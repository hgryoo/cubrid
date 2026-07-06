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

//
// log_append - creating & appending log records
//

#ifndef _LOG_APPEND_HPP_
#define _LOG_APPEND_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Wrong module
#endif

#include "log_lsa.hpp"
#include "log_record.hpp"
#include "log_storage.hpp"
#include "memory_alloc.h"
#include "object_representation_constants.h"
#include "recovery.h"
#include "storage_common.h"
#include "log_compress.h"

#include <atomic>
#include <mutex>

// forward declarations
struct log_tdes;

typedef struct log_crumb LOG_CRUMB;
struct log_crumb
{
  int length;
  const void *data;
};

typedef struct log_data_addr LOG_DATA_ADDR;
struct log_data_addr
{
  using offset_type = PGLENGTH;

  const VFID *vfid;		/* File where the page belong or NULL when the page is not associated with a file */
  PAGE_PTR pgptr;
  offset_type offset;		/* Offset or slot */

  log_data_addr () = default;
  log_data_addr (const VFID *vfid, PAGE_PTR pgptr, PGLENGTH offset);
};
#define LOG_DATA_ADDR_INITIALIZER { NULL, NULL, 0 } // todo: remove me

enum LOG_PRIOR_LSA_LOCK
{
  LOG_PRIOR_LSA_WITHOUT_LOCK = 0,
  LOG_PRIOR_LSA_WITH_LOCK = 1
};

typedef struct log_append_info LOG_APPEND_INFO;
struct log_append_info
{
  int vdes;			/* Volume descriptor of active log */
  std::atomic<LOG_LSA> nxio_lsa;  /* Lowest log sequence number which has not been written to disk (for WAL). */
  /* todo - not really belonging here. should be part of page buffer. */
  /* N30 tier-2: record-aligned watermark up to which prior records are copied into the log page
   * buffer (readable). Published (release) by the drain, read (acquire) by pre-flush readers.
   * Invariant: nxio_lsa <= copied_lsa <= prior_lsa. Replaces the reader's unfenced append_lsa read. */
  std::atomic<LOG_LSA> copied_lsa;
  LOG_LSA prev_lsa;		/* Address of last append log record */
  LOG_PAGE *log_pgptr;		/* The log page which is fixed */

  bool appending_page_tde_encrypted;  /* true if a newly appended page has to be tde-encrypted */

  log_append_info ();
  log_append_info (const log_append_info &other);

  LOG_LSA get_nxio_lsa () const;
  void set_nxio_lsa (const LOG_LSA &next_io_lsa);
  LOG_LSA get_copied_lsa () const;
  void set_copied_lsa (const LOG_LSA &copied);
};

typedef struct log_prior_node LOG_PRIOR_NODE;
struct log_prior_node
{
  LOG_RECORD_HEADER log_header;
  LOG_LSA start_lsa;		/* for assertion */

  bool tde_encrypted;   /* whether the log page which'll contain this node has to be encrypted */

  /* data header info */
  int data_header_length;
  char *data_header;

  /* data info */
  int ulength;
  char *udata;
  int rlength;
  char *rdata;

  bool in_inflight;   /* N30 tier-1: true if this node was registered in the in-flight window
                       * (MVCC undo classes only, and not skipped at saturation). */
  void *inflight_reclaim;   /* N30 tier-1: opaque handle to the epoch-reclamation wrapper allocated
                             * at registration; the drain retires it instead of freeing the node
                             * directly, so a pinned reader can read the node view without UAF. */

  MVCCID oldest_visible_at_append;  /* N30 Phase-1 (Class-A deferral): the global oldest-visible
                             * MVCCID captured at this record's append, for the MVCC undo classes.
                             * The ordered header/vacuum effects are deferred to the completion
                             * processor (log_prior_complete_mvcc_effects) which runs at drain time
                             * in LSA order; capturing this one live input per node lets the
                             * processor reproduce the exact value append-time code would have read
                             * for the block-first record, so the deferral is behavior-identical. */

