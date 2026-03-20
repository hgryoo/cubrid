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

#include <map>
#include <set>
#include <sstream>
#include <vector>
#include <utility>

namespace test_lock_manager
{
  namespace
  {
    bool
    has_path (const std::set<std::pair<int, int>> &edges, int from_txn, int to_txn)
    {
      std::set<int> visited;
      std::vector<int> stack;

      stack.push_back (from_txn);
      while (!stack.empty ())
        {
          const int current = stack.back ();
          stack.pop_back ();

          if (current == to_txn)
            {
              return true;
            }

          if (visited.insert (current).second == false)
            {
              continue;
            }

          for (const std::pair<int, int> &edge : edges)
            {
              if (edge.first == current)
                {
                  stack.push_back (edge.second);
                }
            }
        }

      return false;
    }
  } // namespace

  mock_runtime::mock_runtime ()
  {
  }

  std::string
  mock_runtime::make_resource_key (const operation &op) const
  {
    std::ostringstream out;
    out << op.class_oid.volid << ':' << op.class_oid.pageid << ':' << op.class_oid.slotid
        << '/' << op.oid.volid << ':' << op.oid.pageid << ':' << op.oid.slotid;
    return out.str ();
  }

  simulation_stats
  mock_runtime::simulate (const std::vector<operation> &operations)
  {
    simulation_stats stats { 0, 0, 0, 0, 0, 0, 0 };
    std::map<std::string, int> owners;
    std::map<int, std::set<std::string>> txn_resources;
    std::map<std::string, std::size_t> txn_class_acquires;
    std::set<std::pair<int, int>> wait_edges;

    for (const operation &op : operations)
      {
        const std::string resource_key = make_resource_key (op);
        std::ostringstream class_key_builder;
        class_key_builder << op.txn_id << ':' << op.class_oid.volid << ':' << op.class_oid.pageid << ':' << op.class_oid.slotid;
        const std::string txn_class_key = class_key_builder.str ();
        std::map<std::string, int>::iterator owner_it = owners.find (resource_key);

        switch (op.kind)
          {
          case operation_kind::acquire:
            stats.acquire_attempts++;
            if (owner_it == owners.end () || owner_it->second == op.txn_id)
              {
                owners[resource_key] = op.txn_id;
                txn_resources[op.txn_id].insert (resource_key);
                txn_class_acquires[txn_class_key]++;
                stats.acquire_grants++;
                if (txn_class_acquires[txn_class_key] == 10)
                  {
                    stats.escalation_candidates++;
                  }
              }
            else
              {
                stats.acquire_conflicts++;
                if (has_path (wait_edges, owner_it->second, op.txn_id))
                  {
                    stats.deadlock_pairs++;
                  }
                wait_edges.insert (std::make_pair (op.txn_id, owner_it->second));
              }
            break;

          case operation_kind::convert:
            if (owner_it != owners.end () && owner_it->second == op.txn_id)
              {
                stats.conversions++;
              }
            break;

          case operation_kind::release:
            if (owner_it != owners.end () && owner_it->second == op.txn_id)
              {
                owners.erase (owner_it);
                txn_resources[op.txn_id].erase (resource_key);
                stats.releases++;
              }
            break;
          }
      }

    return stats;
  }
} // namespace test_lock_manager
