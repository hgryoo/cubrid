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

#include "test_lock_manager_mock_runtime.hpp"

#include "lock_manager.h"
#include "log_impl.h"
#include "oid.h"
#include "perf_monitor.h"
#include "system_parameter.h"
#include "thread_entry_task.hpp"
#include "thread_manager.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <cstdlib>
#include <sstream>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

namespace test_lock_manager
{
  namespace
  {
    UINT64
    diff_perf_value (const UINT64 *current_stats, const UINT64 *base_stats, PERF_STAT_ID stat_id)
    {
      const int offset = pstat_Metadata[stat_id].start_offset;

      if (current_stats[offset] >= base_stats[offset])
	{
	  return current_stats[offset] - base_stats[offset];
	}
      return 0;
    }

    struct transaction_context
    {
      int txn_id;
      int tran_index;
      std::size_t participant_count;
      std::size_t committed_workers;
      bool active;
      std::mutex execution_mutex;
      std::mutex mutex;
    };

    struct worker_context
    {
      int worker_id;
      std::vector<operation> operations;
    };

    class cyclic_barrier
    {
      public:
	explicit cyclic_barrier (std::size_t participants)
	  : m_participants (participants)
	  , m_arrived (0)
	  , m_generation (0)
	{
	}

	void
	wait (void)
	{
	  std::unique_lock<std::mutex> lock (m_mutex);
	  const std::size_t generation = m_generation;

	  m_arrived++;
	  if (m_arrived == m_participants)
	    {
	      m_arrived = 0;
	      m_generation++;
	      m_cv.notify_all ();
	      return;
	    }

	  m_cv.wait (lock, [this, generation] ()
	  {
	    return m_generation != generation;
	  });
	}

      private:
	std::size_t m_participants;
	std::size_t m_arrived;
	std::size_t m_generation;
	std::mutex m_mutex;
	std::condition_variable m_cv;
    };

    OID
    make_real_oid (const mock_oid &mock)
    {
      OID oid;
      oid.volid = mock.volid;
      oid.pageid = mock.pageid;
      oid.slotid = mock.slotid;
      return oid;
    }

    LOCK
    to_lock (const std::string &lock_mode)
    {
      if (lock_mode == "S_LOCK")
	{
	  return S_LOCK;
	}
      if (lock_mode == "X_LOCK")
	{
	  return X_LOCK;
	}
      if (lock_mode == "IX_LOCK")
	{
	  return IX_LOCK;
	}
      if (lock_mode == "IS_LOCK")
	{
	  return IS_LOCK;
	}
      throw std::invalid_argument ("Unsupported lock mode: " + lock_mode);
    }

    int
    to_cond_flag (wait_kind kind)
    {
      return kind == wait_kind::unconditional ? LK_UNCOND_LOCK : LK_COND_LOCK;
    }

    void
    cleanup_transaction_locks (THREAD_ENTRY *thread_p)
    {
      lock_unlock_all (thread_p);
    }

    bool
    is_transaction_aborted (const LOG_TDES *tdes)
    {
      return tdes->tran_abort_reason != TRAN_NORMAL || LOG_ISTRAN_ABORTED (tdes);
    }
  } // namespace

  mock_runtime::mock_runtime ()
  {
    assert (cubthread::get_manager () != NULL);
  }

