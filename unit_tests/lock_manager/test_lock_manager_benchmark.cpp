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

#include <chrono>
#include <iostream>

namespace test_lock_manager
{
  int
  run_benchmark_suite (int loops)
  {
    const scenario_kind scenarios[] = {
      scenario_kind::hot_row,
      scenario_kind::hot_class_cold_rows,
      scenario_kind::lock_conversion,
      scenario_kind::deadlock_detector,
      scenario_kind::hash_collision,
      scenario_kind::low_contention,
      scenario_kind::escalation_sweep
    };

    mock_runtime runtime;

    std::cout << "scenario,loops,ops,total_us,ops_per_sec,conflicts,deadlock_pairs,conversions,escalation_candidates" << std::endl;

    for (scenario_kind kind : scenarios)
      {
        const scenario_config config { kind, 8, 100, 16, 1 };
        std::vector<operation> operations = build_operations (config);
        simulation_stats total { 0, 0, 0, 0, 0, 0, 0 };

        const auto start = std::chrono::steady_clock::now ();
        for (int loop = 0; loop < loops; loop++)
          {
            simulation_stats stats = runtime.simulate (operations);
            total.acquire_attempts += stats.acquire_attempts;
            total.acquire_grants += stats.acquire_grants;
            total.acquire_conflicts += stats.acquire_conflicts;
            total.conversions += stats.conversions;
            total.releases += stats.releases;
            total.deadlock_pairs += stats.deadlock_pairs;
            total.escalation_candidates += stats.escalation_candidates;
          }
        const auto end = std::chrono::steady_clock::now ();
        const auto total_us = std::chrono::duration_cast<std::chrono::microseconds> (end - start).count ();
        const double ops_per_sec = total_us > 0 ? (operations.size () * loops * 1000000.0) / total_us : 0.0;

        std::cout << to_string (kind) << ','
                  << loops << ','
                  << operations.size () << ','
                  << total_us << ','
                  << ops_per_sec << ','
                  << total.acquire_conflicts << ','
                  << total.deadlock_pairs << ','
                  << total.conversions << ','
                  << total.escalation_candidates
                  << std::endl;
      }

    return 0;
  }
} // namespace test_lock_manager
