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
 * lock_manager_daemon.cpp - Deadlock-detect daemon and thread-map helpers
 *                           for the lock manager.
 *
 * Owns the cubthread::looper / cubthread::entry_callable_task /
 * cubthread::manager interactions and the map_entries wrappers used by the
 * lock manager (lk_get_transaction_lock_waiting_threads,
 * lk_victimize_first_thread), so that lock_manager.c does not need to see
 * the cubthread daemon / worker-pool headers.
 */

#ident "$Id$"

#include "config.h"

#if defined(SERVER_MODE)

#include "lock_manager_internal.hpp"

#include "boot_sr.h"
#include "error_manager.h"
#include "log_impl.h"
#include "server_support.h"
#include "system_parameter.h"
#include "thread_daemon.hpp"
#include "thread_entry.hpp"
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"

#include <chrono>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

// *INDENT-OFF*
static cubthread::daemon *lock_Deadlock_detect_daemon = NULL;

REGISTER_DAEMON (lock_deadlock_detect);

//
// lock_check_timeout_expired_and_count_suspended_mapfunc - map function over
//   live threads that:
//     (1) checks lock-wait timeouts and forces wake-up on expiry, and
//     (2) counts how many threads remain genuinely suspended on locks.
//
static void
lock_check_timeout_expired_and_count_suspended_mapfunc (THREAD_ENTRY & thread_ref, bool & stop_mapper,
                                                        size_t & suspend_count)
{
  (void) stop_mapper;  // suppress unused parameter warning

  // skip dead/free threads
  if (thread_ref.m_status == cubthread::entry::status::TS_DEAD
      || thread_ref.m_status == cubthread::entry::status::TS_FREE)
    {
      return;
    }
  if (thread_ref.lockwait == NULL)
    {
      return;
    }
  // suspended thread

  /* The transaction, for which the current thread is working, might be interrupted.
   * lock_force_timeout_expired_wait_transactions() performs not only interrupt but timeout checking.
   */
  if (!lock_force_timeout_expired_wait_transactions (&thread_ref))
    {
      // not timed out. count as suspended
      suspend_count++;
    }
}

//
// deadlock_detect_task_execute - body of the deadlock-detect daemon.
//
//  Description:
//    Runs at the daemon's looper interval (100 ms). On each tick:
//      (1) wake up any interrupted lock waiter,
//      (2) wake up any timed-out lock waiter,
//      (3) at PRM_ID_LK_RUN_DEADLOCK_INTERVAL, detect and resolve deadlocks.
//
static void
deadlock_detect_task_execute (cubthread::entry & thread_ref)
{
  if (!BO_IS_SERVER_RESTARTED ())
    {
      // wait for boot to finish
      return;
    }

  if (lk_Gl.deadlock_and_timeout_detector == 0)
    {
      // if none of the threads were suspended then just return
      return;
    }

  /* check if the lock-wait thread exists */
  size_t lock_wait_count = 0;
  thread_get_manager ()->map_entries (lock_check_timeout_expired_and_count_suspended_mapfunc, lock_wait_count);

  if (lock_is_local_deadlock_detection_interval_up () && lock_wait_count >= 2)
    {
      lock_detect_local_deadlock (&thread_ref);
    }
}

//
// lk_deadlock_daemon_start - create the deadlock-detect daemon.
//
void
lk_deadlock_daemon_start (void)
{
  assert (lock_Deadlock_detect_daemon == NULL);

  cubthread::looper looper = cubthread::looper (std::chrono::milliseconds (100));
  cubthread::entry_callable_task *daemon_task = new cubthread::entry_callable_task (deadlock_detect_task_execute);

  // create deadlock detect daemon thread
  lock_Deadlock_detect_daemon = cubthread::get_manager ()->create_daemon (looper, daemon_task, "deadlock-detect");
}

//
// lk_deadlock_daemon_stop - destroy the deadlock-detect daemon.
//
void
lk_deadlock_daemon_stop (void)
{
  cubthread::get_manager ()->destroy_daemon (lock_Deadlock_detect_daemon);
  lock_Deadlock_detect_daemon = NULL;
}

//
// lock_deadlock_detect_daemon_get_stats - public stats accessor for the
//   deadlock-detect daemon (declared in lock_manager.h).
//
void
lock_deadlock_detect_daemon_get_stats (UINT64 * statsp)
{
  if (lock_Deadlock_detect_daemon != NULL)
    {
      lock_Deadlock_detect_daemon->get_stats (statsp);
    }
}

//
// lock_get_transaction_lock_waiting_threads_mapfunc - map function used to
//   collect every thread that belongs to a given transaction and is currently
//   waiting on a lock.
//
static void
lock_get_transaction_lock_waiting_threads_mapfunc (THREAD_ENTRY & thread_ref, bool & stop_mapper, int tran_index,
                                                   tran_lock_waiters_array_type & tran_lock_waiters, size_t & count)
{
  (void) stop_mapper;  // suppress unused parameter warning

  if (thread_ref.tran_index != tran_index)
    {
      // not the right transaction
      return;
    }
  if (thread_ref.lockwait == NULL)
    {
      // not a lock waiter
      return;
    }
  tran_lock_waiters[count++] = &thread_ref;
}

//
// lk_get_transaction_lock_waiting_threads - collect all threads belonging to
//   transaction tran_index that are currently waiting on a lock. Wraps
//   thread_get_manager ()->map_entries so callers do not need the
//   thread_manager surface.
//
void
lk_get_transaction_lock_waiting_threads (int tran_index, tran_lock_waiters_array_type & tran_lock_waiters,
                                         size_t & count)
{
  thread_get_manager ()->map_entries (lock_get_transaction_lock_waiting_threads_mapfunc, tran_index,
                                      tran_lock_waiters, count);
}

//
// lock_victimize_first_thread_mapfunc - map function that walks live thread
//   entries and victimizes the first lock-waiter found.
//
static void
lock_victimize_first_thread_mapfunc (THREAD_ENTRY & thread_ref, bool & stop_mapper)
{
  if (thread_ref.lockwait == NULL)
    {
      return;
    }
  int tran_index = thread_ref.tran_index;
  if (lock_wakeup_deadlock_victim_timeout (tran_index))
    {
      stop_mapper = true;
      er_set (ER_NOTIFICATION_SEVERITY, ARG_FILE_LINE, ER_LK_NOTENOUGH_ACTIVE_THREADS, 3,
              (int) css_get_num_request_workers (), logtb_get_number_assigned_tran_indices (), tran_index);
    }
}

//
// lk_victimize_first_thread - time out the first lock waiter that
//   thread_get_manager ()->map_entries hands us. Used as a last-resort
//   release valve when no deadlock victim was selected but the
//   request-handler pool is fully saturated.
//
void
lk_victimize_first_thread (void)
{
  thread_get_manager ()->map_entries (lock_victimize_first_thread_mapfunc);
}
// *INDENT-ON*

#endif /* SERVER_MODE */
