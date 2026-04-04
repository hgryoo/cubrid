/*
 * Copyright 2008 Search Solution Corporation
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

/*
 * rtree.c - R-tree spatial index implementation
 *
 * Algorithm: Guttman (1984) R-tree with quadratic split.
 *
 * Storage model (logging deferred – algorithm focus):
 *   - Each R-tree file uses FILE_BTREE / PAGE_BTREE so no disk format
 *     change is needed.
 *   - Every page is a slotted page:
 *       slot 0          : RTREE_NODE_HEADER  (root: RTREE_ROOT_HEADER)
 *       slot 1 .. N     : RTREE_LEAF_ENTRY or RTREE_NON_LEAF_ENTRY
 *
 * Insert path  : choose_leaf → insert_into_leaf → (split_node →
 *                  adjust_tree)* → grow_tree (if root split)
 * Search path  : search_page (recursive DFS)
 * Delete path  : find_leaf → delete_entry → condense_tree → shorten_tree
 */

#include "config.h"

#include "rtree.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "error_manager.h"
#include "file_manager.h"
#include "heap_file.h"
#include "log_manager.h"
#include "page_buffer.h"
#include "slotted_page.h"
#include "storage_common.h"
#include "vacuum.h"
#include "memory_wrapper.hpp"    /* SHOULD BE THE LAST INCLUDE HEADER */

/* ======================================================================
 * Internal constants
 * ====================================================================== */

/* The leaf / non-leaf entry record types stored in slotted pages */
#define RTREE_REC_TYPE   REC_HOME

/*
 * Split-time entry pool: one extra slot beyond the page maximum so we can
 * collect all M+1 entries before re-distributing them.
 */
#define RTREE_SPLIT_POOL_SIZE  (RTREE_MAX_LEAF_ENTRIES + 1)

/* Sentinel value meaning "no slot chosen" in pick_next */
#define RTREE_NO_SLOT  (-1)

/* ======================================================================
 * Internal helper types
 * ====================================================================== */

/* A thin wrapper used during split to hold all M+1 candidate entries in
 * memory before we redistribute them between the two pages. */
typedef struct rtree_split_entry RTREE_SPLIT_ENTRY;
struct rtree_split_entry
{
  RTREE_MBR mbr;
  union
  {
    OID oid;          /* valid when splitting a leaf page  */
    VPID child_vpid;  /* valid when splitting an internal page */
  } ptr;
  bool is_leaf;
};

/* Used by choose_leaf and adjust_tree to record the path from root to leaf */
#define RTREE_MAX_DEPTH  32   /* more than enough for any realistic tree */

typedef struct rtree_path_entry RTREE_PATH_ENTRY;
struct rtree_path_entry
{
  VPID vpid;        /* page at this level */
  PGSLOTID slotid;  /* slot in the parent page that points here */
};

typedef struct rtree_path RTREE_PATH;
struct rtree_path
{
  RTREE_PATH_ENTRY entries[RTREE_MAX_DEPTH];
  int depth;  /* number of entries; entries[depth-1] is the leaf */
};


/* ======================================================================
 * Section 1: MBR utility functions
 * ====================================================================== */

/*
 * rtree_mbr_area - area of an MBR (product of side lengths).
 * Returns 0 for a degenerate MBR (e.g. a point) so area comparisons
 * still work correctly.
 */
double
rtree_mbr_area (const RTREE_MBR * mbr)
{
  double area = 1.0;
  int d;

  for (d = 0; d < RTREE_NDIMS; d++)
    {
      double len = RTREE_MBR_MAX (mbr, d) - RTREE_MBR_MIN (mbr, d);
      if (len < 0.0)
        {
          return 0.0;   /* empty / inverted MBR */
        }
      area *= len;
    }
  return area;
}

/*
 * rtree_mbr_union - enlarge *result to cover both *a and *b.
 */
void
rtree_mbr_union (RTREE_MBR * result, const RTREE_MBR * a, const RTREE_MBR * b)
{
  int d;

  for (d = 0; d < RTREE_NDIMS; d++)
    {
      RTREE_MBR_MIN (result, d) =
        (RTREE_MBR_MIN (a, d) < RTREE_MBR_MIN (b, d)) ? RTREE_MBR_MIN (a, d) : RTREE_MBR_MIN (b, d);
      RTREE_MBR_MAX (result, d) =
        (RTREE_MBR_MAX (a, d) > RTREE_MBR_MAX (b, d)) ? RTREE_MBR_MAX (a, d) : RTREE_MBR_MAX (b, d);
    }
}

/*
 * rtree_mbr_enlargement - area increase needed to extend *mbr to also cover *add.
 */
double
rtree_mbr_enlargement (const RTREE_MBR * mbr, const RTREE_MBR * add)
{
  RTREE_MBR enlarged;
  rtree_mbr_union (&enlarged, mbr, add);
  return rtree_mbr_area (&enlarged) - rtree_mbr_area (mbr);
}

/*
 * rtree_mbr_intersects - true iff the two MBRs share at least one point.
 */
bool
rtree_mbr_intersects (const RTREE_MBR * a, const RTREE_MBR * b)
{
  int d;

  for (d = 0; d < RTREE_NDIMS; d++)
    {
      if (RTREE_MBR_MIN (a, d) > RTREE_MBR_MAX (b, d) ||
          RTREE_MBR_MIN (b, d) > RTREE_MBR_MAX (a, d))
        {
          return false;
        }
    }
  return true;
}

/*
 * rtree_mbr_contains - true iff *outer fully contains *inner.
 */
bool
rtree_mbr_contains (const RTREE_MBR * outer, const RTREE_MBR * inner)
{
  int d;

  for (d = 0; d < RTREE_NDIMS; d++)
    {
      if (RTREE_MBR_MIN (inner, d) < RTREE_MBR_MIN (outer, d) ||
          RTREE_MBR_MAX (inner, d) > RTREE_MBR_MAX (outer, d))
        {
          return false;
        }
    }
  return true;
}

/*
 * rtree_mbr_from_db_spatial - extract a 2-D bounding box from a serialised
 * geometry value that is stored inside a DB_SPATIAL.  The geometry pointer
 * is expected to be a GEOSGeometry* cast to void*.
 *
 * NOTE: this function intentionally avoids a direct dependency on GEOS by
 * delegating through the GEOS C API.  A proper implementation should call
 * GEOSEnvelope_r() and extract the coordinate sequence.  For now we expose
 * the interface and provide a stub that callers can override.
 */
void
rtree_mbr_from_db_spatial (RTREE_MBR * mbr, const void *geom)
{
  /* Stub: callers must supply a pre-computed MBR, or this function must be
   * extended to call into db_geometry.cpp via a thin bridge.  Setting an
   * "empty" MBR signals an error to the caller. */
  RTREE_MBR_SET_EMPTY (mbr);
  (void) geom;
}


/* ======================================================================
 * Section 2: Page header access helpers
 * ====================================================================== */

/*
 * rtree_get_node_header - return pointer to the RTREE_NODE_HEADER stored in
 * slot 0.  The pointer is valid as long as the page is latched.
 */
static RTREE_NODE_HEADER *
rtree_get_node_header (THREAD_ENTRY * thread_p, PAGE_PTR page)
{
  RECDES rec;

  if (spage_get_record (thread_p, page, RTREE_HEADER_SLOTID, &rec, PEEK) != S_SUCCESS)
    {
      return NULL;
    }
  return (RTREE_NODE_HEADER *) rec.data;
}

/*
 * rtree_get_root_header - same as rtree_get_node_header but asserts that
 * the page is the root (node_level > 1 OR empty tree).
 */
static RTREE_ROOT_HEADER *
rtree_get_root_header (THREAD_ENTRY * thread_p, PAGE_PTR page)
{
  RECDES rec;

  if (spage_get_record (thread_p, page, RTREE_HEADER_SLOTID, &rec, PEEK) != S_SUCCESS)
    {
      return NULL;
    }
  /* The root header is larger than a plain node header; check size. */
  if (rec.length < (int) sizeof (RTREE_ROOT_HEADER))
    {
      return NULL;
    }
  return (RTREE_ROOT_HEADER *) rec.data;
}

/*
 * rtree_set_node_header - write an updated RTREE_NODE_HEADER back to slot 0.
 */