  simulation_stats
  mock_runtime::simulate (const std::vector<operation> &operations, const scenario_config &config)
  {
    simulation_stats stats { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "" };
    std::map<int, transaction_context> txns;
    std::map<int, std::set<int>> txn_participants;
    std::map<int, worker_context> workers;
    std::map<int, std::set<int>> barrier_participants;
    std::map<int, std::unique_ptr<cyclic_barrier>> barriers;
    std::mutex stats_mutex;
    std::mutex failure_mutex;
    std::exception_ptr thread_failure;
    std::mutex completion_mutex;
    std::condition_variable completion_cv;
    std::atomic<int> pending_workers (0);
    std::atomic<bool> stop_watchdog (false);
    std::unique_ptr<std::thread> watchdog_thread;
    int error = NO_ERROR;
    bool has_unconditional_wait = false;

    for (const operation &op : operations)
      {
	txn_participants[op.txn_id].insert (op.worker_id);
	if (op.wait_policy == wait_kind::unconditional)
	  {
	    has_unconditional_wait = true;
	  }
	if (op.kind == operation_kind::barrier)
	  {
	    barrier_participants[op.value].insert (op.worker_id);
	  }
      }

    int next_tran_index = 1;
    for (const std::pair<const int, std::set<int>> &entry : txn_participants)
      {
	transaction_context &txn = txns[entry.first];
	txn.txn_id = entry.first;
	txn.tran_index = next_tran_index++;
	txn.participant_count = entry.second.size ();
	txn.committed_workers = 0;
	txn.active = false;
      }

    for (const operation &op : operations)
      {
	worker_context &worker = workers[op.worker_id];
	worker.worker_id = op.worker_id;
	worker.operations.push_back (op);
      }

    for (const std::pair<const int, std::set<int>> &entry : barrier_participants)
      {
	barriers.emplace (entry.first, std::unique_ptr<cyclic_barrier> (new cyclic_barrier (entry.second.size ())));
      }

    LK_INIT_CONFIG lock_init_config;
    const float previous_deadlock_interval = prm_get_float_value (PRM_ID_LK_RUN_DEADLOCK_INTERVAL);
    const int previous_lock_escalation_at = prm_get_integer_value (PRM_ID_LK_ESCALATION_AT);
    UINT64 *base_stats = NULL;
    UINT64 *current_stats = NULL;

    lock_initialize_default_config (&lock_init_config);
    lock_init_config.start_deadlock_detector = true;
    lock_init_config.initial_object_locks = 256;
    lock_init_config.object_res_block_count = 2;
    lock_init_config.object_entry_block_count = 1;

    prm_set_float_value (PRM_ID_LK_RUN_DEADLOCK_INTERVAL, config.deadlock_detection_interval_in_secs);
    prm_set_integer_value (PRM_ID_LK_ESCALATION_AT, config.lock_escalation_at);
    base_stats = perfmon_allocate_values ();
    current_stats = perfmon_allocate_values ();
    if (base_stats == NULL || current_stats == NULL)
      {
	free_and_init (base_stats);
	free_and_init (current_stats);
	prm_set_integer_value (PRM_ID_LK_ESCALATION_AT, previous_lock_escalation_at);
	prm_set_float_value (PRM_ID_LK_RUN_DEADLOCK_INTERVAL, previous_deadlock_interval);
	throw std::runtime_error ("perfmon_allocate_values failed");
      }
    if (!pstat_Global.initialized || pstat_Global.global_stats == NULL)
      {
	free_and_init (base_stats);
	free_and_init (current_stats);
	prm_set_integer_value (PRM_ID_LK_ESCALATION_AT, previous_lock_escalation_at);
	prm_set_float_value (PRM_ID_LK_RUN_DEADLOCK_INTERVAL, previous_deadlock_interval);
	throw std::runtime_error ("perfmon global stats not initialized");
      }
    perfmon_copy_values (base_stats, pstat_Global.global_stats);

    error = lock_initialize_with_config (&lock_init_config);
    if (error != NO_ERROR)
      {
	free_and_init (base_stats);
	free_and_init (current_stats);
	prm_set_integer_value (PRM_ID_LK_ESCALATION_AT, previous_lock_escalation_at);
	prm_set_float_value (PRM_ID_LK_RUN_DEADLOCK_INTERVAL, previous_deadlock_interval);
	throw std::runtime_error ("lock_initialize_with_config failed");
      }

    cubthread::system_worker_entry_manager worker_entry_manager (TT_WORKER);
    cubthread::entry_workpool *worker_pool =
	    cubthread::get_manager ()->create_worker_pool (workers.size (), workers.size (), "test_lock_manager_runtime",
		&worker_entry_manager, workers.size (), false);

    if (worker_pool == NULL)
      {
	lock_finalize ();
	free_and_init (base_stats);
	free_and_init (current_stats);
	prm_set_integer_value (PRM_ID_LK_ESCALATION_AT, previous_lock_escalation_at);
	prm_set_float_value (PRM_ID_LK_RUN_DEADLOCK_INTERVAL, previous_deadlock_interval);
	throw std::runtime_error ("create_worker_pool failed");
      }

    pending_workers = static_cast<int> (workers.size ());

    if (has_unconditional_wait)
      {
	watchdog_thread.reset (new std::thread ([&txns, &stop_watchdog] ()
	{
	  const auto grace_period = std::chrono::milliseconds (1500);
	  const auto start_time = std::chrono::steady_clock::now ();

	  while (!stop_watchdog.load ())
	    {
	      if (std::chrono::steady_clock::now () - start_time >= grace_period)
		{
		  for (const std::pair<const int, transaction_context> &txn_entry : txns)
		    {
		      const int tran_index = txn_entry.second.tran_index;
		      if (!lock_is_waiting_transaction (tran_index))
			{
			  continue;
			}

		      cubthread::get_manager ()->map_entries ([tran_index] (THREAD_ENTRY & thread_ref, bool & stop_mapper)
		      {
			(void) stop_mapper;
			if (thread_ref.tran_index == tran_index && thread_ref.lockwait != NULL)
			  {
			    lock_force_thread_timeout_lock (&thread_ref);
			  }
		      });
		    }
		}

	      std::this_thread::sleep_for (std::chrono::milliseconds (50));
	    }
	}));
      }

    try
      {
	for (std::pair<const int, worker_context> &worker_entry : workers)
	  {
	    worker_context &worker = worker_entry.second;

	    cubthread::entry_task *task =
		    new cubthread::entry_callable_task (
		    [&worker, &txns, &barriers, &stats, &stats_mutex, &thread_failure, &failure_mutex,
			      &pending_workers, &completion_mutex, &completion_cv] (cubthread::entry &thread_ref)
	    {
	      try
		{
		  for (const operation &op : worker.operations)
		    {
		      transaction_context &txn = txns.find (op.txn_id)->second;
		      thread_ref.tran_index = txn.tran_index;

		      switch (op.kind)
			{
			case operation_kind::begin_transaction:
			{
			  std::lock_guard<std::mutex> execution_guard (txn.execution_mutex);
			  LOG_TDES *tdes = LOG_FIND_TDES (txn.tran_index);
			  bool start_watch = false;

			  std::lock_guard<std::mutex> guard (txn.mutex);
			  start_watch = !txn.active;
			  txn.active = true;
			  txn.committed_workers = 0;
			  tdes->state = TRAN_ACTIVE;
			  tdes->tran_abort_reason = TRAN_NORMAL;
			  if (tdes->trid == NULL_TRANID)
			    {
			      (void) logtb_get_new_tran_id (&thread_ref, tdes);
			    }
			  lock_clear_deadlock_victim (txn.tran_index);
			  if (start_watch)
			    {
			      perfmon_start_watch (&thread_ref);
			    }
			}
			break;

			case operation_kind::acquire:
			case operation_kind::convert:
			{
			  std::lock_guard<std::mutex> execution_guard (txn.execution_mutex);
			  const OID oid = make_real_oid (op.oid);
			  OID class_oid = make_real_oid (op.class_oid);
			  OID *class_oid_ptr = op.is_class_lock ? oid_Root_class_oid : &class_oid;
			  const LOCK lock = to_lock (op.lock_mode);
			  const int granted = lock_object (&thread_ref, &oid, class_oid_ptr, lock, to_cond_flag (op.wait_policy));

			  std::lock_guard<std::mutex> guard (stats_mutex);
			  stats.acquire_attempts++;
			  if (granted == LK_GRANTED)
			    {
			      stats.acquire_grants++;
			    }
			  else
			    {
			      LOG_TDES *tdes = LOG_FIND_TDES (txn.tran_index);
			      stats.acquire_conflicts++;
			      if (granted == LK_NOTGRANTED_DUE_ABORTED)
				{
				  tdes->state = TRAN_UNACTIVE_UNILATERALLY_ABORTED;
				}
			      else if (granted == LK_NOTGRANTED_DUE_TIMEOUT && tdes->tran_abort_reason != TRAN_NORMAL)
				{
				  tdes->state = TRAN_UNACTIVE_ABORTED;
				}
			    }
			}
			break;

			case operation_kind::commit_transaction:
			{
			  std::lock_guard<std::mutex> execution_guard (txn.execution_mutex);
			  LOG_TDES *tdes = LOG_FIND_TDES (txn.tran_index);
			  bool finalize_txn = false;
			  {
			    std::lock_guard<std::mutex> guard (txn.mutex);
			    txn.committed_workers++;
			    finalize_txn = txn.active && txn.committed_workers == txn.participant_count;
			    if (finalize_txn)
			      {
				txn.active = false;
			      }
			  }

			  if (finalize_txn)
			    {
			      cleanup_transaction_locks (&thread_ref);
			      tdes->state = is_transaction_aborted (tdes) ? TRAN_UNACTIVE_ABORTED : TRAN_UNACTIVE_COMMITTED;
			      lock_clear_deadlock_victim (txn.tran_index);
			      perfmon_stop_watch (&thread_ref);
			    }

			  std::lock_guard<std::mutex> guard (stats_mutex);
			  stats.commits++;
			}
			break;

			case operation_kind::barrier:
			  barriers.find (op.value)->second->wait ();
			  break;

			case operation_kind::sleep:
			  std::this_thread::sleep_for (std::chrono::milliseconds (op.value));
			  break;
			}
		    }
		}
	      catch (...)
		{
		  std::lock_guard<std::mutex> guard (failure_mutex);
		  if (thread_failure == nullptr)
		    {
		      thread_failure = std::current_exception ();
		    }
		}

	      if (--pending_workers == 0)
		{
		  std::lock_guard<std::mutex> guard (completion_mutex);
		  completion_cv.notify_one ();
		}
	    });

	    cubthread::get_manager ()->push_task (worker_pool, task);
	  }

	std::unique_lock<std::mutex> lock (completion_mutex);
	completion_cv.wait (lock, [&pending_workers] ()
	{
	  return pending_workers.load () == 0;
	});
      }
    catch (...)
      {
	stop_watchdog = true;
	if (watchdog_thread != NULL && watchdog_thread->joinable ())
	  {
	    watchdog_thread->join ();
	  }
	cubthread::get_manager ()->destroy_worker_pool (worker_pool);
	lock_finalize ();
	free_and_init (base_stats);
	free_and_init (current_stats);
	prm_set_integer_value (PRM_ID_LK_ESCALATION_AT, previous_lock_escalation_at);
	prm_set_float_value (PRM_ID_LK_RUN_DEADLOCK_INTERVAL, previous_deadlock_interval);
	throw;
      }

    stop_watchdog = true;
    if (watchdog_thread != NULL && watchdog_thread->joinable ())
      {
	watchdog_thread->join ();
      }

    cubthread::get_manager ()->destroy_worker_pool (worker_pool);
    lock_finalize ();
    perfmon_copy_values (current_stats, pstat_Global.global_stats);
    stats.engine_lock_acquires = diff_perf_value (current_stats, base_stats, PSTAT_LK_NUM_ACQUIRED_ON_OBJECTS);
    stats.engine_lock_conversions = diff_perf_value (current_stats, base_stats, PSTAT_LK_NUM_CONVERTED_ON_OBJECTS);
    stats.engine_lock_rerequests = diff_perf_value (current_stats, base_stats, PSTAT_LK_NUM_RE_REQUESTED_ON_OBJECTS);
    stats.engine_lock_waits = diff_perf_value (current_stats, base_stats, PSTAT_LK_NUM_WAITED_ON_OBJECTS);
    stats.engine_lock_wait_time_usec = diff_perf_value (current_stats, base_stats, PSTAT_LK_NUM_WAITED_TIME_ON_OBJECTS);
    stats.engine_lock_escalations = diff_perf_value (current_stats, base_stats, PSTAT_LK_NUM_ESCALATED_ON_OBJECTS);
    stats.engine_deadlocks_detected = diff_perf_value (current_stats, base_stats, PSTAT_LK_NUM_DEADLOCKS_DETECTED);
    {
      const PERF_STAT_ID stat_ids[] =
      {
	PSTAT_LK_NUM_ACQUIRED_ON_OBJECTS,
	PSTAT_LK_NUM_CONVERTED_ON_OBJECTS,
	PSTAT_LK_NUM_RE_REQUESTED_ON_OBJECTS,
	PSTAT_LK_NUM_WAITED_ON_OBJECTS,
	PSTAT_LK_NUM_WAITED_TIME_ON_OBJECTS,
	PSTAT_LK_NUM_ESCALATED_ON_OBJECTS,
	PSTAT_LK_NUM_DEADLOCKS_DETECTED
      };
      std::ostringstream stream;

      for (const PERF_STAT_ID stat_id : stat_ids)
	{
	  stream << std::left << std::setw (32) << pstat_Metadata[stat_id].stat_name
		 << " = " << diff_perf_value (current_stats, base_stats, stat_id) << '\n';
	}
      stats.engine_statdump = stream.str ();
    }
    free_and_init (base_stats);
    free_and_init (current_stats);
    prm_set_integer_value (PRM_ID_LK_ESCALATION_AT, previous_lock_escalation_at);
    prm_set_float_value (PRM_ID_LK_RUN_DEADLOCK_INTERVAL, previous_deadlock_interval);

    if (thread_failure != nullptr)
      {
	std::rethrow_exception (thread_failure);
      }

    return stats;
  }
} // namespace test_lock_manager
