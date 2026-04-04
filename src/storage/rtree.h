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
 * rtree.h - R-tree spatial index manager (interface)
 *
 * Implements Guttman's R-tree (1984) with quadratic split.
 * Spatial data is stored as 2D Minimum Bounding Rectangles (MBRs).
 * Each index page is a slotted page:
 *   slot 0       : RTREE_NODE_HEADER  (or RTREE_ROOT_HEADER for root)
 *   slot 1..N    : RTREE_ENTRY records (leaf: MBR+OID, non-leaf: MBR+VPID)
 *
 * Storage reuses FILE_BTREE and PAGE_BTREE to avoid disk format changes.
 */

#ifndef _RTREE_H_
#define _RTREE_H_

#ident "$Id$"

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "config.h"

#include "oid.h"
#include "object_domain.h"
#include "storage_common.h"
#include "thread_compat.hpp"

/* ======================================================================
 * Constants
 * ====================================================================== */

/* Number of spatial dimensions supported */
#define RTREE_NDIMS  2

/* Minimum fill factor: a node must have at least RTREE_MIN_ENTRIES after
 * split.  Guttman recommends m >= 2, and m <= M/2. */
#define RTREE_MIN_ENTRIES  2

/* Header slot number (same convention as btree) */
#define RTREE_HEADER_SLOTID  0

/* Alignment for R-tree pages (same as btree) */
#define RTREE_MAX_ALIGN  INT_ALIGNMENT

/* Slot type for R-tree pages */
#define RTREE_SLOT_TYPE  UNANCHORED_KEEP_SEQUENCE

/* ======================================================================
 * MBR (Minimum Bounding Rectangle)
 * ====================================================================== */

/*
 * RTREE_MBR - axis-aligned bounding box in RTREE_NDIMS dimensions.
 *
 * bounds layout: [dim0_min, dim1_min, ..., dim0_max, dim1_max, ...]
 * For 2D:        [xmin, ymin, xmax, ymax]
 */
typedef struct rtree_mbr RTREE_MBR;
struct rtree_mbr
{
  double bounds[RTREE_NDIMS * 2];
};

#define RTREE_MBR_SIZE  sizeof (RTREE_MBR)	/* 32 bytes for 2D */

/* Access helpers */
#define RTREE_MBR_MIN(mbr, dim)  ((mbr)->bounds[(dim)])
#define RTREE_MBR_MAX(mbr, dim)  ((mbr)->bounds[RTREE_NDIMS + (dim)])

/* Set an MBR to "infinite empty" (inverse of universe) so any union
 * enlarges it. */
#define RTREE_MBR_SET_EMPTY(mbr)  \
  do {  \
    int _d;  \
    for (_d = 0; _d < RTREE_NDIMS; _d++) {  \
      RTREE_MBR_MIN (mbr, _d) =  1e300;  \
      RTREE_MBR_MAX (mbr, _d) = -1e300;  \
    }  \
  } while (0)

/* ======================================================================
 * On-disk node structures
 * ====================================================================== */

/*
 * RTREE_NODE_HEADER - stored in slot 0 of every non-root page.
 *
 * For root pages use RTREE_ROOT_HEADER which embeds this as first field.
 */
typedef struct rtree_node_header RTREE_NODE_HEADER;
struct rtree_node_header
{
  VPID parent_vpid;		/* parent page (NULL for root) */
  short node_level;		/* 1 = leaf, >1 = internal node */
  short num_entries;		/* number of entries currently in page */
  int padding;			/* reserved, keep alignment */
};

/*
 * RTREE_ROOT_HEADER - slot 0 of the root page only.
 * Extends RTREE_NODE_HEADER with tree-level statistics.
 */
typedef struct rtree_root_header RTREE_ROOT_HEADER;
struct rtree_root_header
{
  RTREE_NODE_HEADER node;	/* must be first */
  INT64 num_oids;		/* total OIDs stored in the tree */
  OID class_oid;		/* owning class */
  int attr_id;			/* indexed attribute id */
};

/* ======================================================================
 * On-disk entry record formats
 * ====================================================================== */

/*
 * RTREE_LEAF_ENTRY - one entry in a leaf page.
 * Size: RTREE_MBR_SIZE + OR_OID_SIZE = 32 + 8 = 40 bytes.
 */
typedef struct rtree_leaf_entry RTREE_LEAF_ENTRY;
struct rtree_leaf_entry
{
  RTREE_MBR mbr;		/* bounding box of the indexed object */
  OID oid;			/* object identifier */
  int padding;			/* alignment to 8 bytes */
};

/*
 * RTREE_NON_LEAF_ENTRY - one entry in an internal (non-leaf) page.
 * Size: RTREE_MBR_SIZE + DISK_VPID_SIZE (6) + 2 padding = 40 bytes.
 */
