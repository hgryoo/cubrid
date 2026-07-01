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
 * lock_manager_internal.hpp - Internal definitions shared across the lock
 *                             manager translation units.
 *
 * Not part of the public lock manager interface. Exposes the lock manager's
 * internal state (lk_Gl, LK_TRAN_LOCK, LK_WFG_*, ...) and the helpers that
 * live in sibling .cpp files (daemon wiring, event-log dumps, conn_entry
 * resolution) to lock_manager.c without inflating the public API surface.
 */

#ifndef _LOCK_MANAGER_INTERNAL_HPP_
#define _LOCK_MANAGER_INTERNAL_HPP_

#ident "$Id$"

#include "config.h"

#if defined(SERVER_MODE)

#include "lock_manager.h"
#include "lock_manager_resource_table.hpp"
#include "lock_free.h"
#include "lock_table.h"
#include "thread_compat.hpp"

#include <array>
#include <atomic>
#include <pthread.h>
#include <sys/time.h>

/*
 * Transaction Lock Entry Structure
 */
typedef struct lk_tran_lock LK_TRAN_LOCK;
struct lk_tran_lock
{
  /* transaction lock hold lists */
  pthread_mutex_t hold_mutex;	/* mutex for hold lists */
  LK_ENTRY *inst_hold_list;	/* instance lock hold list */
  LK_ENTRY *class_hold_list;	/* class lock hold list */
  LK_ENTRY *root_class_hold;	/* root class lock hold */
  LK_ENTRY *lk_entry_pool;	/* local pool of lock entries which can be used with no synchronization. */
  int lk_entry_pool_count;	/* Current count of lock entries in local pool. */
  int inst_hold_count;		/* # of entries in inst_hold_list */
  int class_hold_count;		/* # of entries in class_hold_list */

  LK_ENTRY *waiting;		/* waiting lock entry */

  /* non two phase lock list */
  pthread_mutex_t non2pl_mutex;	/* mutex for non2pl_list */
  LK_ENTRY *non2pl_list;	/* non2pl list */
  int num_incons_non2pl;	/* # of inconsistent non2pl */

  /* lock escalation related fields */
  bool lock_escalation_on;

  /* locking on manual duration */
  bool is_instant_duration;
};

/* TWFG (transaction wait-for graph) entry and edge */
typedef struct lk_WFG_node LK_WFG_NODE;
struct lk_WFG_node
{
  int first_edge;
  bool candidate;
  int current;
  int ancestor;
  INT64 thrd_wait_stime;
  int tran_edge_seq_num;
  bool checked_by_deadlock_detector;
  bool DL_victim;
};

typedef struct lk_WFG_edge LK_WFG_EDGE;
struct lk_WFG_edge
{
  int to_tran_index;
  int edge_seq_num;
  int holder_flag;
  int next;
  INT64 edge_wait_stime;
  LK_ENTRY *holder;
  LK_ENTRY *waiter;
};

/*
 * Lock Manager Global Data Structure
 */
typedef struct lk_global_data LK_GLOBAL_DATA;
struct lk_global_data
{
  /* object lock table including hash table */
  int max_obj_locks;		/* max # of object locks */

  lock_manager::resource_table m_obj_hash_table;
  LF_FREELIST obj_free_entry_list;

  /* transaction lock table */
  int num_trans;		/* # of transactions */
  LK_TRAN_LOCK *tran_lock_table;	/* transaction lock hold table */

  /* deadlock detection related fields */
  pthread_mutex_t DL_detection_mutex;
  struct timeval last_deadlock_run;	/* last deadlock detection time */
  LK_WFG_NODE *TWFG_node;	/* transaction WFG node */
  LK_WFG_EDGE *TWFG_edge;	/* transaction WFG edge */
  int max_TWFG_edge;
  int TWFG_free_edge_idx;
  int global_edge_seq_num;

