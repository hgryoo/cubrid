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

#ifndef _TEST_LOCK_MANAGER_MOCK_RUNTIME_HPP_
#define _TEST_LOCK_MANAGER_MOCK_RUNTIME_HPP_

#include "test_lock_manager_scenarios.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace test_lock_manager
{
  struct simulation_stats
  {
    std::size_t acquire_attempts;
    std::size_t acquire_grants;
    std::size_t acquire_conflicts;
    std::size_t conversions;
    std::size_t releases;
    std::size_t deadlock_pairs;
    std::size_t escalation_candidates;
  };

  class mock_runtime
  {
    public:
      mock_runtime ();

      simulation_stats simulate (const std::vector<operation> &operations);

    private:
      std::string make_resource_key (const operation &op) const;
  };
} // namespace test_lock_manager

#endif // _TEST_LOCK_MANAGER_MOCK_RUNTIME_HPP_