typedef struct rtree_non_leaf_entry RTREE_NON_LEAF_ENTRY;
struct rtree_non_leaf_entry
{
  RTREE_MBR mbr;		/* MBR covering entire subtree */
  VPID child_vpid;		/* child page pointer */
  short padding;
};

#define RTREE_LEAF_ENTRY_SIZE     sizeof (RTREE_LEAF_ENTRY)
#define RTREE_NON_LEAF_ENTRY_SIZE sizeof (RTREE_NON_LEAF_ENTRY)

/* ======================================================================
 * Capacity computation
 * ====================================================================== */

/*
 * Maximum number of leaf entries per page.
 * Reserves space for spage header, node header slot, and one slot overhead
 * per entry.
 *
 * spage slot overhead is sizeof(SPAGE_SLOT) = 4 bytes per record.
 */
#define RTREE_MAX_LEAF_ENTRIES  \
  (int)((DB_PAGESIZE  \
         - DB_ALIGN (SPAGE_HEADER_SIZE, RTREE_MAX_ALIGN)  \
         - DB_ALIGN ((int) sizeof (RTREE_NODE_HEADER), RTREE_MAX_ALIGN) \
         - 4 /* slot 0 overhead */) \
        / (RTREE_LEAF_ENTRY_SIZE + 4 /* per-slot overhead */))

#define RTREE_MAX_NON_LEAF_ENTRIES  \
  (int)((DB_PAGESIZE  \
         - DB_ALIGN (SPAGE_HEADER_SIZE, RTREE_MAX_ALIGN)  \
         - DB_ALIGN ((int) sizeof (RTREE_NODE_HEADER), RTREE_MAX_ALIGN) \
         - 4) \
        / (RTREE_NON_LEAF_ENTRY_SIZE + 4))

/* ======================================================================
 * Search predicate
 * ====================================================================== */

typedef enum
{
  RTREE_SEARCH_INTERSECTS = 0,	/* MBR intersects search region */
  RTREE_SEARCH_CONTAINED,	/* MBR is fully inside search region */
  RTREE_SEARCH_CONTAINS		/* MBR fully contains search region */
} RTREE_SEARCH_MODE;

/* Callback invoked for each matching OID during a range search */
typedef int (*RTREE_SEARCH_CALLBACK) (OID * oid, void *arg);

/* ======================================================================
 * Public API
 * ====================================================================== */

/* Create and initialise a new R-tree file; returns btid on success */
extern BTID *xrtree_add_index (THREAD_ENTRY * thread_p, BTID * btid, TP_DOMAIN * key_type, OID * class_oid,
			       int attr_id);

/* Bulk-load an R-tree index from heap files */
extern BTID *xrtree_load_index (THREAD_ENTRY * thread_p, BTID * btid, const char *constraint_name,
				TP_DOMAIN * key_type, OID * class_oids, int n_classes, int n_attrs,
				int *attr_ids, HFID * hfids);

/* Destroy the R-tree file */
extern int xrtree_delete_index (THREAD_ENTRY * thread_p, BTID * btid);

/* Insert one (MBR, OID) pair into an existing R-tree */
extern int rtree_insert (THREAD_ENTRY * thread_p, BTID * btid, RTREE_MBR * mbr, OID * oid);

/* Delete one (MBR, OID) pair from an existing R-tree */
extern int rtree_delete (THREAD_ENTRY * thread_p, BTID * btid, RTREE_MBR * mbr, OID * oid);

/* Search: invoke callback for every OID whose MBR satisfies mode vs search_mbr */
extern int rtree_search (THREAD_ENTRY * thread_p, BTID * btid, RTREE_MBR * search_mbr, RTREE_SEARCH_MODE mode,
			 RTREE_SEARCH_CALLBACK cb, void *cb_arg);

/* MBR utility functions (also useful for query layer) */
extern double rtree_mbr_area (const RTREE_MBR * mbr);
extern double rtree_mbr_enlargement (const RTREE_MBR * mbr, const RTREE_MBR * add);
extern void rtree_mbr_union (RTREE_MBR * result, const RTREE_MBR * a, const RTREE_MBR * b);
extern bool rtree_mbr_intersects (const RTREE_MBR * a, const RTREE_MBR * b);
extern bool rtree_mbr_contains (const RTREE_MBR * outer, const RTREE_MBR * inner);
extern void rtree_mbr_from_db_spatial (RTREE_MBR * mbr, const void *geom);

/* Initialise a new R-tree page (FILE_INIT_PAGE_FUNC signature) */
extern int rtree_initialize_new_page (THREAD_ENTRY * thread_p, PAGE_PTR page, void *args);

/* Dump the full R-tree structure for debugging / tracing */
extern void rtree_dump (THREAD_ENTRY * thread_p, BTID * btid, FILE * fp);

#endif /* _RTREE_H_ */
