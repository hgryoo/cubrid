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
#include "system_parameter.h"
#include "thread_entry.hpp"
#include "thread_manager.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace test_lock_manager
{
  namespace
  {
    class scoped_thread_entry
    {
      public:
	explicit scoped_thread_entry (THREAD_ENTRY &thread_entry)
	  : m_previous (&cubthread::get_entry ())
	{
	  cubthread::clear_thread_local_entry ();
	  cubthread::set_thread_local_entry (thread_entry);
	}

	~scoped_thread_entry ()
	{
	  cubthread::clear_thread_local_entry ();
	  cubthread::set_thread_local_entry (*m_previous);
	}

      private:
	cubthread::entry *m_previous;
    };

    void
    require_true (bool condition, const std::string &message)
    {
      if (!condition)
	{
	  throw std::runtime_error (message);
	}
    }

  } // namespace

  class real_thread_entry
  {
    public:
      explicit real_thread_entry (int tran_index)
	: m_thread_entry ()
      {
	m_thread_entry.tran_index = tran_index;
	m_thread_entry.request_lock_free_transactions ();
      }

      ~real_thread_entry (void)
      {
	m_thread_entry.return_lock_free_transaction_entries ();
      }

      real_thread_entry (const real_thread_entry &) = delete;
      real_thread_entry &operator= (const real_thread_entry &) = delete;
      real_thread_entry (real_thread_entry &&) = delete;
      real_thread_entry &operator= (real_thread_entry &&) = delete;

      THREAD_ENTRY *get (void)
      {
	return &m_thread_entry;
      }

    private:
      THREAD_ENTRY m_thread_entry;
  };

  namespace
  {
    void
    cleanup_transaction_locks (real_thread_entry &entry)
    {
      scoped_thread_entry activation (*entry.get ());
      lock_unlock_all (entry.get ());
    }
  } // namespace

  static OID
  make_test_oid (int pageid, int slotid)
  {
    OID oid;
    oid.volid = 1;
    oid.pageid = pageid;
    oid.slotid = slotid;
    return oid;
  }

  static void
  run_lock_manager_api_suite (void)
  {
    assert (cubthread::get_manager () != NULL);

    LK_INIT_CONFIG config;
    int error = NO_ERROR;

    lock_initialize_default_config (&config);

    require_true (config.initial_object_locks > 0, "default initial_object_locks must be positive");
    require_true (config.object_res_block_count > 0, "default object_res_block_count must be positive");
    require_true (config.object_entry_block_count > 0, "default object_entry_block_count must be positive");

    config.start_deadlock_detector = false;
    config.initial_object_locks = 128;
    config.object_res_block_count = 2;
    config.object_entry_block_count = 1;

    error = lock_initialize_with_config (&config);
    require_true (error == NO_ERROR, "lock_initialize_with_config should succeed");

    {
      real_thread_entry thread_one (1);
      real_thread_entry thread_two (2);
      OID class_oid = make_test_oid (100, 0);
      OID inst_oid = make_test_oid (200, 1);

      {
	scoped_thread_entry activation (*thread_one.get ());
	error = lock_object (thread_one.get (), &inst_oid, &class_oid, S_LOCK, LK_COND_LOCK);
      }
      require_true (error == LK_GRANTED, "thread_one S lock should be granted");

      {
	scoped_thread_entry activation (*thread_two.get ());
	error = lock_object (thread_two.get (), &inst_oid, &class_oid, S_LOCK, LK_COND_LOCK);
      }
      require_true (error == LK_GRANTED, "thread_two shared lock should be granted");

      cleanup_transaction_locks (thread_one);
      cleanup_transaction_locks (thread_two);

      {
	scoped_thread_entry activation (*thread_one.get ());
	error = lock_object (thread_one.get (), &inst_oid, &class_oid, X_LOCK, LK_COND_LOCK);
      }
      require_true (error == LK_GRANTED, "thread_one X lock should be granted");

      {
	scoped_thread_entry activation (*thread_two.get ());
	error = lock_object (thread_two.get (), &inst_oid, &class_oid, X_LOCK, LK_COND_LOCK);
      }
      require_true (error != LK_GRANTED, "thread_two conflicting X lock should not be granted");

      cleanup_transaction_locks (thread_one);
      cleanup_transaction_locks (thread_two);
    }

    lock_finalize ();

    std::cout << "[functional] passed: lock_manager_api" << std::endl;
  }

  int
  run_functional_suite (void)
  {
    run_lock_manager_api_suite ();
    const scenario_config configs[] =
    {
      { scenario_kind::hot_row, 4, 8, 1, 1, 16, 70, 25, 1.0f, prm_get_integer_value (PRM_ID_LK_ESCALATION_AT), 1 },
      { scenario_kind::hot_class_cold_rows, 4, 6, 4, 4, 16, 70, 25, 1.0f, prm_get_integer_value (PRM_ID_LK_ESCALATION_AT), 1 },
      { scenario_kind::lock_conversion, 4, 6, 4, 4, 16, 70, 25, 1.0f, prm_get_integer_value (PRM_ID_LK_ESCALATION_AT), 1 },
      { scenario_kind::deadlock_detector, 2, 3, 2, 1, 8, 100, 25, 0.1f, prm_get_integer_value (PRM_ID_LK_ESCALATION_AT), 1 },
      { scenario_kind::escalation_sweep, 8, 12, 16, 8, 32, 70, 25, 1.0f, 4, 1 }
    };

    mock_runtime runtime;

    for (const scenario_config &config : configs)
      {
	const std::vector<operation> operations = build_operations (config);
	const scenario_summary summary = summarize (config.kind, operations);
	const simulation_stats stats = runtime.simulate (operations, config);

	require_true (!operations.empty (), "scenario generated no operations: " + summary.name);
	require_true (summary.operation_count == operations.size (), "operation count mismatch: " + summary.name);

	if (config.kind == scenario_kind::hot_row)
	  {
	    require_true (summary.distinct_oid_count == 1, "hot_row should use exactly one target OID");
	  }
	else if (config.kind == scenario_kind::hot_class_cold_rows)
	  {
	    const std::size_t expected_transactions = static_cast<std::size_t> (std::max (config.transaction_count / 2, 1));

	    require_true (summary.notes.at ("distinct_transactions") == expected_transactions,
			  "hot_class_cold_rows should share transactions across workers");
	  }
	else if (config.kind == scenario_kind::lock_conversion)
	  {
	    require_true (stats.engine_lock_conversions > 0, "lock_conversion should record engine conversions");
	  }
	else if (config.kind == scenario_kind::deadlock_detector)
	  {
	    require_true (stats.engine_deadlocks_detected > 0,
			  "deadlock_detector should record engine deadlock detections");
	  }
	else if (config.kind == scenario_kind::escalation_sweep)
	  {
	    require_true (stats.engine_lock_escalations > 0,
			  "escalation_sweep should record engine lock escalations");
	  }

	std::cout << "[functional] passed: " << summary.name << std::endl;
      }

    return 0;
  }
} // namespace test_lock_manager