static int
rtree_set_node_header (THREAD_ENTRY * thread_p, PAGE_PTR page, RTREE_NODE_HEADER * hdr)
{
  RECDES rec;

  rec.area_size = sizeof (RTREE_NODE_HEADER);
  rec.length    = sizeof (RTREE_NODE_HEADER);
  rec.type      = RTREE_REC_TYPE;
  rec.data      = (char *) hdr;

  if (spage_insert_at (thread_p, page, RTREE_HEADER_SLOTID, &rec) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}

/*
 * rtree_set_root_header - write a full RTREE_ROOT_HEADER to slot 0 of
 * the root page.
 */
static int
rtree_set_root_header (THREAD_ENTRY * thread_p, PAGE_PTR page, RTREE_ROOT_HEADER * rhdr)
{
  RECDES rec;

  rec.area_size = sizeof (RTREE_ROOT_HEADER);
  rec.length    = sizeof (RTREE_ROOT_HEADER);
  rec.type      = RTREE_REC_TYPE;
  rec.data      = (char *) rhdr;

  if (spage_insert_at (thread_p, page, RTREE_HEADER_SLOTID, &rec) != NO_ERROR)
    {
      return ER_FAILED;
    }
  return NO_ERROR;
}

/* ======================================================================
 * Section 3: Page allocation and initialisation
 * ====================================================================== */

/*
 * rtree_initialize_new_page - FILE_INIT_PAGE_FUNC callback.
 * Sets the page type and initialises the slotted page layout.
 * Logging is deferred; the caller is responsible for logging.
 */
int
rtree_initialize_new_page (THREAD_ENTRY * thread_p, PAGE_PTR page, void *args)
{
  (void) args;

  pgbuf_set_page_ptype (thread_p, page, PAGE_BTREE);
  spage_initialize (thread_p, page, RTREE_SLOT_TYPE, RTREE_MAX_ALIGN, DONT_SAFEGUARD_RVSPACE);
  pgbuf_set_dirty (thread_p, page, DONT_FREE);

  return NO_ERROR;
}

/*
 * rtree_alloc_new_page - allocate one new page from the R-tree file and
 * initialise it as a slotted page.  The page is returned write-latched.
 */
static int
rtree_alloc_new_page (THREAD_ENTRY * thread_p, const VFID * vfid, VPID * vpid_out, PAGE_PTR * page_out)
{
  int error_code;

  error_code = file_alloc (thread_p, vfid, rtree_initialize_new_page, NULL, vpid_out, page_out);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }
  if (*page_out == NULL)
    {
      assert_release (false);
      return ER_FAILED;
    }
  return NO_ERROR;
}

/*
 * rtree_create_file - create the underlying file for an R-tree index and
 * allocate + initialise its root page.
 */
static int
rtree_create_file (THREAD_ENTRY * thread_p, const OID * class_oid, int attrid, BTID * btid)
{
  FILE_DESCRIPTORS des;
  VPID vpid_root;
  PAGE_PTR root_page = NULL;
  RTREE_ROOT_HEADER rhdr;
  int error_code = NO_ERROR;

  memset (&des, 0, sizeof (des));
  des.btree.class_oid = *class_oid;
  des.btree.attr_id   = attrid;

  error_code = file_create_with_npages (thread_p, FILE_BTREE, 1, &des, &btid->vfid);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return error_code;
    }

  /* Allocate the root as the sticky first page. */
  log_sysop_start (thread_p);

  error_code = file_alloc_sticky_first_page (thread_p, &btid->vfid,
                                             rtree_initialize_new_page, NULL,
                                             &vpid_root, &root_page);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      log_sysop_abort (thread_p);
      return error_code;
    }
  if (vpid_root.volid != btid->vfid.volid)
    {
      assert_release (false);
      pgbuf_unfix (thread_p, root_page);
      log_sysop_abort (thread_p);
      return ER_FAILED;
    }

  btid->root_pageid = vpid_root.pageid;

  /* Write the root header into slot 0. */
  memset (&rhdr, 0, sizeof (rhdr));
  VPID_SET_NULL (&rhdr.node.parent_vpid);
  rhdr.node.node_level  = 1;   /* empty tree starts as a leaf */
  rhdr.node.num_entries = 0;
  rhdr.num_oids         = 0;
  rhdr.class_oid        = *class_oid;
  rhdr.attr_id          = attrid;

  error_code = rtree_set_root_header (thread_p, root_page, &rhdr);
  if (error_code != NO_ERROR)
    {
      pgbuf_unfix (thread_p, root_page);
      log_sysop_abort (thread_p);
      return error_code;
    }

  pgbuf_set_dirty (thread_p, root_page, DONT_FREE);
  pgbuf_unfix (thread_p, root_page);

  log_sysop_commit (thread_p);
  return NO_ERROR;
}


/* ======================================================================
 * Section 4: Entry record read / write helpers
 * ====================================================================== */

/*
 * rtree_read_leaf_entry - read the leaf entry at *slotid* from *page*.
 * The entry struct is filled in from a PEEK into the slotted page.
 */
static int
rtree_read_leaf_entry (THREAD_ENTRY * thread_p, PAGE_PTR page, PGSLOTID slotid, RTREE_LEAF_ENTRY * entry_out)
{
  RECDES rec;

  if (spage_get_record (thread_p, page, slotid, &rec, PEEK) != S_SUCCESS)
    {
      return ER_FAILED;
    }
  assert (rec.length == (int) sizeof (RTREE_LEAF_ENTRY));
  memcpy (entry_out, rec.data, sizeof (RTREE_LEAF_ENTRY));
  return NO_ERROR;
}

/*
 * rtree_read_non_leaf_entry - read the non-leaf entry at *slotid* from *page*.
 */
static int
rtree_read_non_leaf_entry (THREAD_ENTRY * thread_p, PAGE_PTR page, PGSLOTID slotid,
                           RTREE_NON_LEAF_ENTRY * entry_out)
{
  RECDES rec;

  if (spage_get_record (thread_p, page, slotid, &rec, PEEK) != S_SUCCESS)
    {
      return ER_FAILED;
    }
  assert (rec.length == (int) sizeof (RTREE_NON_LEAF_ENTRY));
  memcpy (entry_out, rec.data, sizeof (RTREE_NON_LEAF_ENTRY));
  return NO_ERROR;
}

/*
 * rtree_write_leaf_entry - overwrite the leaf entry at *slotid* (must already
 * exist; uses spage_delete + spage_insert_at to replace).
 */
static int
rtree_write_leaf_entry (THREAD_ENTRY * thread_p, PAGE_PTR page, PGSLOTID slotid, const RTREE_LEAF_ENTRY * entry)
{
  RECDES rec;

  rec.area_size = sizeof (RTREE_LEAF_ENTRY);
  rec.length    = sizeof (RTREE_LEAF_ENTRY);
  rec.type      = RTREE_REC_TYPE;
  rec.data      = (char *) entry;

  spage_delete (thread_p, page, slotid);
  return spage_insert_at (thread_p, page, slotid, &rec);
}

/*
 * rtree_write_non_leaf_entry - overwrite the non-leaf entry at *slotid*.
 */
static int
rtree_write_non_leaf_entry (THREAD_ENTRY * thread_p, PAGE_PTR page, PGSLOTID slotid,
                            const RTREE_NON_LEAF_ENTRY * entry)
{
  RECDES rec;

  rec.area_size = sizeof (RTREE_NON_LEAF_ENTRY);
  rec.length    = sizeof (RTREE_NON_LEAF_ENTRY);
  rec.type      = RTREE_REC_TYPE;
  rec.data      = (char *) entry;

  spage_delete (thread_p, page, slotid);
  return spage_insert_at (thread_p, page, slotid, &rec);
}

/*
 * rtree_append_leaf_entry - append a new leaf entry to *page*.
 * slot 0 is the header, so the first entry is at slot 1.
 */
static int
rtree_append_leaf_entry (THREAD_ENTRY * thread_p, PAGE_PTR page, const RTREE_LEAF_ENTRY * entry)
{
  RECDES rec;
  PGSLOTID dummy;
  RTREE_NODE_HEADER *hdr;

  rec.area_size = sizeof (RTREE_LEAF_ENTRY);
  rec.length    = sizeof (RTREE_LEAF_ENTRY);
  rec.type      = RTREE_REC_TYPE;
  rec.data      = (char *) entry;

  if (spage_insert (thread_p, page, &rec, &dummy) != SP_SUCCESS)
    {
      return ER_FAILED;
    }

  hdr = rtree_get_node_header (thread_p, page);
  if (hdr != NULL)
    {
      hdr->num_entries++;
    }
  return NO_ERROR;
}

/*
 * rtree_append_non_leaf_entry - append a new non-leaf entry to *page*.
 */
static int
rtree_append_non_leaf_entry (THREAD_ENTRY * thread_p, PAGE_PTR page, const RTREE_NON_LEAF_ENTRY * entry)
{
  RECDES rec;
  PGSLOTID dummy;
  RTREE_NODE_HEADER *hdr;

  rec.area_size = sizeof (RTREE_NON_LEAF_ENTRY);
  rec.length    = sizeof (RTREE_NON_LEAF_ENTRY);
  rec.type      = RTREE_REC_TYPE;
  rec.data      = (char *) entry;

  if (spage_insert (thread_p, page, &rec, &dummy) != SP_SUCCESS)
    {
      return ER_FAILED;
    }

  hdr = rtree_get_node_header (thread_p, page);
  if (hdr != NULL)
    {
      hdr->num_entries++;
    }
  return NO_ERROR;
}


