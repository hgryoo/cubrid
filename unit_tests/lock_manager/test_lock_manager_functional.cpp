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

  int
  run_functional_suite (void)
  {
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
