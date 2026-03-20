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
#include <cstdint>
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
    constexpr int DEFAULT_OBJECT_HASH_SIZE = 10000;

    mock_oid
    make_oid (int pageid, int slotid)
    {
      mock_oid oid;
      oid.volid = 1;
      oid.pageid = pageid;
      oid.slotid = slotid;
      return oid;
    }

    mock_oid
    make_pool_oid (int pageid_seed, int slotid_seed)
    {
      mock_oid oid;
      oid.volid = -1;
      oid.pageid = -2 - pageid_seed;
      oid.slotid = -1 - slotid_seed;
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

    const oid_bucket &
    get_bucket (const oid_pool &pool, int index)
    {
      return pool.buckets[static_cast<std::size_t> (index % static_cast<int> (pool.buckets.size ()))];
    }

    const mock_oid &
    get_object_from_bucket (const oid_bucket &bucket, int index)
    {
      return bucket.object_oids[static_cast<std::size_t> (index % static_cast<int> (bucket.object_oids.size ()))];
    }

    const mock_oid &
    get_hot_object_from_bucket (const oid_bucket &bucket, int index)
    {
      const int hot_count = std::min (std::max (static_cast<int> (bucket.hot_object_count), 1),
				      static_cast<int> (bucket.object_oids.size ()));
      return bucket.object_oids[static_cast<std::size_t> (index % hot_count)];
    }

    const mock_oid &
    get_cold_object_from_bucket (const oid_bucket &bucket, int index)
    {
      const int object_count = static_cast<int> (bucket.object_oids.size ());
      const int hot_count = std::min (std::max (static_cast<int> (bucket.hot_object_count), 1), object_count);
      if (object_count <= hot_count)
	{
	  return get_hot_object_from_bucket (bucket, index);
	}
      const int cold_count = object_count - hot_count;
      return bucket.object_oids[static_cast<std::size_t> (hot_count + (index % cold_count))];
    }

    std::uint32_t
    mix_value (std::uint32_t value)
    {
      value ^= value >> 16;
      value *= 0x7feb352dU;
      value ^= value >> 15;
      value *= 0x846ca68bU;
      value ^= value >> 16;
      return value;
    }

    std::uint32_t
    make_token (const scenario_config &config, int iter, int worker, int stream)
    {
      std::uint32_t value = static_cast<std::uint32_t> (config.seed);
      value ^= static_cast<std::uint32_t> (iter + 1) * 0x9e3779b9U;
      value ^= static_cast<std::uint32_t> (worker + 1) * 0x85ebca6bU;
      value ^= static_cast<std::uint32_t> (stream + 1) * 0xc2b2ae35U;
      return mix_value (value);
    }

    int
    select_index (std::uint32_t token, int upper_bound)
    {
      if (upper_bound <= 0)
	{
	  return 0;
	}
      return static_cast<int> (token % static_cast<std::uint32_t> (upper_bound));
    }

    bool
    select_by_ratio (int ratio, std::uint32_t token)
    {
      const int effective_ratio = std::max (std::min (ratio, 100), 0);
      return static_cast<int> (token % 100U) < effective_ratio;
    }

    const mock_oid &
    select_object_from_bucket (const oid_bucket &bucket, const scenario_config &config, int iter, int worker, int stream)
    {
      const bool use_hot = select_by_ratio (config.hot_ratio, make_token (config, iter, worker, stream));
      const std::uint32_t selection_token = make_token (config, iter, worker, stream + 1);
      if (use_hot)
	{
	  return get_hot_object_from_bucket (bucket, select_index (selection_token,
								   static_cast<int> (bucket.hot_object_count)));
	}
      return get_cold_object_from_bucket (bucket, select_index (selection_token,
								static_cast<int> (bucket.object_oids.size ())));
    }

    const mock_oid &
    select_collision_object (const oid_pool &pool, const oid_bucket &bucket, const scenario_config &config, int iter,
			     int worker, int stream)
    {
      const bool use_collision = select_by_ratio (config.collision_ratio, make_token (config, iter, worker, stream));
      const std::uint32_t selection_token = make_token (config, iter, worker, stream + 1);
      if (use_collision)
	{
	  return pool.hash_collision_oids[static_cast<std::size_t> (select_index (selection_token,
									 static_cast<int> (pool.hash_collision_oids.size ())))];
	}
      return get_object_from_bucket (bucket, select_index (selection_token, static_cast<int> (bucket.object_oids.size ())));
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

  std::string
  to_string (benchmark_output_format format)
  {
    switch (format)
      {
      case benchmark_output_format::csv:
        return "csv";
      case benchmark_output_format::pretty:
        return "pretty";
      case benchmark_output_format::both:
        return "both";
      }

    return "unknown";
  }

  benchmark_output_format
  parse_benchmark_output_format (const std::string &name)
  {
    if (name == "csv")
      {
        return benchmark_output_format::csv;
      }
    if (name == "pretty")
      {
        return benchmark_output_format::pretty;
      }
    if (name == "both")
      {
        return benchmark_output_format::both;
      }

    throw std::invalid_argument ("Unknown benchmark format: " + name);
  }

  oid_pool
  build_oid_pool (const scenario_config &config)
  {
    oid_pool pool;
    const int effective_class_count = std::max (config.class_count, 1);
    const int effective_objects_per_class = std::max (config.objects_per_class, 2);
    const int effective_hotset_size = std::min (std::max (config.hotset_size, 1), effective_objects_per_class);
    const int collision_object_count = std::max (effective_objects_per_class, 2);

    pool.root_class_oid = make_pool_oid (1, 0);
    pool.hot_class_oid = make_pool_oid (100, 0);
    pool.hot_object_oid = make_pool_oid (200, 1);

    pool.buckets.reserve (effective_class_count);
    for (int bucket_index = 0; bucket_index < effective_class_count; bucket_index++)
      {
        oid_bucket bucket;
        bucket.class_oid = make_pool_oid (1000 + bucket_index, 0);
        bucket.hot_object_count = static_cast<std::size_t> (effective_hotset_size);
        bucket.object_oids.reserve (effective_objects_per_class);

        for (int object_index = 0; object_index < effective_objects_per_class; object_index++)
          {
            bucket.object_oids.push_back (make_pool_oid (2000 + bucket_index * 256 + object_index, object_index % 32));
          }

        pool.buckets.push_back (bucket);
      }

    pool.hash_collision_oids.reserve (collision_object_count);
    for (int index = 0; index < collision_object_count; index++)
      {
        pool.hash_collision_oids.push_back (make_pool_oid (6000 + index * DEFAULT_OBJECT_HASH_SIZE, 0));
      }

    return pool;
  }

  std::vector<operation>
  build_operations (const scenario_config &config)
  {
    std::vector<operation> ops;
    const oid_pool pool = build_oid_pool (config);
    const int pair_count = std::max (config.transaction_count / 2, 1);

    for (int iter = 0; iter < config.iterations; iter++)
      {
	const int iteration_barrier = make_barrier_id (iter, 0);
	const int conversion_barrier = make_barrier_id (iter, 1);

		for (int worker = 0; worker < config.transaction_count; worker++)
		  {
		    const int own_txn_id = worker;
		    const int shared_txn_id = worker / 2;
	            const oid_bucket &worker_bucket = get_bucket (pool, worker);
		    const mock_oid &class_oid = worker_bucket.class_oid;

	    switch (config.kind)
	      {
	      case scenario_kind::hot_row:
		ops.push_back (make_begin (worker, own_txn_id));
		ops.push_back (make_barrier (worker, own_txn_id, iteration_barrier));
		ops.push_back (make_lock (worker, own_txn_id, operation_kind::acquire, "X_LOCK", wait_kind::conditional,
					  false, pool.hot_class_oid, pool.hot_object_oid));
		ops.push_back (make_sleep (worker, own_txn_id, 1));
		ops.push_back (make_commit (worker, own_txn_id));
		break;

	      case scenario_kind::hot_class_cold_rows:
		{
		  const int pair_barrier_one = make_barrier_id (iter, 10 + (worker / 2) * 2);
		  const int pair_barrier_two = pair_barrier_one + 1;
	                  const oid_bucket &shared_bucket = get_bucket (pool, worker / 2);
	                  const mock_oid &pair_hot_oid = get_hot_object_from_bucket (shared_bucket,
										   select_index (make_token (config, iter, worker, 10),
												 static_cast<int> (shared_bucket.hot_object_count)));
	                  const mock_oid &pair_peer_oid = select_object_from_bucket (shared_bucket, config, iter, worker, 12);

		ops.push_back (make_begin (worker, shared_txn_id));
		if ((worker % 2) == 0)
		  {
		    ops.push_back (make_lock (worker, shared_txn_id, operation_kind::acquire, "IX_LOCK",
					      wait_kind::conditional, true, shared_bucket.class_oid, shared_bucket.class_oid));
		    ops.push_back (make_lock (worker, shared_txn_id, operation_kind::acquire, "X_LOCK",
					      wait_kind::conditional, false, shared_bucket.class_oid, pair_hot_oid));
		  }
		ops.push_back (make_barrier (worker, shared_txn_id, pair_barrier_one));
		if ((worker % 2) != 0)
		  {
		    ops.push_back (make_lock (worker, shared_txn_id, operation_kind::acquire, "X_LOCK",
					      wait_kind::conditional, false, shared_bucket.class_oid, pair_peer_oid));
		  }
		if ((worker % 2) == 0 && ((iter + worker) % 8) == 0)
		  {
		    ops.push_back (make_lock (worker, shared_txn_id, operation_kind::acquire, "X_LOCK",
					      wait_kind::conditional, true, shared_bucket.class_oid, shared_bucket.class_oid));
		  }
		ops.push_back (make_barrier (worker, shared_txn_id, pair_barrier_two));
		ops.push_back (make_commit (worker, shared_txn_id));
		}
		break;

	      case scenario_kind::lock_conversion:
		{
		  const bool use_hot_conversion = select_by_ratio (config.hot_ratio, make_token (config, iter, worker, 20));
		  const mock_oid &conversion_oid = use_hot_conversion
						    ? pool.hot_object_oid
						    : get_cold_object_from_bucket (worker_bucket,
										  select_index (make_token (config, iter, worker, 21),
													static_cast<int> (worker_bucket.object_oids.size ())));

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
		    const int txn_id = worker;
                    const oid_bucket &deadlock_bucket = get_bucket (pool, 0);
                    const mock_oid &first_oid = get_object_from_bucket (deadlock_bucket, worker);
                    const mock_oid &second_oid = get_object_from_bucket (deadlock_bucket, peer);

		    ops.push_back (make_begin (worker, txn_id));
		    ops.push_back (make_lock (worker, txn_id, operation_kind::acquire, "X_LOCK", wait_kind::conditional,
					      false, deadlock_bucket.class_oid, first_oid));
		    ops.push_back (make_barrier (worker, txn_id, pair_barrier));
		    ops.push_back (make_lock (worker, txn_id, operation_kind::acquire, "X_LOCK",
					      wait_kind::unconditional, false, deadlock_bucket.class_oid, second_oid));
		    ops.push_back (make_commit (worker, txn_id));
		  }
		break;

	      case scenario_kind::hash_collision:
		ops.push_back (make_begin (worker, own_txn_id));
		ops.push_back (make_lock (worker, own_txn_id, operation_kind::acquire, "X_LOCK", wait_kind::conditional,
					  false, pool.buckets[0].class_oid,
	                                          select_collision_object (pool, pool.buckets[0], config, iter, worker, 30)));
		ops.push_back (make_commit (worker, own_txn_id));
		break;

	      case scenario_kind::low_contention:
		ops.push_back (make_begin (worker, own_txn_id));
		ops.push_back (make_lock (worker, own_txn_id, operation_kind::acquire, "X_LOCK", wait_kind::conditional,
					  false, class_oid,
					  get_object_from_bucket (worker_bucket,
								  select_index (make_token (config, iter, worker, 40),
											static_cast<int> (worker_bucket.object_oids.size ())))));
		ops.push_back (make_commit (worker, own_txn_id));
		break;

	      case scenario_kind::escalation_sweep:
                {
                  const oid_bucket &sweep_bucket = get_bucket (pool, worker);
		ops.push_back (make_begin (worker, own_txn_id));
		ops.push_back (make_lock (worker, own_txn_id, operation_kind::acquire, "IX_LOCK", wait_kind::conditional,
					  true, sweep_bucket.class_oid, sweep_bucket.class_oid));
		ops.push_back (make_lock (worker, own_txn_id, operation_kind::acquire, "X_LOCK", wait_kind::conditional,
					  false, sweep_bucket.class_oid,
	                                          get_hot_object_from_bucket (sweep_bucket,
								      select_index (make_token (config, iter, worker, 50),
										    static_cast<int> (sweep_bucket.hot_object_count)))));
		ops.push_back (make_commit (worker, own_txn_id));
                }
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
