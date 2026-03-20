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

#ifndef _TEST_LOCK_MANAGER_SCENARIOS_HPP_
#define _TEST_LOCK_MANAGER_SCENARIOS_HPP_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace test_lock_manager
{
  struct mock_oid
  {
    int volid;
    int pageid;
    int slotid;
  };

  enum class scenario_kind
  {
    hot_row,
    hot_class_cold_rows,
    lock_conversion,
    deadlock_detector,
    hash_collision,
    low_contention,
    escalation_sweep
  };

  enum class operation_kind
  {
    acquire,
    convert,
    release
  };

  struct operation
  {
    int worker_id;
    int txn_id;
    operation_kind kind;
    std::string lock_mode;
    bool is_class_lock;
    mock_oid class_oid;
    mock_oid oid;
  };

  struct scenario_config
  {
    scenario_kind kind;
    int worker_count;
    int iterations;
    int hotset_size;
    int seed;
  };

  struct scenario_summary
  {
    std::string name;
    std::string description;
    std::size_t operation_count;
    std::size_t distinct_class_count;
    std::size_t distinct_oid_count;
    std::map<std::string, std::size_t> operation_kind_counts;
    std::map<std::string, std::size_t> lock_mode_counts;
    std::map<std::string, std::size_t> notes;
  };

  std::vector<std::string> get_scenario_names (void);
  std::string get_scenario_description (scenario_kind kind);
  scenario_kind parse_scenario_kind (const std::string &name);
  std::string to_string (scenario_kind kind);
  std::string to_string (operation_kind kind);

  std::vector<operation> build_operations (const scenario_config &config);
  scenario_summary summarize (scenario_kind kind, const std::vector<operation> &operations);
  std::string format_operation (const operation &op);
  int run_functional_suite (void);
  int run_benchmark_suite (int loops);
} // namespace test_lock_manager

#endif // _TEST_LOCK_MANAGER_SCENARIOS_HPP_
