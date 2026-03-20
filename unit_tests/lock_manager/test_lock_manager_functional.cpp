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

#if defined (TEST_LOCK_MANAGER_WITH_CUBRID_API)
#include "lock_manager.h"
#include "thread_entry.hpp"
#include "thread_manager.hpp"
#endif

#include <iostream>
#include <stdexcept>
#include <vector>

namespace test_lock_manager
{
  namespace
  {
    void
    require_true (bool condition, const std::string &message)
    {
      if (!condition)
        {
          throw std::runtime_error (message);
        }
    }
  } // namespace

#if defined (TEST_LOCK_MANAGER_WITH_CUBRID_API)
  class real_thread_entry
  {
    public:
      explicit real_thread_entry (int tran_index)
	: m_thread_entry ()
      {
	cubthread::set_thread_local_entry (m_thread_entry);
	m_thread_entry.tran_index = tran_index;
      }

      ~real_thread_entry ()
      {
	cubthread::clear_thread_local_entry ();
      }

      THREAD_ENTRY *get (void)
      {
	return &m_thread_entry;
      }

    private:
      THREAD_ENTRY m_thread_entry;
  };

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

      error = lock_object (thread_one.get (), &inst_oid, &class_oid, S_LOCK, LK_COND_LOCK);
      require_true (error == LK_GRANTED, "thread_one S lock should be granted");

      error = lock_object (thread_two.get (), &inst_oid, &class_oid, S_LOCK, LK_COND_LOCK);
      require_true (error == LK_GRANTED, "thread_two shared lock should be granted");

      lock_unlock_all (thread_one.get ());
      lock_unlock_all (thread_two.get ());

      error = lock_object (thread_one.get (), &inst_oid, &class_oid, X_LOCK, LK_COND_LOCK);
      require_true (error == LK_GRANTED, "thread_one X lock should be granted");

      error = lock_object (thread_two.get (), &inst_oid, &class_oid, X_LOCK, LK_COND_LOCK);
      require_true (error != LK_GRANTED, "thread_two conflicting X lock should not be granted");

      lock_unlock_all (thread_one.get ());
      lock_unlock_all (thread_two.get ());
    }

    lock_finalize ();

    std::cout << "[functional] passed: lock_manager_api" << std::endl;
  }
#endif

  int
  run_functional_suite (void)
  {
#if defined (TEST_LOCK_MANAGER_WITH_CUBRID_API)
    run_lock_manager_api_suite ();
#endif
    const scenario_config configs[] = {
      { scenario_kind::hot_row, 4, 8, 1, 1 },
      { scenario_kind::lock_conversion, 4, 6, 4, 1 },
      { scenario_kind::deadlock_detector, 4, 5, 4, 1 },
      { scenario_kind::escalation_sweep, 8, 12, 16, 1 }
    };

    mock_runtime runtime;

    for (const scenario_config &config : configs)
      {
        const std::vector<operation> operations = build_operations (config);
        const scenario_summary summary = summarize (config.kind, operations);
        const simulation_stats stats = runtime.simulate (operations);

        require_true (!operations.empty (), "scenario generated no operations: " + summary.name);
        require_true (summary.operation_count == operations.size (), "operation count mismatch: " + summary.name);

        if (config.kind == scenario_kind::hot_row)
          {
            require_true (summary.distinct_oid_count == 1, "hot_row should use exactly one target OID");
          }
        else if (config.kind == scenario_kind::lock_conversion)
          {
            require_true (stats.conversions > 0, "lock_conversion should record conversions");
          }
        else if (config.kind == scenario_kind::deadlock_detector)
          {
            require_true (stats.deadlock_pairs > 0, "deadlock_detector should observe deadlock pairs");
          }
        else if (config.kind == scenario_kind::escalation_sweep)
          {
            require_true (stats.escalation_candidates > 0, "escalation_sweep should produce escalation candidates");
          }

        std::cout << "[functional] passed: " << summary.name << std::endl;
      }

    return 0;
  }
} // namespace test_lock_manager
