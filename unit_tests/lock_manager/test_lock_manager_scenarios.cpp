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

#include <algorithm>
#include <set>
#include <sstream>
#include <stdexcept>

namespace test_lock_manager
{
  namespace
  {
    struct scenario_metadata
    {
      scenario_kind kind;
      const char *name;
      const char *description;
    };

    constexpr scenario_metadata SCENARIOS[] = {
      { scenario_kind::hot_row, "hot_row", "Single-row hotspot contention" },
      { scenario_kind::hot_class_cold_rows, "hot_class_cold_rows", "Mixed class/instance contention" },
      { scenario_kind::lock_conversion, "lock_conversion", "Repeated lock conversion workload" },
      { scenario_kind::deadlock_detector, "deadlock_detector", "Intentional cyclic lock ordering" },
      { scenario_kind::hash_collision, "hash_collision", "Hash-bucket collision stress" },
      { scenario_kind::low_contention, "low_contention", "Baseline low-contention throughput" },
      { scenario_kind::escalation_sweep, "escalation_sweep", "Row-to-class escalation sweep" }
    };

    mock_oid
    make_oid (int pageid, int slotid)
    {
      mock_oid oid;
      oid.volid = 1;
      oid.pageid = pageid;
      oid.slotid = slotid;
      return oid;
    }

    void
    append_acquire_release (std::vector<operation> &ops, int worker_id, int txn_id, const char *lock_mode,
                            const mock_oid &class_oid, const mock_oid &oid)
    {
      ops.push_back ({ worker_id, txn_id, operation_kind::acquire, lock_mode, class_oid, oid });
      ops.push_back ({ worker_id, txn_id, operation_kind::release, lock_mode, class_oid, oid });
    }

    const scenario_metadata &
    get_metadata (scenario_kind kind)
    {
      const scenario_metadata *found = std::find_if (std::begin (SCENARIOS), std::end (SCENARIOS),
                                                     [kind] (const scenario_metadata &meta)
      {
        return meta.kind == kind;
      });

      if (found == std::end (SCENARIOS))
        {
          throw std::invalid_argument ("Unknown scenario kind");
        }
      return *found;
    }
  } // namespace

  std::vector<std::string>
  get_scenario_names (void)
  {
    std::vector<std::string> names;
    for (const scenario_metadata &meta : SCENARIOS)
      {
        names.emplace_back (meta.name);
      }
    return names;
  }

  std::string
  get_scenario_description (scenario_kind kind)
  {
    return get_metadata (kind).description;
  }

  scenario_kind
  parse_scenario_kind (const std::string &name)
  {
    auto found = std::find_if (std::begin (SCENARIOS), std::end (SCENARIOS), [&name] (const scenario_metadata &meta)
    {
      return name == meta.name;
    });

    if (found == std::end (SCENARIOS))
      {
        throw std::invalid_argument ("Unknown scenario name: " + name);
      }
    return found->kind;
  }

  std::string
  to_string (scenario_kind kind)
  {
    return get_metadata (kind).name;
  }

  std::string
  to_string (operation_kind kind)
  {
    switch (kind)
      {
      case operation_kind::acquire:
        return "acquire";
      case operation_kind::convert:
        return "convert";
      case operation_kind::release:
        return "release";
      }

    return "unknown";
  }