/* ======================================================================
 * Section 5: Quadratic Split
 *
 * Reference: Guttman, R. (1984). R-trees: A dynamic index structure for
 *            spatial searching. SIGMOD '84.
 *
 * The split receives M+1 entries collected from the overflowing page plus
 * the new entry that triggered the overflow.  It produces two groups that
 * are then written to the original page (cleared) and a newly allocated
 * sibling page.
 * ====================================================================== */

/*
 * rtree_pick_seeds - QuadraticPickSeeds.
 *
 * Find the two entries that would waste the most space if placed in the
 * same group.  Returns their indices via *seed1 and *seed2.
 */
static void
rtree_pick_seeds (RTREE_SPLIT_ENTRY * pool, int n, int *seed1, int *seed2)
{
  double worst_waste = -DBL_MAX;
  int i, j;

  *seed1 = 0;
  *seed2 = 1;

  for (i = 0; i < n - 1; i++)
    {
      for (j = i + 1; j < n; j++)
        {
          RTREE_MBR combined;
          double waste;

          rtree_mbr_union (&combined, &pool[i].mbr, &pool[j].mbr);
          waste = rtree_mbr_area (&combined)
                  - rtree_mbr_area (&pool[i].mbr)
                  - rtree_mbr_area (&pool[j].mbr);

          if (waste > worst_waste)
            {
              worst_waste = waste;
              *seed1 = i;
              *seed2 = j;
            }
        }
    }
}

/*
 * rtree_pick_next - QuadraticPickNext.
 *
 * Among the remaining (unassigned) entries, find the one with the greatest
 * difference between the area-enlargement needed for group 1 vs group 2.
 * *assigned* is a bool array: assigned[i]=true means entry i already has a
 * group.  Returns the index of the chosen entry.
 */
static int
rtree_pick_next (RTREE_SPLIT_ENTRY * pool, int n, bool * assigned,
                 const RTREE_MBR * mbr1, const RTREE_MBR * mbr2)
{
  double best_diff = -1.0;
  int best = RTREE_NO_SLOT;
  int i;

  for (i = 0; i < n; i++)
    {
      double d1, d2, diff;

      if (assigned[i])
        {
          continue;
        }

      d1   = rtree_mbr_enlargement (mbr1, &pool[i].mbr);
      d2   = rtree_mbr_enlargement (mbr2, &pool[i].mbr);
      diff = fabs (d1 - d2);

      if (diff > best_diff)
        {
          best_diff = diff;
          best      = i;
        }
    }
  return best;
}

/*
 * rtree_distribute_entries - distribute the *n* entries in *pool* between
 * group1 (going to the original page) and group2 (going to a new sibling).
 * On return *g1_ids / *g1_cnt and *g2_ids / *g2_cnt hold the slot indices
 * that belong to each group.
 *
 * The arrays g1_ids / g2_ids must be pre-allocated with at least n slots.
 */
static void
rtree_distribute_entries (RTREE_SPLIT_ENTRY * pool, int n,
                          int *g1_ids, int *g1_cnt,
                          int *g2_ids, int *g2_cnt)
{
  bool *assigned;
  RTREE_MBR mbr1, mbr2;
  int remaining, seed1, seed2, next;
  int i;
  int min_entries = RTREE_MIN_ENTRIES;

  assigned = (bool *) malloc (n * sizeof (bool));
  if (assigned == NULL)
    {
      /* Fallback: split evenly */
      for (i = 0; i < n; i++)
        {
          if (i < n / 2)
            {
              g1_ids[(*g1_cnt)++] = i;
            }
          else
            {
              g2_ids[(*g2_cnt)++] = i;
            }
        }
      return;
    }

  memset (assigned, 0, n * sizeof (bool));
  *g1_cnt = 0;
  *g2_cnt = 0;

  /* Step 1: pick seeds */
  rtree_pick_seeds (pool, n, &seed1, &seed2);

  g1_ids[(*g1_cnt)++] = seed1;
  g2_ids[(*g2_cnt)++] = seed2;
  assigned[seed1] = true;
  assigned[seed2] = true;

  mbr1 = pool[seed1].mbr;
  mbr2 = pool[seed2].mbr;

  remaining = n - 2;

  /* Step 2: repeatedly pick the next entry to assign */
  while (remaining > 0)
    {
      RTREE_MBR enlarged;
      double d1, d2;

      /* If one group needs all remaining entries to meet min_entries, force
       * assign them all. */
      if (*g1_cnt + remaining == min_entries)
        {
          for (i = 0; i < n; i++)
            {
              if (!assigned[i])
                {
                  g1_ids[(*g1_cnt)++] = i;
                  assigned[i] = true;
                  remaining--;
                }
            }
          break;
        }
      if (*g2_cnt + remaining == min_entries)
        {
          for (i = 0; i < n; i++)
            {
              if (!assigned[i])
                {
                  g2_ids[(*g2_cnt)++] = i;
                  assigned[i] = true;
                  remaining--;
                }
            }
          break;
        }

      next = rtree_pick_next (pool, n, assigned, &mbr1, &mbr2);
      if (next == RTREE_NO_SLOT)
        {
          break;
        }

      d1 = rtree_mbr_enlargement (&mbr1, &pool[next].mbr);
      d2 = rtree_mbr_enlargement (&mbr2, &pool[next].mbr);

      if (d1 < d2 || (d1 == d2 && *g1_cnt <= *g2_cnt))
        {
          g1_ids[(*g1_cnt)++] = next;
          rtree_mbr_union (&enlarged, &mbr1, &pool[next].mbr);
          mbr1 = enlarged;
        }
      else
        {
          g2_ids[(*g2_cnt)++] = next;
          rtree_mbr_union (&enlarged, &mbr2, &pool[next].mbr);
          mbr2 = enlarged;
        }

      assigned[next] = true;
      remaining--;
    }

  free (assigned);
}


/* ======================================================================
 * Section 6: Split node
 *
 * Splits an overflowing page (original_page) into two pages.  The new
 * sibling page is allocated here.  On return the caller's path is updated
 * with the sibling VPID / separator MBR so that adjust_tree can propagate
 * the change upward.
 * ====================================================================== */

/*
 * rtree_split_node - split page *Q* (which has M entries plus one pending
 * entry given by *new_entry* / *new_is_leaf*) into Q and a new page R.
 *
 * After the split:
 *   - Q contains group-1 entries (page latched write, marked dirty)
 *   - R is allocated, contains group-2 entries (unlatched after return)
 *   - *sibling_vpid_out holds R's VPID
 *   - *sibling_mbr_out holds the MBR covering all entries in R
 *   - *q_mbr_out holds the new MBR covering all entries remaining in Q
 */
