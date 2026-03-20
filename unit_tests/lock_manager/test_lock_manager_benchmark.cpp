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
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace test_lock_manager
{
  namespace
  {
    struct benchmark_row
    {
      std::string scenario;
      int loops;
      std::size_t operations;
      long long total_us;
      double ops_per_sec;
      std::size_t conflicts;
      std::size_t deadlock_pairs;
      std::size_t conversions;
      std::size_t escalation_candidates;
    };

    std::string
    make_bar (double ratio, int width)
    {
      const int filled = static_cast<int> (ratio * width + 0.5);
      return std::string (std::max (filled, 0), '#') + std::string (std::max (width - filled, 0), '.');
    }

    void
    print_csv (const std::vector<benchmark_row> &rows)
    {
      std::cout << "scenario,loops,ops,total_us,ops_per_sec,conflicts,deadlock_pairs,conversions,escalation_candidates"
                << std::endl;

      for (const benchmark_row &row : rows)
        {
          std::cout << row.scenario << ','
                    << row.loops << ','
                    << row.operations << ','
                    << row.total_us << ','
                    << row.ops_per_sec << ','
                    << row.conflicts << ','
                    << row.deadlock_pairs << ','
                    << row.conversions << ','
                    << row.escalation_candidates
                    << std::endl;
        }
    }

    void
    print_pretty (const std::vector<benchmark_row> &rows)
    {
      double max_ops_per_sec = 0.0;
      std::size_t max_conflicts = 0;
      std::size_t max_deadlocks = 0;

      for (const benchmark_row &row : rows)
        {
          max_ops_per_sec = std::max (max_ops_per_sec, row.ops_per_sec);
          max_conflicts = std::max (max_conflicts, row.conflicts);
          max_deadlocks = std::max (max_deadlocks, row.deadlock_pairs);
        }

      std::cout << "**Benchmark Summary**" << std::endl;
      for (const benchmark_row &row : rows)
        {
          const double throughput_ratio = max_ops_per_sec > 0.0 ? row.ops_per_sec / max_ops_per_sec : 0.0;
          const double conflict_ratio = max_conflicts > 0 ? static_cast<double> (row.conflicts) / max_conflicts : 0.0;
          const double deadlock_ratio = max_deadlocks > 0
                                          ? static_cast<double> (row.deadlock_pairs) / max_deadlocks
                                          : 0.0;

          std::ostringstream ops_stream;
          ops_stream << std::fixed << std::setprecision (1) << row.ops_per_sec;

          std::cout << row.scenario
                    << " ops/s=" << ops_stream.str ()
                    << " conflicts=" << row.conflicts
                    << " deadlocks=" << row.deadlock_pairs
                    << " conversions=" << row.conversions
                    << " escalations=" << row.escalation_candidates
                    << std::endl;
          std::cout << "  throughput [" << make_bar (throughput_ratio, 24) << "]" << std::endl;
          std::cout << "  conflicts  [" << make_bar (conflict_ratio, 24) << "]" << std::endl;
          if (max_deadlocks > 0)
            {
              std::cout << "  deadlocks  [" << make_bar (deadlock_ratio, 24) << "]" << std::endl;
            }
        }
    }
  } // namespace

  int
  run_benchmark_suite (int loops, const scenario_config &base_config, benchmark_output_format format)
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

    std::vector<benchmark_row> rows;

    for (scenario_kind kind : scenarios)
      {
        scenario_config config = base_config;
        config.kind = kind;
        std::vector<operation> operations = build_operations (config);
        simulation_stats total { 0, 0, 0, 0, 0, 0, 0 };
        mock_runtime runtime;

        const auto start = std::chrono::steady_clock::now ();
        for (int loop = 0; loop < loops; loop++)
          {
            simulation_stats stats = runtime.simulate (operations);
            total.acquire_attempts += stats.acquire_attempts;
            total.acquire_grants += stats.acquire_grants;
            total.acquire_conflicts += stats.acquire_conflicts;
            total.conversions += stats.conversions;
            total.commits += stats.commits;
            total.deadlock_pairs += stats.deadlock_pairs;
            total.escalation_candidates += stats.escalation_candidates;
          }
        const auto end = std::chrono::steady_clock::now ();
        const auto total_us = std::chrono::duration_cast<std::chrono::microseconds> (end - start).count ();
        const double ops_per_sec = total_us > 0 ? (operations.size () * loops * 1000000.0) / total_us : 0.0;

        rows.push_back ({ to_string (kind), loops, operations.size (), total_us, ops_per_sec, total.acquire_conflicts,
                          total.deadlock_pairs, total.conversions, total.escalation_candidates });
      }

    if (format == benchmark_output_format::csv || format == benchmark_output_format::both)
      {
        print_csv (rows);
      }
    if (format == benchmark_output_format::pretty || format == benchmark_output_format::both)
      {
        if (format == benchmark_output_format::both)
          {
            std::cout << std::endl;
          }
        print_pretty (rows);
      }

    return 0;
  }
} // namespace test_lock_manager