  std::vector<operation>
  build_operations (const scenario_config &config)
  {
    std::vector<operation> ops;
    const int effective_hotset = std::max (config.hotset_size, 1);

    if (config.kind == scenario_kind::deadlock_detector)
      {
        for (int iter = 0; iter < config.iterations; iter++)
          {
            for (int worker = 0; worker < config.worker_count; worker++)
              {
                ops.push_back ({ worker, worker, operation_kind::acquire, "X_LOCK", make_oid (110, 0),
                                 make_oid (400 + worker, 1) });
              }
            for (int worker = 0; worker < config.worker_count; worker++)
              {
                ops.push_back ({ worker, worker, operation_kind::acquire, "X_LOCK", make_oid (110, 0),
                                 make_oid (400 + ((worker + 1) % config.worker_count), 1) });
              }
            for (int worker = 0; worker < config.worker_count; worker++)
              {
                ops.push_back ({ worker, worker, operation_kind::release, "X_LOCK", make_oid (110, 0),
                                 make_oid (400 + worker, 1) });
              }
          }

        return ops;
      }

    for (int iter = 0; iter < config.iterations; iter++)
      {
        for (int worker = 0; worker < config.worker_count; worker++)
          {
            const int txn_id = worker;
            const mock_oid class_oid = make_oid (100 + (worker % effective_hotset), 0);

            switch (config.kind)
              {
              case scenario_kind::hot_row:
                append_acquire_release (ops, worker, txn_id, "X_LOCK", make_oid (100, 0), make_oid (200, 1));
                break;

              case scenario_kind::hot_class_cold_rows:
                if (((iter + worker) % 10) == 0)
                  {
                    append_acquire_release (ops, worker, txn_id, "X_LOCK", make_oid (100, 0), make_oid (100, 0));
                  }
                else
                  {
                    append_acquire_release (ops, worker, txn_id, "S_LOCK", make_oid (100, 0),
                                            make_oid (200 + worker, iter % effective_hotset));
                  }
                break;

              case scenario_kind::lock_conversion:
                ops.push_back ({ worker, txn_id, operation_kind::acquire, "S_LOCK", class_oid,
                                 make_oid (300 + worker, iter % effective_hotset) });
                ops.push_back ({ worker, txn_id, operation_kind::convert, "X_LOCK", class_oid,
                                 make_oid (300 + worker, iter % effective_hotset) });
                ops.push_back ({ worker, txn_id, operation_kind::release, "X_LOCK", class_oid,
                                 make_oid (300 + worker, iter % effective_hotset) });
                break;

              case scenario_kind::hash_collision:
                append_acquire_release (ops, worker, txn_id, "X_LOCK", make_oid (120, 0),
                                        make_oid (500 + (iter % effective_hotset) * 1024, worker));
                break;

              case scenario_kind::low_contention:
                append_acquire_release (ops, worker, txn_id, "X_LOCK", class_oid,
                                        make_oid (600 + worker * config.iterations + iter, worker));
                break;

              case scenario_kind::escalation_sweep:
                ops.push_back ({ worker, txn_id, operation_kind::acquire, "IX_LOCK", make_oid (130, 0),
                                 make_oid (130, 0) });
                append_acquire_release (ops, worker, txn_id, "X_LOCK", make_oid (130, 0),
                                        make_oid (700 + worker, iter % (effective_hotset * 2)));
                break;
              }
          }
      }

    return ops;
  }

  scenario_summary
  summarize (scenario_kind kind, const std::vector<operation> &operations)
  {
    scenario_summary summary;
    std::set<std::string> distinct_classes;
    std::set<std::string> distinct_oids;

    summary.name = to_string (kind);
    summary.description = get_scenario_description (kind);
    summary.operation_count = operations.size ();

    for (const operation &op : operations)
      {
        std::ostringstream class_key;
        class_key << op.class_oid.volid << ':' << op.class_oid.pageid << ':' << op.class_oid.slotid;
        distinct_classes.insert (class_key.str ());

        std::ostringstream oid_key;
        oid_key << op.oid.volid << ':' << op.oid.pageid << ':' << op.oid.slotid;
        distinct_oids.insert (oid_key.str ());

        summary.operation_kind_counts[to_string (op.kind)]++;
        summary.lock_mode_counts[op.lock_mode]++;
      }

    summary.distinct_class_count = distinct_classes.size ();
    summary.distinct_oid_count = distinct_oids.size ();
    summary.notes["sample_classes"] = summary.distinct_class_count;
    summary.notes["sample_oids"] = summary.distinct_oid_count;

    return summary;
  }

  std::string
  format_operation (const operation &op)
  {
    std::ostringstream out;
    out << "worker=" << op.worker_id
        << " txn=" << op.txn_id
        << " op=" << to_string (op.kind)
        << " lock=" << op.lock_mode
        << " class_oid=" << op.class_oid.volid << ':' << op.class_oid.pageid << ':' << op.class_oid.slotid
        << " oid=" << op.oid.volid << ':' << op.oid.pageid << ':' << op.oid.slotid;
    return out.str ();
  }
} // namespace test_lock_manager