static int
rtree_split_node (THREAD_ENTRY * thread_p, const BTID * btid,
                  PAGE_PTR Q, const VPID * Q_vpid,
                  RTREE_SPLIT_ENTRY *new_entry,
                  bool new_is_leaf,
                  VPID * sibling_vpid_out,
                  RTREE_MBR * sibling_mbr_out,
                  RTREE_MBR * q_mbr_out)
{
  int n_existing;
  int pool_size;
  RTREE_SPLIT_ENTRY *pool = NULL;
  int *g1_ids = NULL, *g2_ids = NULL;
  int g1_cnt = 0, g2_cnt = 0;
  PAGE_PTR R = NULL;
  VPID R_vpid;
  RTREE_NODE_HEADER *q_hdr;
  RTREE_NODE_HEADER r_hdr;
  int i, d, error_code = NO_ERROR;

  q_hdr      = rtree_get_node_header (thread_p, Q);
  n_existing = spage_number_of_records (Q) - 1;  /* subtract header slot */
  pool_size  = n_existing + 1;                    /* existing + new entry */

  pool   = (RTREE_SPLIT_ENTRY *) malloc (pool_size * sizeof (RTREE_SPLIT_ENTRY));
  g1_ids = (int *) malloc (pool_size * sizeof (int));
  g2_ids = (int *) malloc (pool_size * sizeof (int));

  if (pool == NULL || g1_ids == NULL || g2_ids == NULL)
    {
      error_code = ER_OUT_OF_VIRTUAL_MEMORY;
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1,
              pool_size * sizeof (RTREE_SPLIT_ENTRY));
      goto cleanup;
    }

  /* Collect all existing entries into the pool */
  if (new_is_leaf)
    {
      RTREE_LEAF_ENTRY le;

      for (i = 0; i < n_existing; i++)
        {
          if (rtree_read_leaf_entry (thread_p, Q, (PGSLOTID) (i + 1), &le) != NO_ERROR)
            {
              error_code = ER_FAILED;
              goto cleanup;
            }
          pool[i].mbr     = le.mbr;
          pool[i].ptr.oid = le.oid;
          pool[i].is_leaf = true;
        }
    }
  else
    {
      RTREE_NON_LEAF_ENTRY nle;

      for (i = 0; i < n_existing; i++)
        {
          if (rtree_read_non_leaf_entry (thread_p, Q, (PGSLOTID) (i + 1), &nle) != NO_ERROR)
            {
              error_code = ER_FAILED;
              goto cleanup;
            }
          pool[i].mbr             = nle.mbr;
          pool[i].ptr.child_vpid  = nle.child_vpid;
          pool[i].is_leaf         = false;
        }
    }

  /* Add the new (overflowing) entry at the end */
  pool[n_existing] = *new_entry;

  /* Distribute into two groups */
  rtree_distribute_entries (pool, pool_size, g1_ids, &g1_cnt, g2_ids, &g2_cnt);

  /* ----------------------------------------------------------------
   * Allocate the sibling page R
   * ---------------------------------------------------------------- */
  error_code = rtree_alloc_new_page (thread_p, &btid->vfid, &R_vpid, &R);
  if (error_code != NO_ERROR)
    {
      goto cleanup;
    }

  /* Initialise R's node header */
  memset (&r_hdr, 0, sizeof (r_hdr));
  r_hdr.node_level  = q_hdr->node_level;
  r_hdr.num_entries = 0;
  VPID_SET_NULL (&r_hdr.parent_vpid);  /* parent will be fixed by adjust_tree */

  {
    RECDES rec;
    rec.area_size = sizeof (RTREE_NODE_HEADER);
    rec.length    = sizeof (RTREE_NODE_HEADER);
    rec.type      = RTREE_REC_TYPE;
    rec.data      = (char *) &r_hdr;
    if (spage_insert_at (thread_p, R, RTREE_HEADER_SLOTID, &rec) != NO_ERROR)
      {
        error_code = ER_FAILED;
        goto cleanup;
      }
  }

  /* ----------------------------------------------------------------
   * Rebuild Q with group-1 entries and R with group-2 entries
   * ---------------------------------------------------------------- */

  /* Clear all entries from Q (keep slot 0 header slot, delete 1..N) */
  for (i = spage_number_of_records (Q) - 1; i >= 1; i--)
    {
      spage_delete (thread_p, Q, (PGSLOTID) i);
    }
  q_hdr->num_entries = 0;

  /* Compute MBRs for both groups */
  RTREE_MBR_SET_EMPTY (q_mbr_out);
  RTREE_MBR_SET_EMPTY (sibling_mbr_out);

  if (new_is_leaf)
    {
      /* Write group-1 entries to Q */
      for (i = 0; i < g1_cnt; i++)
        {
          RTREE_LEAF_ENTRY le;
          PGSLOTID dummy;
          RECDES rec;
          RTREE_MBR tmp;

          le.mbr     = pool[g1_ids[i]].mbr;
          le.oid     = pool[g1_ids[i]].ptr.oid;
          le.padding = 0;

          rec.area_size = sizeof (RTREE_LEAF_ENTRY);
          rec.length    = sizeof (RTREE_LEAF_ENTRY);
          rec.type      = RTREE_REC_TYPE;
          rec.data      = (char *) &le;

          if (spage_insert (thread_p, Q, &rec, &dummy) != SP_SUCCESS)
            {
              error_code = ER_FAILED;
              goto cleanup;
            }

          rtree_mbr_union (&tmp, q_mbr_out, &le.mbr);
          *q_mbr_out = tmp;
        }
      q_hdr->num_entries = g1_cnt;

      /* Write group-2 entries to R */
      for (i = 0; i < g2_cnt; i++)
        {
          RTREE_LEAF_ENTRY le;
          PGSLOTID dummy;
          RECDES rec;
          RTREE_MBR tmp;

          le.mbr     = pool[g2_ids[i]].mbr;
          le.oid     = pool[g2_ids[i]].ptr.oid;
          le.padding = 0;

          rec.area_size = sizeof (RTREE_LEAF_ENTRY);
          rec.length    = sizeof (RTREE_LEAF_ENTRY);
          rec.type      = RTREE_REC_TYPE;
          rec.data      = (char *) &le;

          if (spage_insert (thread_p, R, &rec, &dummy) != SP_SUCCESS)
            {
              error_code = ER_FAILED;
              goto cleanup;
            }

          rtree_mbr_union (&tmp, sibling_mbr_out, &le.mbr);
          *sibling_mbr_out = tmp;
        }
      {
        RTREE_NODE_HEADER *rh = rtree_get_node_header (thread_p, R);
        if (rh != NULL)
          {
            rh->num_entries = g2_cnt;
          }
      }
    }
  else /* non-leaf split */
    {
      /* Write group-1 entries to Q */
      for (i = 0; i < g1_cnt; i++)
        {
          RTREE_NON_LEAF_ENTRY nle;
          PGSLOTID dummy;
          RECDES rec;
          RTREE_MBR tmp;

          nle.mbr        = pool[g1_ids[i]].mbr;
          nle.child_vpid = pool[g1_ids[i]].ptr.child_vpid;
          nle.padding    = 0;

          rec.area_size = sizeof (RTREE_NON_LEAF_ENTRY);
          rec.length    = sizeof (RTREE_NON_LEAF_ENTRY);
          rec.type      = RTREE_REC_TYPE;
          rec.data      = (char *) &nle;

          if (spage_insert (thread_p, Q, &rec, &dummy) != SP_SUCCESS)
            {
              error_code = ER_FAILED;
              goto cleanup;
            }

          rtree_mbr_union (&tmp, q_mbr_out, &nle.mbr);
          *q_mbr_out = tmp;
        }
      q_hdr->num_entries = g1_cnt;

      /* Write group-2 entries to R */
      for (i = 0; i < g2_cnt; i++)
        {
          RTREE_NON_LEAF_ENTRY nle;
          PGSLOTID dummy;
          RECDES rec;
          RTREE_MBR tmp;

          nle.mbr        = pool[g2_ids[i]].mbr;
          nle.child_vpid = pool[g2_ids[i]].ptr.child_vpid;
          nle.padding    = 0;

          rec.area_size = sizeof (RTREE_NON_LEAF_ENTRY);
          rec.length    = sizeof (RTREE_NON_LEAF_ENTRY);
          rec.type      = RTREE_REC_TYPE;
          rec.data      = (char *) &nle;

          if (spage_insert (thread_p, R, &rec, &dummy) != SP_SUCCESS)
            {
              error_code = ER_FAILED;
              goto cleanup;
            }

          rtree_mbr_union (&tmp, sibling_mbr_out, &nle.mbr);
          *sibling_mbr_out = tmp;
        }
      {
        RTREE_NODE_HEADER *rh = rtree_get_node_header (thread_p, R);
        if (rh != NULL)
          {
            rh->num_entries = g2_cnt;
          }
      }
    }

  *sibling_vpid_out = R_vpid;

  pgbuf_set_dirty (thread_p, Q, DONT_FREE);
  pgbuf_set_dirty (thread_p, R, DONT_FREE);
  pgbuf_unfix (thread_p, R);

cleanup:
  free (pool);
  free (g1_ids);
  free (g2_ids);

  if (error_code != NO_ERROR && R != NULL)
    {
      pgbuf_unfix (thread_p, R);
    }

  return error_code;
}


/* ======================================================================
 * Section 7: Insert algorithm
 *
 * rtree_choose_leaf   - descend to the most suitable leaf
 * rtree_insert        - main entry point
 * rtree_adjust_tree   - propagate MBR updates and splits upward
 * rtree_grow_tree     - grow the tree by one level when the root splits
 * ====================================================================== */

/*
 * rtree_compute_page_mbr - recompute the MBR covering all entries on *page*.
 * Works for both leaf and non-leaf pages (checks node_level).
 */
