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

#include "test_lock_manager_scenarios.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

namespace
{
  void
  print_usage (const char *progname)
  {
    std::cout << "Usage: " << progname
              << " [--list] [--functional] [--benchmark] [--scenario <name>]"
              << " [--workers N] [--iterations N] [--hotset N] [--sample N] [--loops N]" << std::endl;
  }

  int
  parse_int (const char *arg_name, const char *value)
  {
    char *end_ptr = NULL;
    long parsed = std::strtol (value, &end_ptr, 10);
    if (end_ptr == value || *end_ptr != '\0' || parsed <= 0)
      {
        throw std::invalid_argument (std::string ("Invalid value for ") + arg_name + ": " + value);
      }
    return static_cast<int> (parsed);
  }
}

int
main (int argc, char **argv)
{
  try
    {
      test_lock_manager::scenario_config config {
        test_lock_manager::scenario_kind::hot_row,
        4,
        10,
        4,
        1
      };
      int sample_count = 8;
      int benchmark_loops = 10;
      bool list_only = false;
      bool run_functional = false;
      bool run_benchmark = false;

      for (int index = 1; index < argc; index++)
        {
          std::string arg = argv[index];
          if (arg == "--list")
            {
              list_only = true;
            }
          else if (arg == "--functional")
            {
              run_functional = true;
            }
          else if (arg == "--benchmark")
            {
              run_benchmark = true;
            }
          else if (arg == "--scenario" && index + 1 < argc)
            {
              config.kind = test_lock_manager::parse_scenario_kind (argv[++index]);
            }
          else if (arg == "--workers" && index + 1 < argc)
            {
              config.worker_count = parse_int ("--workers", argv[++index]);
            }
          else if (arg == "--iterations" && index + 1 < argc)
            {
              config.iterations = parse_int ("--iterations", argv[++index]);
            }
          else if (arg == "--hotset" && index + 1 < argc)
            {
              config.hotset_size = parse_int ("--hotset", argv[++index]);
            }
          else if (arg == "--sample" && index + 1 < argc)
            {
              sample_count = parse_int ("--sample", argv[++index]);
            }
          else if (arg == "--loops" && index + 1 < argc)
            {
              benchmark_loops = parse_int ("--loops", argv[++index]);
            }
          else
            {
              print_usage (argv[0]);
              throw std::invalid_argument ("Unknown argument: " + arg);
            }
        }

      if (list_only)
        {
          std::cout << "Available scenarios:" << std::endl;
          for (const std::string &name : test_lock_manager::get_scenario_names ())
            {
              test_lock_manager::scenario_kind kind = test_lock_manager::parse_scenario_kind (name);
              std::cout << "  - " << name << ": " << test_lock_manager::get_scenario_description (kind) << std::endl;
            }
          return 0;
        }

      if (run_functional)
        {
          return test_lock_manager::run_functional_suite ();
        }

      if (run_benchmark)
        {
          return test_lock_manager::run_benchmark_suite (benchmark_loops);
        }

      const std::vector<test_lock_manager::operation> operations = test_lock_manager::build_operations (config);
      const test_lock_manager::scenario_summary summary = test_lock_manager::summarize (config.kind, operations);

      std::cout << "Scenario: " << summary.name << std::endl;
      std::cout << "Description: " << summary.description << std::endl;
      std::cout << "Workers: " << config.worker_count << std::endl;
      std::cout << "Iterations: " << config.iterations << std::endl;
      std::cout << "Hotset size: " << config.hotset_size << std::endl;
      std::cout << "Operations: " << summary.operation_count << std::endl;
      std::cout << "Distinct classes: " << summary.distinct_class_count << std::endl;
      std::cout << "Distinct OIDs: " << summary.distinct_oid_count << std::endl;

      std::cout << "Operation kind counts:" << std::endl;
      for (const auto &entry : summary.operation_kind_counts)
        {
          std::cout << "  - " << entry.first << ": " << entry.second << std::endl;
        }

      std::cout << "Lock mode counts:" << std::endl;
      for (const auto &entry : summary.lock_mode_counts)
        {
          std::cout << "  - " << entry.first << ": " << entry.second << std::endl;
        }

      std::cout << "Sample operations:" << std::endl;
      for (int idx = 0; idx < sample_count && idx < static_cast<int> (operations.size ()); idx++)
        {
          std::cout << "  - " << test_lock_manager::format_operation (operations[idx]) << std::endl;
        }

      return 0;
    }
  catch (const std::exception &e)
    {
      std::cerr << "test_lock_manager failed: " << e.what () << std::endl;
      return 1;
    }
}