  /* miscellaneous things */
  short no_victim_case_count;
  bool verbose_mode;
  // *INDENT-OFF*
  std::atomic_int deadlock_and_timeout_detector;
  // *INDENT-ON*
#if defined(LK_DUMP)
  bool dump_level;
#endif				/* LK_DUMP */

  lk_global_data ();
};

extern LK_GLOBAL_DATA lk_Gl;

/* shared internal constants and macros */
#define MAX_NUM_LOCKS_DUMP_TO_EVENT_LOG 100

#define SET_EMULATE_THREAD_WITH_LOCK_ENTRY(th,lock_entry) \
  do \
    { \
      THREAD_ENTRY *locked_thread_entry_p; \
      assert ((th)->emulate_tid == thread_id_t ()); \
      locked_thread_entry_p = logtb_find_thread_by_tran_index ((lock_entry)->tran_index); \
      if (locked_thread_entry_p != NULL) \
	{ \
	  (th)->emulate_tid = locked_thread_entry_p->get_id (); \
	} \
    } \
   while (0)

#define CLEAR_EMULATE_THREAD(th) \
  do \
    { \
      (th)->emulate_tid = thread_id_t (); \
    } \
   while (0)

/* shared types used by daemon-side helpers */
const size_t DEFAULT_LOCK_WAITING_THREAD_ARRAY_SIZE = 10;
// *INDENT-OFF*
using tran_lock_waiters_array_type = std::array<THREAD_ENTRY *, DEFAULT_LOCK_WAITING_THREAD_ARRAY_SIZE>;
// *INDENT-ON*

/* lock_manager.c state-machine entry points reachable from sibling TUs. */
extern bool lock_force_timeout_expired_wait_transactions (void *thrd_entry);
extern bool lock_is_local_deadlock_detection_interval_up (void);
extern void lock_detect_local_deadlock (THREAD_ENTRY * thread_p);
extern bool lock_wakeup_deadlock_victim_timeout (int tran_index);

/* Deadlock-detect daemon and thread-map helpers (lock_manager_daemon.cpp). */
extern void lk_deadlock_daemon_start (void);
extern void lk_deadlock_daemon_stop (void);
extern void lk_get_transaction_lock_waiting_threads (int tran_index, tran_lock_waiters_array_type & tran_lock_waiters,
                                                     size_t & count);
extern void lk_victimize_first_thread (void);

/* conn_entry helper (lock_manager_conn.cpp). */
extern int lk_resolve_loaddb_tran_index (THREAD_ENTRY * thread_p);

/* event_log open/close (src/base/event_log.h). Forward declared so we do not
 * have to pull event_log.h's full include surface in here. Signatures must
 * match event_log.h exactly. */
extern FILE *event_log_start (THREAD_ENTRY * thread_p, const char *event_name);
extern void event_log_end (THREAD_ENTRY * thread_p);

/* event_log dump helpers (lock_manager_event_log.cpp). */
extern void lock_event_log_tran_locks (THREAD_ENTRY * thread_p, FILE * log_fp, int tran_index);
extern void lock_event_log_deadlock_locks (THREAD_ENTRY * thread_p, FILE * log_fp, int tran_index, bool log_trunc,
					   int log_num_entries, LK_ENTRY ** log_entries);
extern void lock_event_log_blocked_lock (THREAD_ENTRY * thread_p, FILE * log_fp, LK_ENTRY * entry);
extern void lock_event_log_blocking_locks (THREAD_ENTRY * thread_p, FILE * log_fp, LK_ENTRY * wait_entry);
extern void lock_event_log_lock_info (THREAD_ENTRY * thread_p, FILE * log_fp, LK_ENTRY * entry);

/* xasl_id accessors on LK_ENTRY. Live alongside the event_log dumps because
 * they need xasl.h for the XASL_ID_* macros. */
extern void lock_event_set_tran_wait_entry (int tran_index, LK_ENTRY * entry);
extern void lock_event_set_xasl_id_to_entry (int tran_index, LK_ENTRY * entry);

#endif /* SERVER_MODE */

#endif /* _LOCK_MANAGER_INTERNAL_HPP_ */