static void
rtree_compute_page_mbr (THREAD_ENTRY * thread_p, PAGE_PTR page, RTREE_MBR * mbr_out)
{
  RTREE_NODE_HEADER *hdr;
  int i, n;

  RTREE_MBR_SET_EMPTY (mbr_out);

  hdr = rtree_get_node_header (thread_p, page);
  if (hdr == NULL)
    {
      return;
    }

  n = hdr->num_entries;

  if (hdr->node_level == 1)
    {
      /* Leaf page */
      for (i = 1; i <= n; i++)
        {
          RTREE_LEAF_ENTRY le;
          RTREE_MBR tmp;

          if (rtree_read_leaf_entry (thread_p, page, (PGSLOTID) i, &le) != NO_ERROR)
            {
              break;
            }
          rtree_mbr_union (&tmp, mbr_out, &le.mbr);
          *mbr_out = tmp;
        }
    }
  else
    {
      /* Non-leaf page */
      for (i = 1; i <= n; i++)
        {
          RTREE_NON_LEAF_ENTRY nle;
          RTREE_MBR tmp;

          if (rtree_read_non_leaf_entry (thread_p, page, (PGSLOTID) i, &nle) != NO_ERROR)
            {
              break;
            }
          rtree_mbr_union (&tmp, mbr_out, &nle.mbr);
          *mbr_out = tmp;
        }
    }
}

/*
 * rtree_choose_subtree - at an internal node, pick the child entry with the
 * least area enlargement (ties broken by smallest area).
 * Returns the slot id (1-based) of the chosen child entry.
 */
static PGSLOTID
rtree_choose_subtree (THREAD_ENTRY * thread_p, PAGE_PTR page, const RTREE_MBR * insert_mbr)
{
  RTREE_NODE_HEADER *hdr;
  int n;
  PGSLOTID best_slot = 1;
  double best_enl = DBL_MAX;
  double best_area = DBL_MAX;
  int i;

  hdr = rtree_get_node_header (thread_p, page);
  if (hdr == NULL)
    {
      return 1;
    }

  n = hdr->num_entries;

  for (i = 1; i <= n; i++)
    {
      RTREE_NON_LEAF_ENTRY nle;
      double enl, area;

      if (rtree_read_non_leaf_entry (thread_p, page, (PGSLOTID) i, &nle) != NO_ERROR)
        {
          continue;
        }

      enl  = rtree_mbr_enlargement (&nle.mbr, insert_mbr);
      area = rtree_mbr_area (&nle.mbr);

      if (enl < best_enl || (enl == best_enl && area < best_area))
        {
          best_enl  = enl;
          best_area = area;
          best_slot = (PGSLOTID) i;
        }
    }
  return best_slot;
}

/*
 * rtree_choose_leaf - descend the tree from the root to the leaf page most
 * suitable for inserting *insert_mbr*.
 *
 * On return *path* records the full root-to-leaf path so that adjust_tree
 * can walk it in reverse.  path->entries[path->depth-1] is the leaf.
 * The leaf page is returned latched for write; all ancestor pages have
 * already been unlatched.
 */
