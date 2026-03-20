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
#include <map>
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
      { scenario_kind::hot_class_cold_rows, "hot_class_cold_rows", "Mixed class/instance contention with shared transactions" },
      { scenario_kind::lock_conversion, "lock_conversion", "Repeated lock conversion workload" },
      { scenario_kind::deadlock_detector, "deadlock_detector", "Intentional cyclic lock ordering" },
      { scenario_kind::hash_collision, "hash_collision", "Hash-bucket collision stress" },
      { scenario_kind::low_contention, "low_contention", "Baseline low-contention throughput" },
      { scenario_kind::escalation_sweep, "escalation_sweep", "Row-to-class escalation sweep" }
    };

    constexpr int BARRIER_BASE = 1000;

    mock_oid
    make_oid (int pageid, int slotid)
    {
      mock_oid oid;
      oid.volid = 1;
      oid.pageid = pageid;
      oid.slotid = slotid;
      return oid;
    }

    operation
    make_begin (int worker_id, int txn_id)
    {
      return { worker_id, txn_id, operation_kind::begin_transaction, "", wait_kind::conditional, false,
	       make_oid (0, 0), make_oid (0, 0), 0 };
    }

    operation
    make_commit (int worker_id, int txn_id)
    {
      return { worker_id, txn_id, operation_kind::commit_transaction, "", wait_kind::conditional, false,
	       make_oid (0, 0), make_oid (0, 0), 0 };
    }

    operation
    make_barrier (int worker_id, int txn_id, int barrier_id)
    {
      return { worker_id, txn_id, operation_kind::barrier, "", wait_kind::conditional, false,
	       make_oid (0, 0), make_oid (0, 0), barrier_id };
    }

    operation
    make_sleep (int worker_id, int txn_id, int sleep_msecs)
    {
      return { worker_id, txn_id, operation_kind::sleep, "", wait_kind::conditional, false,
	       make_oid (0, 0), make_oid (0, 0), sleep_msecs };
    }

    operation
    make_lock (int worker_id, int txn_id, operation_kind kind, const char *lock_mode, wait_kind wait_mode,
	       bool is_class_lock, const mock_oid &class_oid, const mock_oid &oid)
    {
      return { worker_id, txn_id, kind, lock_mode, wait_mode, is_class_lock, class_oid, oid, 0 };
    }

    int
    make_barrier_id (int iter, int slot)
    {
      return BARRIER_BASE + iter * 32 + slot;
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
      case operation_kind::begin_transaction:
	return "begin_transaction";
      case operation_kind::acquire:
	return "acquire";
      case operation_kind::convert:
	return "convert";
      case operation_kind::commit_transaction:
	return "commit_transaction";
      case operation_kind::barrier:
	return "barrier";
      case operation_kind::sleep:
	return "sleep";
      }

    return "unknown";
  }

  std::string
  to_string (wait_kind kind)
  {
    switch (kind)
      {
      case wait_kind::conditional:
	return "conditional";
      case wait_kind::unconditional:
	return "unconditional";
      }

    return "unknown";
  }

  std::vector<operation>
  build_operations (const scenario_config &config)
  {
    std::vector<operation> ops;
    const int effective_hotset = std::max (config.hotset_size, 1);
    const int pair_count = std::max (config.transaction_count / 2, 1);

    for (int iter = 0; iter < config.iterations; iter++)
      {
	const int iteration_barrier = make_barrier_id (iter, 0);
	const int conversion_barrier = make_barrier_id (iter, 1);

	for (int worker = 0; worker < config.transaction_count; worker++)
	  {
	    const int own_txn_id = iter * config.transaction_count + worker;
	    const int shared_txn_id = iter * pair_count + (worker / 2);
	    const mock_oid class_oid = make_oid (100 + (worker % effective_hotset), 0);

	    switch (config.kind)
	      {
	      case scenario_kind::hot_row:
		ops.push_back (make_begin (worker, own_txn_id));
		ops.push_back (make_barrier (worker, own_txn_id, iteration_barrier));
		ops.push_back (make_lock (worker, own_txn_id, operation_kind::acquire, "X_LOCK", wait_kind::conditional,
					  false, make_oid (100, 0), make_oid (200, 1)));
		ops.push_back (make_sleep (worker, own_txn_id, 1));
		ops.push_back (make_commit (worker, own_txn_id));
		break;

	      case scenario_kind::hot_class_cold_rows:
		{
		  const int pair_barrier_one = make_barrier_id (iter, 10 + (worker / 2) * 2);
		  const int pair_barrier_two = pair_barrier_one + 1;

		ops.push_back (make_begin (worker, shared_txn_id));
		if ((worker % 2) == 0)
		  {
		    ops.push_back (make_lock (worker, shared_txn_id, operation_kind::acquire, "IX_LOCK",
					      wait_kind::conditional, true, make_oid (100, 0), make_oid (100, 0)));
		    ops.push_back (make_lock (worker, shared_txn_id, operation_kind::acquire, "X_LOCK",
					      wait_kind::conditional, false, make_oid (100, 0),
					      make_oid (210 + (worker / 2), 0)));
		  }
		ops.push_back (make_barrier (worker, shared_txn_id, pair_barrier_one));
		if ((worker % 2) != 0)
		  {
		    ops.push_back (make_lock (worker, shared_txn_id, operation_kind::acquire, "X_LOCK",
					      wait_kind::conditional, false, make_oid (100, 0),
					      make_oid (210 + (worker / 2), worker % effective_hotset)));
		  }
		if ((worker % 2) == 0 && ((iter + worker) % 8) == 0)
		  {
		    ops.push_back (make_lock (worker, shared_txn_id, operation_kind::acquire, "X_LOCK",
					      wait_kind::conditional, true, make_oid (100, 0), make_oid (100, 0)));
		  }
		ops.push_back (make_barrier (worker, shared_txn_id, pair_barrier_two));
		ops.push_back (make_commit (worker, shared_txn_id));
		}
		break;

	      case scenario_kind::lock_conversion:
		{
		  const mock_oid conversion_oid = (worker % 2 == 0) ? make_oid (300, 1) : make_oid (300 + worker, 1);

		ops.push_back (make_begin (worker, own_txn_id));
		ops.push_back (make_lock (worker, own_txn_id, operation_kind::acquire, "S_LOCK", wait_kind::conditional,
					  false, class_oid, conversion_oid));
		ops.push_back (make_barrier (worker, own_txn_id, conversion_barrier));
		ops.push_back (make_lock (worker, own_txn_id, operation_kind::convert, "X_LOCK", wait_kind::conditional,
					  false, class_oid, conversion_oid));
		ops.push_back (make_commit (worker, own_txn_id));
		}
		break;

	      case scenario_kind::deadlock_detector:
		if (worker < 2)
		  {
		    const int peer = (worker == 0) ? 1 : 0;
		    const int pair_barrier = make_barrier_id (iter, 20);
		    const int txn_id = iter * 2 + worker;

		    ops.push_back (make_begin (worker, txn_id));
		    ops.push_back (make_lock (worker, txn_id, operation_kind::acquire, "X_LOCK", wait_kind::conditional,
					      false, make_oid (110, 0), make_oid (400 + worker, 1)));
		    ops.push_back (make_barrier (worker, txn_id, pair_barrier));
		    ops.push_back (make_lock (worker, txn_id, operation_kind::acquire, "X_LOCK",
					      wait_kind::unconditional, false, make_oid (110, 0), make_oid (400 + peer, 1)));
		    ops.push_back (make_commit (worker, txn_id));
		  }
		break;

	      case scenario_kind::hash_collision:
		ops.push_back (make_begin (worker, own_txn_id));
		ops.push_back (make_lock (worker, own_txn_id, operation_kind::acquire, "X_LOCK", wait_kind::conditional,
					  false, make_oid (120, 0), make_oid (500 + (iter % effective_hotset) * 1024, worker)));
		ops.push_back (make_commit (worker, own_txn_id));
		break;

	      case scenario_kind::low_contention:
		ops.push_back (make_begin (worker, own_txn_id));
		ops.push_back (make_lock (worker, own_txn_id, operation_kind::acquire, "X_LOCK", wait_kind::conditional,
					  false, class_oid, make_oid (600 + worker * config.iterations + iter, worker)));
		ops.push_back (make_commit (worker, own_txn_id));
		break;

	      case scenario_kind::escalation_sweep:
		ops.push_back (make_begin (worker, own_txn_id));
		ops.push_back (make_lock (worker, own_txn_id, operation_kind::acquire, "IX_LOCK", wait_kind::conditional,
					  true, make_oid (130, 0), make_oid (130, 0)));
		ops.push_back (make_lock (worker, own_txn_id, operation_kind::acquire, "X_LOCK", wait_kind::conditional,
					  false, make_oid (130, 0), make_oid (700 + worker, iter % (effective_hotset * 2))));
		ops.push_back (make_commit (worker, own_txn_id));
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
    std::set<int> distinct_workers;
    std::set<int> distinct_transactions;

    summary.name = to_string (kind);
    summary.description = get_scenario_description (kind);
    summary.operation_count = operations.size ();

    for (const operation &op : operations)
      {
	distinct_workers.insert (op.worker_id);
	distinct_transactions.insert (op.txn_id);
	summary.operation_kind_counts[to_string (op.kind)]++;
	if (!op.lock_mode.empty ())
	  {
	    std::ostringstream class_key;
	    class_key << op.class_oid.volid << ':' << op.class_oid.pageid << ':' << op.class_oid.slotid;
	    distinct_classes.insert (class_key.str ());

	    std::ostringstream oid_key;
	    oid_key << op.oid.volid << ':' << op.oid.pageid << ':' << op.oid.slotid;
	    distinct_oids.insert (oid_key.str ());
	    summary.lock_mode_counts[op.lock_mode]++;
	    summary.notes["wait_" + to_string (op.wait_policy)]++;
	  }
      }

    summary.distinct_class_count = distinct_classes.size ();
    summary.distinct_oid_count = distinct_oids.size ();
    summary.notes["sample_classes"] = summary.distinct_class_count;
    summary.notes["sample_oids"] = summary.distinct_oid_count;
    summary.notes["distinct_workers"] = distinct_workers.size ();
    summary.notes["distinct_transactions"] = distinct_transactions.size ();

    return summary;
  }

  std::string
  format_operation (const operation &op)
  {
    std::ostringstream out;
    out << "worker=" << op.worker_id
	<< " txn=" << op.txn_id
	<< " op=" << to_string (op.kind);

    if (op.kind == operation_kind::barrier)
      {
	out << " barrier=" << op.value;
      }
    else if (op.kind == operation_kind::sleep)
      {
	out << " sleep_msecs=" << op.value;
      }
    else if (!op.lock_mode.empty ())
      {
	out << " target=" << (op.is_class_lock ? "class" : "instance")
	    << " lock=" << op.lock_mode
	    << " wait=" << to_string (op.wait_policy)
	    << " class_oid=" << op.class_oid.volid << ':' << op.class_oid.pageid << ':' << op.class_oid.slotid
	    << " oid=" << op.oid.volid << ':' << op.oid.pageid << ':' << op.oid.slotid;
      }

    return out.str ();
  }
} // namespace test_lock_manager