  LOG_PRIOR_NODE *next;
};

/* N30 tier-1 in-flight window: an LSA-ordered bounded FIFO ring of (start_lsa -> prior node) for
 * MVCC undo records, so a prev-version reader can read its target's undo image directly out of the
 * staged node (a tier-1 hit) instead of force-draining. Node lifetime moves from the drain's
 * immediate free to lockfree::tran epoch reclamation: a reader pins (start_tran) before reading a
 * slot and unpins (end_tran) after; the drain unlinks the slot then retires the node (deferred
 * free), so a node observed by a pinned reader is never reclaimed underneath it.
 *   register   - append path, under prior_lsa_mutex (LSA-monotonic single writer); allocs wrapper.
 *   retire     - drain, in append (LSA) order (FIFO); unlink slot then defer the free via epoch.
 *   pin_lookup - reader: start_tran, scan [head,tail) for start_lsa == lsa, re-validate; returns the
 *                pinned node on a hit (caller must unpin), or NULL on a miss (already unpinned).
 *   unpin      - reader: end_tran once done reading the returned node view. */
void log_prior_inflight_register (const LOG_LSA &start_lsa, LOG_PRIOR_NODE *node);
void log_prior_inflight_retire (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node);

/* N30 Phase-1 (Class-A deferral): the completion processor. Runs the deferred ordered MVCC/vacuum
 * header effects (vacuum block-boundary production, the prev_mvcc_op_log_lsa link write, and the
 * global log-header MVCC mutation) for one prior node, in LSA order, at drain time. Called by the
 * drain (logpb_append_prior_lsa_list) for every node just before its bytes are copied to the log
 * page buffer. Replaces the append-time mutation of the four global header fields; because the
 * drain walks nodes in exact LSA order and the one live input (global oldest-visible) is captured
 * per node at append, the sequence of header states is identical to today's append-time path. */
void log_prior_complete_mvcc_effects (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node);
LOG_PRIOR_NODE *log_prior_inflight_pin_lookup (THREAD_ENTRY *thread_p, const LOG_LSA &lsa);
void log_prior_inflight_unpin (THREAD_ENTRY *thread_p);

/* N30 tier-1 retired-backlog watchdog: live count of nodes retired to epoch reclamation but not yet
 * freed (= retire - reclaim). Read at statdump time into the PSTAT peek gauge. A sustained-high value
 * signals reclamation is not keeping up — a stall regression, or a long-lived reader pinning the
 * epoch. Healthy steady state is small (the live in-flight window). */
INT64 log_prior_inflight_backlog ();

typedef struct log_prior_lsa_info LOG_PRIOR_LSA_INFO;
struct log_prior_lsa_info
{
  LOG_LSA prior_lsa;
  LOG_LSA prev_lsa;

  /* list */
  LOG_PRIOR_NODE *prior_list_header;
  LOG_PRIOR_NODE *prior_list_tail;

  INT64 list_size;		/* bytes */

  /* flush list */
  LOG_PRIOR_NODE *prior_flush_list_header;

  std::mutex prior_lsa_mutex;

  log_prior_lsa_info ();
};

//
// log record partial updates logging
//
using log_rv_record_flag_type = log_data_addr::offset_type;
const log_rv_record_flag_type LOG_RV_RECORD_INSERT = (log_rv_record_flag_type) 0x8000;
const log_rv_record_flag_type LOG_RV_RECORD_DELETE = 0x4000;
const log_rv_record_flag_type LOG_RV_RECORD_UPDATE_ALL = (log_rv_record_flag_type) 0xC000;
const log_rv_record_flag_type LOG_RV_RECORD_UPDATE_PARTIAL = 0x0000;
const log_rv_record_flag_type LOG_RV_RECORD_MODIFY_MASK = (log_rv_record_flag_type) 0xC000;