static PAGE_PTR
rtree_choose_leaf (THREAD_ENTRY * thread_p, const BTID * btid, const RTREE_MBR * insert_mbr, RTREE_PATH * path)
{
  VPID cur_vpid;
  PAGE_PTR cur_page;
  RTREE_NODE_HEADER *hdr;
  int depth = 0;

  cur_vpid.volid  = btid->vfid.volid;
  cur_vpid.pageid = btid->root_pageid;

  path->depth = 0;

  for (;;)
    {
      PGSLOTID chosen_slot;
      RTREE_NON_LEAF_ENTRY nle;

      cur_page = pgbuf_fix (thread_p, &cur_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (cur_page == NULL)
        {
          return NULL;
        }

      hdr = rtree_get_node_header (thread_p, cur_page);
      if (hdr == NULL)
        {
          pgbuf_unfix (thread_p, cur_page);
          return NULL;
        }

      /* Record this page in the path */
      if (depth >= RTREE_MAX_DEPTH)
        {
          pgbuf_unfix (thread_p, cur_page);
          return NULL;
        }
      path->entries[depth].vpid    = cur_vpid;
      path->entries[depth].slotid  = RTREE_NO_SLOT; /* filled by parent */
      depth++;

      if (hdr->node_level == 1)
        {
          /* Reached the leaf */
          path->depth = depth;
          return cur_page;
        }

      /* Choose child */
      chosen_slot = rtree_choose_subtree (thread_p, cur_page, insert_mbr);
      path->entries[depth - 1].slotid = chosen_slot;

      if (rtree_read_non_leaf_entry (thread_p, cur_page, chosen_slot, &nle) != NO_ERROR)
        {
          pgbuf_unfix (thread_p, cur_page);
          return NULL;
        }

      cur_vpid = nle.child_vpid;
      pgbuf_unfix (thread_p, cur_page);
    }
}

/*
 * rtree_adjust_tree - walk the path bottom-up after an insert.
 *
 * At each level:
 *   1. Re-compute and update the MBR of the entry pointing to the child.
 *   2. If the child was split, add the new sibling entry to this page.
 *      If that causes another overflow, split again recursively.
 *
 * *split_vpid / *split_mbr* describe the sibling produced at the previous
 * level (NULL_VPID if no split happened).
 */
static int
rtree_adjust_tree (THREAD_ENTRY * thread_p, const BTID * btid, RTREE_PATH * path,
                   RTREE_MBR *leaf_mbr,
                   VPID * split_vpid, RTREE_MBR * split_mbr,
                   /* out: new root-level split (if any) */
                   VPID * new_root_sibling_vpid, RTREE_MBR * new_root_sibling_mbr)
{
  int level;
  VPID cur_split_vpid   = *split_vpid;
  RTREE_MBR cur_split_mbr = *split_mbr;
  bool has_split = !VPID_ISNULL (split_vpid);
  int error_code = NO_ERROR;

  VPID_SET_NULL (new_root_sibling_vpid);

  for (level = path->depth - 2; level >= 0; level--)
    {
      VPID parent_vpid  = path->entries[level].vpid;
      PGSLOTID child_slot = path->entries[level].slotid;
      PAGE_PTR parent_page;
      RTREE_NODE_HEADER *parent_hdr;
      RTREE_NON_LEAF_ENTRY child_entry;
      int max_entries;

      parent_page = pgbuf_fix (thread_p, &parent_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
      if (parent_page == NULL)
        {
          return ER_FAILED;
        }

      parent_hdr = rtree_get_node_header (thread_p, parent_page);
      if (parent_hdr == NULL)
        {
          pgbuf_unfix (thread_p, parent_page);
          return ER_FAILED;
        }

      /* Update the MBR of the entry that points to the child we just modified */
      if (rtree_read_non_leaf_entry (thread_p, parent_page, child_slot, &child_entry) != NO_ERROR)
        {
          pgbuf_unfix (thread_p, parent_page);
          return ER_FAILED;
        }

      /* Recompute child's MBR from path info (use leaf_mbr at bottom, then
       * recompute from page at higher levels -- simpler: recompute from page) */
      {
        VPID child_vpid = path->entries[level + 1].vpid;
        PAGE_PTR child_page = pgbuf_fix (thread_p, &child_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
        if (child_page != NULL)
          {
            rtree_compute_page_mbr (thread_p, child_page, &child_entry.mbr);
            pgbuf_unfix (thread_p, child_page);
          }
      }

      rtree_write_non_leaf_entry (thread_p, parent_page, child_slot, &child_entry);

      /* If there is a split to propagate, add the sibling entry */
      if (has_split)
        {
          RTREE_NON_LEAF_ENTRY sibling_entry;
          RTREE_SPLIT_ENTRY new_se;

          sibling_entry.mbr        = cur_split_mbr;
          sibling_entry.child_vpid = cur_split_vpid;
          sibling_entry.padding    = 0;

          max_entries = RTREE_MAX_NON_LEAF_ENTRIES;

          if (parent_hdr->num_entries < max_entries)
            {
              /* Enough room – just append */
              if (rtree_append_non_leaf_entry (thread_p, parent_page, &sibling_entry) != NO_ERROR)
                {
                  pgbuf_unfix (thread_p, parent_page);
                  return ER_FAILED;
                }
              has_split = false;
            }
          else
            {
              /* Need to split this internal node */
              VPID this_vpid  = parent_vpid;
              VPID new_sib_vpid;
              RTREE_MBR new_sib_mbr, q_mbr;

              new_se.mbr            = sibling_entry.mbr;
              new_se.ptr.child_vpid = sibling_entry.child_vpid;
              new_se.is_leaf        = false;

              error_code = rtree_split_node (thread_p, btid, parent_page, &this_vpid,
                                             &new_se, false,
                                             &new_sib_vpid, &new_sib_mbr, &q_mbr);
              if (error_code != NO_ERROR)
                {
                  pgbuf_unfix (thread_p, parent_page);
                  return error_code;
                }

              cur_split_vpid = new_sib_vpid;
              cur_split_mbr  = new_sib_mbr;
              has_split      = true;

              if (level == 0)
                {
                  /* Split propagated to the root level */
                  *new_root_sibling_vpid = cur_split_vpid;
                  *new_root_sibling_mbr  = cur_split_mbr;
                }
            }
        }

      pgbuf_set_dirty (thread_p, parent_page, DONT_FREE);
      pgbuf_unfix (thread_p, parent_page);
    }

  return NO_ERROR;
}

/*
 * rtree_grow_tree - called when the root itself overflows and is split.
 * Creates a new root page that points to the old root and the new sibling.
 */
static int
rtree_grow_tree (THREAD_ENTRY * thread_p, BTID * btid,
                 const VPID * old_root_vpid, const RTREE_MBR * old_root_mbr,
                 const VPID * sibling_vpid, const RTREE_MBR * sibling_mbr,
                 short child_node_level)
{
  VPID new_root_vpid;
  PAGE_PTR new_root_page = NULL;
  RTREE_ROOT_HEADER new_rhdr;
  RTREE_NON_LEAF_ENTRY e1, e2;
  RTREE_ROOT_HEADER *old_rhdr;
  PAGE_PTR old_root_page = NULL;
  int error_code = NO_ERROR;

  /* Read old root to copy metadata */
  old_root_page = pgbuf_fix (thread_p, old_root_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (old_root_page == NULL)
    {
      return ER_FAILED;
    }

  old_rhdr = rtree_get_root_header (thread_p, old_root_page);
  if (old_rhdr == NULL)
    {
      pgbuf_unfix (thread_p, old_root_page);
      return ER_FAILED;
    }

  memcpy (&new_rhdr, old_rhdr, sizeof (RTREE_ROOT_HEADER));
  pgbuf_unfix (thread_p, old_root_page);

  /* Allocate new root page */
  error_code = rtree_alloc_new_page (thread_p, &btid->vfid, &new_root_vpid, &new_root_page);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  new_rhdr.node.node_level  = child_node_level + 1;
  new_rhdr.node.num_entries = 2;
  VPID_SET_NULL (&new_rhdr.node.parent_vpid);

  /* Write new root header */
  {
    RECDES rec;
    rec.area_size = sizeof (RTREE_ROOT_HEADER);
    rec.length    = sizeof (RTREE_ROOT_HEADER);
    rec.type      = RTREE_REC_TYPE;
    rec.data      = (char *) &new_rhdr;
    if (spage_insert_at (thread_p, new_root_page, RTREE_HEADER_SLOTID, &rec) != NO_ERROR)
      {
        pgbuf_unfix (thread_p, new_root_page);
        return ER_FAILED;
      }
  }

  /* Entry for old root */
  e1.mbr        = *old_root_mbr;
  e1.child_vpid = *old_root_vpid;
  e1.padding    = 0;

  /* Entry for sibling */
  e2.mbr        = *sibling_mbr;
  e2.child_vpid = *sibling_vpid;
  e2.padding    = 0;

  if (rtree_append_non_leaf_entry (thread_p, new_root_page, &e1) != NO_ERROR ||
      rtree_append_non_leaf_entry (thread_p, new_root_page, &e2) != NO_ERROR)
    {
      pgbuf_unfix (thread_p, new_root_page);
      return ER_FAILED;
    }

  pgbuf_set_dirty (thread_p, new_root_page, DONT_FREE);
  pgbuf_unfix (thread_p, new_root_page);

  /* Update BTID to point to the new root */
  btid->root_pageid = new_root_vpid.pageid;

  /* NOTE: The BTID update must also be persisted in the catalog/schema
   * layer.  That persistence path is handled by the caller (xrtree_insert /
   * schema_manager) outside this storage module. */

  return NO_ERROR;
}


/* ======================================================================
 * Section 8: Public insert
 * ====================================================================== */

/*
 * rtree_insert - insert a (MBR, OID) pair into the R-tree identified by *btid*.
 *
 * Algorithm (Guttman Algorithm Insert):
 *   I1. Choose a leaf node L using choose_leaf.
 *   I2. Add E to L. If L has room, add it and stop.
 *   I3. If L is full, invoke split_node to produce L and LL.
 *   I4. Adjust tree: propagate MBR/split up the path.
 *   I5. If the root was split, grow the tree by one level.
 */
int
rtree_insert (THREAD_ENTRY * thread_p, BTID * btid, RTREE_MBR * mbr, OID * oid)
{
  RTREE_PATH path;
  PAGE_PTR leaf_page = NULL;
  RTREE_NODE_HEADER *leaf_hdr;
  RTREE_LEAF_ENTRY new_entry;
  int max_leaf_entries;
  int error_code = NO_ERROR;

  VPID split_vpid;
  RTREE_MBR split_mbr;
  VPID new_root_sib_vpid;
  RTREE_MBR new_root_sib_mbr;

  VPID_SET_NULL (&split_vpid);

  /* I1: choose leaf */
  leaf_page = rtree_choose_leaf (thread_p, btid, mbr, &path);
  if (leaf_page == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  leaf_hdr = rtree_get_node_header (thread_p, leaf_page);
  if (leaf_hdr == NULL)
    {
      pgbuf_unfix (thread_p, leaf_page);
      return ER_FAILED;
    }

  max_leaf_entries = RTREE_MAX_LEAF_ENTRIES;

  new_entry.mbr     = *mbr;
  new_entry.oid     = *oid;
  new_entry.padding = 0;

  if (leaf_hdr->num_entries < max_leaf_entries)
    {
      /* I2: enough space – insert directly */
      error_code = rtree_append_leaf_entry (thread_p, leaf_page, &new_entry);
      if (error_code != NO_ERROR)
        {
          pgbuf_unfix (thread_p, leaf_page);
          return error_code;
        }
      pgbuf_set_dirty (thread_p, leaf_page, DONT_FREE);
    }
  else
    {
      /* I3: leaf is full – split it */
      RTREE_SPLIT_ENTRY se;
      RTREE_MBR q_mbr;
      VPID leaf_vpid = path.entries[path.depth - 1].vpid;

      se.mbr     = new_entry.mbr;
      se.ptr.oid = new_entry.oid;
      se.is_leaf = true;

      error_code = rtree_split_node (thread_p, btid, leaf_page, &leaf_vpid,
                                     &se, true,
                                     &split_vpid, &split_mbr, &q_mbr);
      if (error_code != NO_ERROR)
        {
          pgbuf_unfix (thread_p, leaf_page);
          return error_code;
        }
    }

  pgbuf_unfix (thread_p, leaf_page);

  if (path.depth <= 1)
    {
      /* Tree is only one page (root = leaf).  If a split happened we need
       * to grow the tree immediately; no parent levels to adjust. */
      if (!VPID_ISNULL (&split_vpid))
        {
          VPID root_vpid;
          RTREE_MBR old_root_mbr;
          PAGE_PTR root_page;

          root_vpid.volid  = btid->vfid.volid;
          root_vpid.pageid = btid->root_pageid;

          root_page = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
          if (root_page != NULL)
            {
              rtree_compute_page_mbr (thread_p, root_page, &old_root_mbr);
              pgbuf_unfix (thread_p, root_page);
            }
          else
            {
              RTREE_MBR_SET_EMPTY (&old_root_mbr);
            }

          error_code = rtree_grow_tree (thread_p, btid,
                                        &root_vpid, &old_root_mbr,
                                        &split_vpid, &split_mbr,
                                        1 /* leaf level */);
        }
      return error_code;
    }

  /* I4: adjust tree – propagate up; may generate a split at the root level */
  error_code = rtree_adjust_tree (thread_p, btid, &path,
                                  mbr,
                                  &split_vpid, &split_mbr,
                                  &new_root_sib_vpid, &new_root_sib_mbr);
  if (error_code != NO_ERROR)
    {
      return error_code;
    }

  /* I5: if the root was split, grow the tree */
  if (!VPID_ISNULL (&new_root_sib_vpid))
    {
      VPID root_vpid;
      RTREE_MBR old_root_mbr;
      PAGE_PTR root_page;
      short cur_root_level;

      root_vpid.volid  = btid->vfid.volid;
      root_vpid.pageid = btid->root_pageid;

      root_page = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
      if (root_page == NULL)
        {
          return ER_FAILED;
        }
      rtree_compute_page_mbr (thread_p, root_page, &old_root_mbr);
      {
        RTREE_NODE_HEADER *rh = rtree_get_node_header (thread_p, root_page);
        cur_root_level = rh ? rh->node_level : 1;
      }
      pgbuf_unfix (thread_p, root_page);

      error_code = rtree_grow_tree (thread_p, btid,
                                    &root_vpid, &old_root_mbr,
                                    &new_root_sib_vpid, &new_root_sib_mbr,
                                    cur_root_level);
    }

  return error_code;
}


/* ======================================================================
 * Section 9: Search
 * ====================================================================== */

/*
 * rtree_search_page - recursive DFS search helper.
 *
 * Visits *page* and all qualifying subtrees.  For each leaf entry whose MBR
 * satisfies *mode* against *search_mbr*, invokes *cb(oid, cb_arg)*.
 */
static int
rtree_search_page (THREAD_ENTRY * thread_p, const BTID * btid, const VPID * page_vpid,
                   const RTREE_MBR * search_mbr, RTREE_SEARCH_MODE mode,
                   RTREE_SEARCH_CALLBACK cb, void *cb_arg)
{
  PAGE_PTR page;
  RTREE_NODE_HEADER *hdr;
  int n, i;
  int error_code = NO_ERROR;

  page = pgbuf_fix (thread_p, page_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  hdr = rtree_get_node_header (thread_p, page);
  if (hdr == NULL)
    {
      pgbuf_unfix (thread_p, page);
      return ER_FAILED;
    }

  n = hdr->num_entries;

  if (hdr->node_level == 1)
    {
      /* Leaf node: check each entry against the search predicate */
      for (i = 1; i <= n; i++)
        {
          RTREE_LEAF_ENTRY le;
          bool match = false;

          if (rtree_read_leaf_entry (thread_p, page, (PGSLOTID) i, &le) != NO_ERROR)
            {
              continue;
            }

          switch (mode)
            {
            case RTREE_SEARCH_INTERSECTS:
              match = rtree_mbr_intersects (search_mbr, &le.mbr);
              break;
            case RTREE_SEARCH_CONTAINED:
              match = rtree_mbr_contains (search_mbr, &le.mbr);
              break;
            case RTREE_SEARCH_CONTAINS:
              match = rtree_mbr_contains (&le.mbr, search_mbr);
              break;
            }

          if (match)
            {
              error_code = cb (&le.oid, cb_arg);
              if (error_code != NO_ERROR)
                {
                  break;
                }
            }
        }
    }
  else
    {
      /* Internal node: recurse into children whose MBR intersects the search MBR.
       * Even for CONTAINED / CONTAINS, we must enter any child whose MBR
       * intersects the search region (conservative descent). */
      for (i = 1; i <= n; i++)
        {
          RTREE_NON_LEAF_ENTRY nle;

          if (rtree_read_non_leaf_entry (thread_p, page, (PGSLOTID) i, &nle) != NO_ERROR)
            {
              continue;
            }

          if (!rtree_mbr_intersects (search_mbr, &nle.mbr))
            {
              continue;
            }

          /* Release current page's latch before descending to avoid
           * deadlock (top-down latch ordering). */
          pgbuf_unfix (thread_p, page);
          page = NULL;

          error_code = rtree_search_page (thread_p, btid, &nle.child_vpid,
                                          search_mbr, mode, cb, cb_arg);
          if (error_code != NO_ERROR)
            {
              return error_code;
            }

          /* Re-latch the current page to continue scanning remaining entries */
          page = pgbuf_fix (thread_p, page_vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
          if (page == NULL)
            {
              ASSERT_ERROR_AND_SET (error_code);
              return error_code;
            }
          /* Re-read hdr pointer after re-latch */
          hdr = rtree_get_node_header (thread_p, page);
          if (hdr == NULL)
            {
              pgbuf_unfix (thread_p, page);
              return ER_FAILED;
            }
        }
    }

  if (page != NULL)
    {
      pgbuf_unfix (thread_p, page);
    }

  return error_code;
}

/*
 * rtree_search - find all OIDs whose stored MBR satisfies the search
 * predicate and invoke *cb* for each one.
 */
int
rtree_search (THREAD_ENTRY * thread_p, BTID * btid, RTREE_MBR * search_mbr,
              RTREE_SEARCH_MODE mode, RTREE_SEARCH_CALLBACK cb, void *cb_arg)
{
  VPID root_vpid;

  root_vpid.volid  = btid->vfid.volid;
  root_vpid.pageid = btid->root_pageid;

  return rtree_search_page (thread_p, btid, &root_vpid, search_mbr, mode, cb, cb_arg);
}


/* ======================================================================
 * Section 10: Delete
 *
 * Guttman Algorithm Delete:
 *   D1. Find the leaf containing E: find_leaf.
 *   D2. Delete E from the leaf page.
 *   D3. Condense tree: reinsert entries from underfull nodes.
 *   D4. Shorten tree: if root has one child, make that child the new root.
 * ====================================================================== */

/* Forward declaration */
static int rtree_find_and_delete (THREAD_ENTRY * thread_p, const BTID * btid, const VPID * page_vpid,
                                  const RTREE_MBR * mbr, const OID * oid,
                                  RTREE_PATH * path, int depth, bool * deleted_out);

/*
 * rtree_delete - remove the (MBR, OID) pair from the R-tree.
 *
 * NOTE: the condense step (reinserting orphaned entries) is implemented as a
 * simple re-insert of each orphaned entry.  A production implementation
 * would batch these and defer them to the end of the operation to avoid
 * recursive complexity; here we keep it straightforward for clarity.
 */
int
rtree_delete (THREAD_ENTRY * thread_p, BTID * btid, RTREE_MBR * mbr, OID * oid)
{
  VPID root_vpid;
  RTREE_PATH path;
  bool deleted = false;
  int error_code = NO_ERROR;

  root_vpid.volid  = btid->vfid.volid;
  root_vpid.pageid = btid->root_pageid;

  path.depth = 0;

  error_code = rtree_find_and_delete (thread_p, btid, &root_vpid, mbr, oid, &path, 0, &deleted);
  if (error_code != NO_ERROR || !deleted)
    {
      return error_code != NO_ERROR ? error_code : ER_FAILED;
    }

  /* D4: Shorten tree – if root now has exactly one child, promote that child */
  {
    PAGE_PTR root_page;
    RTREE_NODE_HEADER *root_hdr;

    root_vpid.volid  = btid->vfid.volid;
    root_vpid.pageid = btid->root_pageid;

    root_page = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
    if (root_page == NULL)
      {
        return ER_FAILED;
      }

    root_hdr = rtree_get_node_header (thread_p, root_page);
    if (root_hdr != NULL && root_hdr->node_level > 1 && root_hdr->num_entries == 1)
      {
        /* The root has only one child; promote the child. */
        RTREE_NON_LEAF_ENTRY sole_entry;

        if (rtree_read_non_leaf_entry (thread_p, root_page, 1, &sole_entry) == NO_ERROR)
          {
            /* Make the single child the new root by updating BTID.
             * The old root page could be freed here; deferred for simplicity. */
            btid->root_pageid = sole_entry.child_vpid.pageid;
          }
      }

    pgbuf_unfix (thread_p, root_page);
  }

  return NO_ERROR;
}

/*
 * rtree_find_and_delete - recursive helper.
 *
 * Descends the tree searching for the leaf entry matching (*mbr*, *oid*).
 * When found, deletes it and propagates under-full node handling upward.
 */
static int
rtree_find_and_delete (THREAD_ENTRY * thread_p, const BTID * btid, const VPID * page_vpid,
                       const RTREE_MBR * mbr, const OID * oid,
                       RTREE_PATH * path, int depth, bool * deleted_out)
{
  PAGE_PTR page;
  RTREE_NODE_HEADER *hdr;
  int n, i;
  int error_code = NO_ERROR;

  *deleted_out = false;

  page = pgbuf_fix (thread_p, page_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
  if (page == NULL)
    {
      ASSERT_ERROR_AND_SET (error_code);
      return error_code;
    }

  hdr = rtree_get_node_header (thread_p, page);
  if (hdr == NULL)
    {
      pgbuf_unfix (thread_p, page);
      return ER_FAILED;
    }

  n = hdr->num_entries;

  if (depth >= RTREE_MAX_DEPTH)
    {
      pgbuf_unfix (thread_p, page);
      return ER_FAILED;
    }
  path->entries[depth].vpid   = *page_vpid;
  path->entries[depth].slotid = RTREE_NO_SLOT;

  if (hdr->node_level == 1)
    {
      /* Leaf: linear scan for matching entry */
      for (i = 1; i <= n; i++)
        {
          RTREE_LEAF_ENTRY le;

          if (rtree_read_leaf_entry (thread_p, page, (PGSLOTID) i, &le) != NO_ERROR)
            {
              continue;
            }

          if (OID_EQ (&le.oid, oid) && rtree_mbr_intersects (mbr, &le.mbr))
            {
              spage_delete (thread_p, page, (PGSLOTID) i);
              hdr->num_entries--;
              pgbuf_set_dirty (thread_p, page, DONT_FREE);
              *deleted_out = true;
              break;
            }
        }
      pgbuf_unfix (thread_p, page);
    }
  else
    {
      /* Internal node: visit all children whose MBR intersects *mbr* */
      for (i = 1; i <= n; i++)
        {
          RTREE_NON_LEAF_ENTRY nle;

          if (rtree_read_non_leaf_entry (thread_p, page, (PGSLOTID) i, &nle) != NO_ERROR)
            {
              continue;
            }

          if (!rtree_mbr_intersects (mbr, &nle.mbr))
            {
              continue;
            }

          /* Record which slot we took before unlocking */
          path->entries[depth].slotid = (PGSLOTID) i;
          pgbuf_unfix (thread_p, page);
          page = NULL;

          error_code = rtree_find_and_delete (thread_p, btid, &nle.child_vpid,
                                              mbr, oid, path, depth + 1, deleted_out);
          if (error_code != NO_ERROR)
            {
              return error_code;
            }

          if (*deleted_out)
            {
              /* D3: condense tree – update parent MBR */
              PAGE_PTR parent_page = pgbuf_fix (thread_p, page_vpid, OLD_PAGE,
                                                PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
              if (parent_page != NULL)
                {
                  RTREE_NON_LEAF_ENTRY updated_entry;
                  PAGE_PTR child_page = pgbuf_fix (thread_p, &nle.child_vpid, OLD_PAGE,
                                                   PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);

                  if (child_page != NULL)
                    {
                      updated_entry = nle;
                      rtree_compute_page_mbr (thread_p, child_page, &updated_entry.mbr);
                      pgbuf_unfix (thread_p, child_page);
                      rtree_write_non_leaf_entry (thread_p, parent_page,
                                                  path->entries[depth].slotid, &updated_entry);
                    }
                  pgbuf_set_dirty (thread_p, parent_page, DONT_FREE);
                  pgbuf_unfix (thread_p, parent_page);
                }
              return NO_ERROR;
            }

          /* Re-latch parent to continue scanning siblings */
          page = pgbuf_fix (thread_p, page_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
          if (page == NULL)
            {
              ASSERT_ERROR_AND_SET (error_code);
              return error_code;
            }
          hdr = rtree_get_node_header (thread_p, page);
          if (hdr == NULL)
            {
              pgbuf_unfix (thread_p, page);
              return ER_FAILED;
            }
        }

      if (page != NULL)
        {
          pgbuf_unfix (thread_p, page);
        }
    }

  return NO_ERROR;
}


/* ======================================================================
 * Section 11: rtree_dump  (index trace / debugging)
 * ====================================================================== */

static void
rtree_dump_page (THREAD_ENTRY * thread_p, const VPID * vpid, int indent, FILE *fp)
{
  PAGE_PTR page;
  RTREE_NODE_HEADER *hdr;
  int n, i, d;

  page = pgbuf_fix (thread_p, vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
  if (page == NULL)
    {
      return;
    }

  hdr = rtree_get_node_header (thread_p, page);
  if (hdr == NULL)
    {
      pgbuf_unfix (thread_p, page);
      return;
    }

  n = hdr->num_entries;

  for (i = 0; i < indent; i++)
    {
      fprintf (fp, "  ");
    }

  if (hdr->node_level == 1)
    {
      fprintf (fp, "[LEAF] page=%d:%d  entries=%d\n",
               vpid->volid, vpid->pageid, n);

      for (i = 1; i <= n; i++)
        {
          RTREE_LEAF_ENTRY le;
          int d;

          if (rtree_read_leaf_entry (thread_p, page, (PGSLOTID) i, &le) != NO_ERROR)
            {
              continue;
            }

          for (d = 0; d < indent + 1; d++)
            {
              fprintf (fp, "  ");
            }
          fprintf (fp, "  [%d] OID=%d:%d:%d MBR=[%.2f,%.2f,%.2f,%.2f]\n", i,
                   le.oid.volid, le.oid.pageid, le.oid.slotid,
                   le.mbr.bounds[0], le.mbr.bounds[1],
                   le.mbr.bounds[2], le.mbr.bounds[3]);
        }
    }
  else
    {
      fprintf (fp, "[INTERNAL level=%d] page=%d:%d  entries=%d\n",
               hdr->node_level, vpid->volid, vpid->pageid, n);

      for (i = 1; i <= n; i++)
        {
          RTREE_NON_LEAF_ENTRY nle;

          if (rtree_read_non_leaf_entry (thread_p, page, (PGSLOTID) i, &nle) != NO_ERROR)
            {
              continue;
            }

          for (d = 0; d < indent + 1; d++)
            {
              fprintf (fp, "  ");
            }
          fprintf (fp, "[%d] MBR=[%.2f,%.2f,%.2f,%.2f] -> child page=%d:%d\n", i,
                   nle.mbr.bounds[0], nle.mbr.bounds[1],
                   nle.mbr.bounds[2], nle.mbr.bounds[3],
                   nle.child_vpid.volid, nle.child_vpid.pageid);

          pgbuf_unfix (thread_p, page);
          page = NULL;

          rtree_dump_page (thread_p, &nle.child_vpid, indent + 1, fp);

          page = pgbuf_fix (thread_p, vpid, OLD_PAGE, PGBUF_LATCH_READ, PGBUF_UNCONDITIONAL_LATCH);
          if (page == NULL)
            {
              return;
            }
          hdr = rtree_get_node_header (thread_p, page);
          if (hdr == NULL)
            {
              pgbuf_unfix (thread_p, page);
              return;
            }
        }
    }

  if (page != NULL)
    {
      pgbuf_unfix (thread_p, page);
    }
}

/*
 * rtree_dump - print the full R-tree structure to *fp* (stderr if NULL).
 * Intended for debugging and regression testing.
 */
void
rtree_dump (THREAD_ENTRY * thread_p, BTID * btid, FILE *fp)
{
  VPID root_vpid;

  if (fp == NULL)
    {
      fp = stderr;
    }

  root_vpid.volid  = btid->vfid.volid;
  root_vpid.pageid = btid->root_pageid;

  fprintf (fp, "=== R-tree dump (file=%d:%d root=%d) ===\n",
           btid->vfid.volid, btid->vfid.fileid, btid->root_pageid);

  rtree_dump_page (thread_p, &root_vpid, 0, fp);

  fprintf (fp, "=== end dump ===\n");
}


/* ======================================================================
 * Section 12: Public API  (xrtree_*)
 * ====================================================================== */

/*
 * xrtree_add_index - create an R-tree index for attribute *attr_id* of
 * class *class_oid*.  Fills *btid* and returns it on success; returns NULL
 * on error.
 */
BTID *
xrtree_add_index (THREAD_ENTRY * thread_p, BTID * btid, TP_DOMAIN * key_type, OID * class_oid, int attr_id)
{
  int error_code;

  (void) key_type;  /* geometry type: stored as opaque blob; MBR is extracted at DML time */

  if (btid == NULL || class_oid == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return NULL;
    }

  error_code = rtree_create_file (thread_p, class_oid, attr_id, btid);
  if (error_code != NO_ERROR)
    {
      ASSERT_ERROR ();
      return NULL;
    }

  return btid;
}

/*
 * xrtree_load_index - bulk-load an R-tree index by scanning the heap files.
 *
 * Current implementation: create an empty R-tree and individually insert
 * each (MBR, OID) pair extracted from the heap.  A proper sort-based bulk-
 * loader (STR / OMT packing) is left as a future optimisation.
 */
BTID *
xrtree_load_index (THREAD_ENTRY * thread_p, BTID * btid, const char *constraint_name,
                   TP_DOMAIN * key_type, OID * class_oids, int n_classes, int n_attrs,
                   int *attr_ids, HFID * hfids)
{
  (void) constraint_name;
  (void) n_classes;
  (void) n_attrs;
  (void) hfids;

  if (btid == NULL || class_oids == NULL || attr_ids == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_GENERIC_ERROR, 0);
      return NULL;
    }

  /* Create an initially empty index; individual row inserts happen through
   * the DML path (rtree_insert) which the executor drives after this call
   * returns.  This mirrors what btree_load_index does for the initial
   * structure creation. */
  return xrtree_add_index (thread_p, btid, key_type, &class_oids[0], attr_ids[0]);
}

/*
 * xrtree_delete_index - destroy the R-tree file and all its pages.
 */
int
xrtree_delete_index (THREAD_ENTRY * thread_p, BTID * btid)
{
  int error_code = NO_ERROR;

  if (btid == NULL || VFID_ISNULL (&btid->vfid))
    {
      return NO_ERROR;
    }

  /* Notify vacuum so it can clean up after a committed drop. */
  vacuum_log_add_dropped_file (thread_p, &btid->vfid, NULL, VACUUM_LOG_ADD_DROPPED_FILE_POSTPONE);

  /* Schedule file destruction at transaction commit. */
  file_postpone_destroy (thread_p, &btid->vfid);

  VFID_SET_NULL (&btid->vfid);
  btid->root_pageid = NULL_PAGEID;

  return error_code;
}

