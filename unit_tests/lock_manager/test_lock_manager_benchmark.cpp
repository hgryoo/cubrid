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
      float deadlock_interval_secs;
      std::size_t conflicts;
      std::size_t lock_waits;
      std::size_t lock_conversions;
      std::size_t lock_escalations;
      std::size_t deadlocks_detected;
      std::size_t lock_wait_time_usec;
      std::string engine_statdump;
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
      std::cout <<
		"scenario,loops,ops,total_us,ops_per_sec,deadlock_interval_secs,conflicts,lock_waits,lock_conversions,lock_escalations,deadlocks_detected,lock_wait_time_usec"
		<< std::endl;

      for (const benchmark_row &row : rows)
	{
	  std::cout << row.scenario << ','
		    << row.loops << ','
		    << row.operations << ','
		    << row.total_us << ','
		    << row.ops_per_sec << ','
		    << row.deadlock_interval_secs << ','
		    << row.conflicts << ','
		    << row.lock_waits << ','
		    << row.lock_conversions << ','
		    << row.lock_escalations << ','
		    << row.deadlocks_detected << ','
		    << row.lock_wait_time_usec
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
	  max_deadlocks = std::max (max_deadlocks, row.deadlocks_detected);
	}

      std::cout << "**Benchmark Summary**" << std::endl;
      for (const benchmark_row &row : rows)
	{
	  const double throughput_ratio = max_ops_per_sec > 0.0 ? row.ops_per_sec / max_ops_per_sec : 0.0;
	  const double conflict_ratio = max_conflicts > 0 ? static_cast<double> (row.conflicts) / max_conflicts : 0.0;
	  const double deadlock_ratio = max_deadlocks > 0
					? static_cast<double> (row.deadlocks_detected) / max_deadlocks
					: 0.0;

	  std::ostringstream ops_stream;
	  ops_stream << std::fixed << std::setprecision (1) << row.ops_per_sec;

	  std::cout << row.scenario
		    << " ops/s=" << ops_stream.str ()
		    << " deadlock_interval=" << row.deadlock_interval_secs << 's'
		    << " conflicts=" << row.conflicts
		    << " waits=" << row.lock_waits
		    << " conversions=" << row.lock_conversions
		    << " escalations=" << row.lock_escalations
		    << " deadlocks=" << row.deadlocks_detected
		    << std::endl;
	  std::cout << "  throughput [" << make_bar (throughput_ratio, 24) << "]" << std::endl;
	  std::cout << "  conflicts  [" << make_bar (conflict_ratio, 24) << "]" << std::endl;
	  if (max_deadlocks > 0)
	    {
	      std::cout << "  deadlocks  [" << make_bar (deadlock_ratio, 24) << "]" << std::endl;
	    }
	  std::cout << "  waited_us=" << row.lock_wait_time_usec << std::endl;
	}
    }
  } // namespace

  int
  run_benchmark_suite (int loops, const scenario_config &base_config, benchmark_output_format format)
  {
    const scenario_kind scenarios[] =
    {
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
	simulation_stats total { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "" };
	mock_runtime runtime;

	const auto start = std::chrono::steady_clock::now ();
	for (int loop = 0; loop < loops; loop++)
	  {
	    simulation_stats stats = runtime.simulate (operations, config);
	    total.acquire_attempts += stats.acquire_attempts;
	    total.acquire_grants += stats.acquire_grants;
	    total.acquire_conflicts += stats.acquire_conflicts;
	    total.commits += stats.commits;
	    total.engine_lock_acquires += stats.engine_lock_acquires;
	    total.engine_lock_conversions += stats.engine_lock_conversions;
	    total.engine_lock_rerequests += stats.engine_lock_rerequests;
	    total.engine_lock_waits += stats.engine_lock_waits;
	    total.engine_lock_wait_time_usec += stats.engine_lock_wait_time_usec;
	    total.engine_lock_escalations += stats.engine_lock_escalations;
	    total.engine_deadlocks_detected += stats.engine_deadlocks_detected;
	    total.engine_statdump = stats.engine_statdump;
	  }
	const auto end = std::chrono::steady_clock::now ();
	const auto total_us = std::chrono::duration_cast<std::chrono::microseconds> (end - start).count ();
	const double ops_per_sec = total_us > 0 ? (operations.size () * loops * 1000000.0) / total_us : 0.0;

	rows.push_back ({ to_string (kind), loops, operations.size (), total_us, ops_per_sec,
			  config.deadlock_detection_interval_in_secs, total.acquire_conflicts, total.engine_lock_waits,
			  total.engine_lock_conversions, total.engine_lock_escalations, total.engine_deadlocks_detected,
			  total.engine_lock_wait_time_usec, total.engine_statdump });
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
	std::cout << std::endl << "**Engine Lock Stats**" << std::endl;
	for (const benchmark_row &row : rows)
	  {
	    std::cout << "[" << row.scenario << "]" << std::endl;
	    std::cout << row.engine_statdump;
	  }
      }

    return 0;
  }
} // namespace test_lock_manager