inline bool LOG_RV_RECORD_IS_INSERT (log_rv_record_flag_type flags);
inline bool LOG_RV_RECORD_IS_DELETE (log_rv_record_flag_type flags);
inline bool LOG_RV_RECORD_IS_UPDATE_ALL (log_rv_record_flag_type flags);
inline bool LOG_RV_RECORD_IS_UPDATE_PARTIAL (log_rv_record_flag_type flags);
inline void LOG_RV_RECORD_SET_MODIFY_MODE (log_data_addr *addr, log_rv_record_flag_type mode);
constexpr size_t LOG_RV_RECORD_UPDPARTIAL_ALIGNED_SIZE (size_t new_data_size);

void LOG_RESET_APPEND_LSA (const LOG_LSA *lsa);
void LOG_RESET_PREV_LSA (const LOG_LSA *lsa);
char *LOG_APPEND_PTR ();

bool log_prior_has_worker_log_records (THREAD_ENTRY *thread_p);
LOG_PRIOR_NODE *prior_lsa_alloc_and_copy_data (THREAD_ENTRY *thread_p, LOG_RECTYPE rec_type, LOG_RCVINDEX rcvindex,
    LOG_DATA_ADDR *addr, int ulength, const char *udata, int rlength, const char *rdata);
LOG_PRIOR_NODE *prior_lsa_alloc_and_copy_crumbs (THREAD_ENTRY *thread_p, LOG_RECTYPE rec_type, LOG_RCVINDEX rcvindex,
    LOG_DATA_ADDR *addr, const int num_ucrumbs, const LOG_CRUMB *ucrumbs, const int num_rcrumbs,
    const LOG_CRUMB *rcrumbs);
LOG_LSA prior_lsa_next_record (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node, log_tdes *tdes);
LOG_LSA prior_lsa_next_record_with_lock (THREAD_ENTRY *thread_p, LOG_PRIOR_NODE *node, log_tdes *tdes);
int prior_set_tde_encrypted (log_prior_node *node, LOG_RCVINDEX recvindex);
bool prior_is_tde_encrypted (const log_prior_node *node);
void log_append_init_zip ();
void log_append_final_zip ();
extern LOG_ZIP *log_append_get_zip_undo (THREAD_ENTRY *thread_p);
extern LOG_ZIP *log_append_get_zip_redo (THREAD_ENTRY *thread_p);

// todo - move to header of log page buffer
size_t logpb_get_memsize ();

extern bool log_Zip_support;
extern int log_Zip_min_size_to_compress;

//////////////////////////////////////////////////////////////////////////
//
// Inline/templates
//
//////////////////////////////////////////////////////////////////////////

bool
LOG_RV_RECORD_IS_INSERT (log_rv_record_flag_type flags)
{
  return (flags & LOG_RV_RECORD_MODIFY_MASK) == LOG_RV_RECORD_INSERT;
}

bool
LOG_RV_RECORD_IS_DELETE (log_rv_record_flag_type flags)
{
  return (flags & LOG_RV_RECORD_MODIFY_MASK) == LOG_RV_RECORD_DELETE;
}

bool
LOG_RV_RECORD_IS_UPDATE_ALL (log_rv_record_flag_type flags)
{
  return (flags & LOG_RV_RECORD_MODIFY_MASK) == LOG_RV_RECORD_UPDATE_ALL;
}

bool
LOG_RV_RECORD_IS_UPDATE_PARTIAL (log_rv_record_flag_type flags)
{
  return (flags & LOG_RV_RECORD_MODIFY_MASK) == LOG_RV_RECORD_UPDATE_PARTIAL;
}

void
LOG_RV_RECORD_SET_MODIFY_MODE (log_data_addr *addr, log_rv_record_flag_type mode)
{
  addr->offset = (addr->offset & (~LOG_RV_RECORD_MODIFY_MASK)) | (mode);
}

constexpr size_t
LOG_RV_RECORD_UPDPARTIAL_ALIGNED_SIZE (size_t new_data_size)
{
  return DB_ALIGN (new_data_size + OR_SHORT_SIZE + 2 * OR_BYTE_SIZE, INT_ALIGNMENT);
}

#endif // !_LOG_APPEND_HPP_
