/*
 *
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

//
// hnsw_algo.hpp
//

#ifndef _HNSW_ALGO_HPP_
#define _HNSW_ALGO_HPP_

#include <functional>

#include "hnsw_api.hpp"
#include "hnsw_utils.hpp"
#include "hnsw_graph_base.hpp"
#include "hnsw_algo_common.hpp"
#include "hnsw_storage.hpp" // storage_t
#include "vector_distance.hpp"
#include "perf_monitor.h"
#include "system_parameter.h"

#define HNSW_ALGO_DEBUG 0
#define HNSW_ALGO_PRINT(fmt, ...) do { if (HNSW_ALGO_DEBUG) { fprintf (stdout, fmt, ##__VA_ARGS__); fflush (stdout); } } while (0)

namespace cubhnsw
{
  /* this class is modified version of the usearch implementation */
  // =====================================================================
  // algo class definition
  // =====================================================================
  template <typename Traits>
  class algo
  {
    public:
      using traits      = Traits;

      using key_id_t    = OID;
      using slot_id_t  = typename traits::slot_id_t;

      using storage_t   = storage<traits>;

      using root_type   = root_t<traits>;
      using node_type   = node_t<traits>;
      using neighbors_ref_type = neighbors_ref_t<traits>;

      using pinned_t = pinned_block_t<Traits, std::function<void (pinned_block_data<Traits>&)>>;

      algo (const hnsw_build_params &build_params);

      // high-level APIs
      add_result_t<Traits> add (cubthread::entry *thread_p, const key_id_t &oid, const float *vector);
      search_result_t<Traits> search (cubthread::entry *thread_p, const float *query, const std::size_t k,
				      const std::size_t expansion);

      void set_storage (storage_t *storage) noexcept
      {
	m_storage = storage;
      }

    protected:

      // horizontal seeking
      int seek_on_layer_ (algo_context_t<Traits> &context, const float *query, const slot_id_t &start_slot,
			  const std::size_t expansion_limit);

      // vertical seeking
      int seek_down_ (algo_context_t<Traits> &context, const float *query, const slot_id_t &start_slot,
		      const level_t target_level,
		      slot_id_t &closest_slot);

      // refining links
      void form_links_to_closest_ (algo_context_t<Traits> &context, const pinned_t &new_slot,
				   candidates_view_t<Traits> &out);
      int form_reverse_links_ (algo_context_t<Traits> &context, const pinned_t &new_slot, const float *value,
			       candidates_view_t<Traits> &new_neighbors);
      void refine_ (algo_context_t<Traits> &context, std::size_t needed, top_candidates_t<Traits> &top,
		    candidates_view_t<Traits> &out, std::size_t &refines_counter) const;

      // random level generation
      level_t choose_random_level_ (std::default_random_engine &generator, double inverse_log_connectivity);

      // distance
      inline distance_t compute_distance_ (algo_context_t<Traits> &context, const float *v1, const float *v2) const
      {
	if (context.m_is_perf_tracking)
	  {
	    context.m_stats.computed_distances++;
	    if (context.m_level == 0)
	      {
		context.m_stats.computed_distances_l0++;
	      }
	  }
	return metric_table[static_cast<size_t> (m_metric)] (v1, v2, m_dimension);
      }

      inline distance_t compute_distance_from_query_ (algo_context_t<Traits> &context, const float *query,
	  const slot_id_t &slot) const
      {
	const float *vec = m_storage->get_vector_by_slot_id (context, slot, lock_mode::shared);
	return compute_distance_ (context, query, vec);
      }

      inline distance_t compute_distance_between (algo_context_t<Traits> &context, const slot_id_t &a,
	  const slot_id_t &b) const
      {
	const float *avec = m_storage->get_vector_by_slot_id (context, a, lock_mode::shared);
	const float *bvec = m_storage->get_vector_by_slot_id (context, b, lock_mode::shared);
	return compute_distance_ (context, avec, bvec);
      }

      inline neighbors_ref_type get_neighbors (const pinned_t &node_blk, const level_t level)
      {
	node_type node = node_type (node_blk->data);
	neighbors_ref_type neighbors = neighbors_ref_type (node.neighbors_tape() + m_storage->node_neighbors_offset_ (level));

	HNSW_ALGO_PRINT ("[node] node: %s\n", node.dump().c_str());
	HNSW_ALGO_PRINT ("[neighbors of level %d] neighbors: %s\n", (int)level, neighbors.dump().c_str());

	return neighbors;
      }

      // variables
      storage_t *m_storage {nullptr};

      // from build_params
      vector_distance_metric_t m_metric;
      std::size_t m_dimension;
      std::size_t m_connectivity;
      std::size_t m_expansion;

      std::default_random_engine m_level_generator {std::random_device{}()};

      std::size_t m_debug_group_start {0};
      std::size_t m_debug_cnt {0};

      // precomputed
      double m_inverse_log_connectivity;
  };

  // =====================================================================
  // algo class implementation
  // =====================================================================

  template <typename Traits>
  algo<Traits>::algo (const hnsw_build_params &build_params)
    : m_dimension ((size_t) build_params.dimension), m_connectivity (build_params.m),
      m_expansion (build_params.ef_construction)
  {
    switch (build_params.metric)
      {
      case METRIC_COSINE:
	m_metric = vector_distance_metric_t::COSINE;
	break;
      case METRIC_EUCLIDEAN:
	m_metric = vector_distance_metric_t::EUCLIDEAN;
	break;
      case METRIC_DOT:
	m_metric = vector_distance_metric_t::DOT;
	break;
      default:
	assert (false);
      }

    // precompute inverse log connectivity
    m_inverse_log_connectivity = 1.0 / std::log (static_cast<double> (build_params.m));
  }

  template <typename Traits>
  add_result_t<Traits>
  algo<Traits>::add (cubthread::entry *thread_p, const key_id_t &key, const float *vector)
  {
    add_result_t<Traits> result;

    algo_context_t<Traits> context;
    context.m_thread_p = thread_p;
    context.m_is_perf_tracking = perfmon_is_perf_tracking ();
    context.m_is_debugging = prm_get_integer_value (PRM_ID_VECTOR_INDEX_DEBUG) != 0;

    context.clear_candidates();

    std::size_t connectivity_max = m_connectivity * 2 + 1;

    // pre-reserve top_for_refine
    context.m_top_for_refine.reserve (connectivity_max);

    // pre-reserve for visits
    context.m_visits.reserve (connectivity_max);

    top_candidates_t<Traits> &top = context.m_top_candidates;
    next_candidates_t<Traits> &next = context.m_next_candidates;

    // TODO: now, connectivity_base is not considered.
    // std::size_t connecitvity_max = m_connectivity;
    std::size_t top_limit = std::max (connectivity_max, m_expansion);
    if (!top.reserve (top_limit) || !next.reserve (top_limit))
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, top_limit * sizeof (candidate_t<Traits>));
	assert (false);
	return {ER_FAILED, OID_INITIALIZER};
      }

    level_t curr_max_level, new_target_level;
    slot_id_t entry_slot, new_slot;

    pinned_t root_block = m_storage->get_root (context, lock_mode::exclusive);
    root_type root_node = root_type (root_block->data);
    {
      curr_max_level = root_node.get_level(); // get max_level from root page
      new_target_level = choose_random_level_ (m_level_generator, m_inverse_log_connectivity);
      entry_slot = root_node.get_entry();
      if (new_target_level > MAX_LEVELS)
	{
	  // TODO: for optimzation, if new_target_level is greater than max_level, we can just use max_level
	  new_target_level = MAX_LEVELS;
	}

      if (context.m_is_debugging)
	{
	  if (new_target_level > curr_max_level)
	    {
	      m_debug_group_start = m_debug_cnt;
	    }
	  context.open_debug_file (m_debug_group_start, m_debug_cnt, std::max (curr_max_level, new_target_level));
	}

      if (m_metric == vector_distance_metric_t::COSINE)
	{
	  if (!cubvec_cosine_normalize ((float *) vector, m_dimension))
	    {
	      abort ();
	    }
	}
      //
      new_slot = m_storage->add_node (context, key, vector, new_target_level);
      //

      if (m_storage->is_empty())
	{
	  {
	    //pinned_t cleanup {std::move (root_block)};
	  }

	  //pinned_t promoted_root = m_storage->get_root (lock_mode::exclusive);
	  //root_type promoted_root_node = root_type (promoted_root.data());
	  root_node.set_entry (new_slot);
	  root_node.set_level (new_target_level);
	  m_storage->set_empty (false);
	  return result;
	}
    }

    if (new_target_level <= curr_max_level)
      {
	// TODO (investigate): it is safe to unlock root here.
	pinned_t cleanup {std::move (root_block)};
      }


    {
      slot_id_t closest_slot {};
      {
	// TODO: error handling
	context.m_level = curr_max_level;
	(void) seek_down_ (context, vector, entry_slot, new_target_level, closest_slot);
      }

      assert (context.m_level == (std::min) (new_target_level, curr_max_level));

      pinned_t new_node_blk = m_storage->get_node_by_slot_id (context, new_slot, lock_mode::exclusive);

      while (context.m_level >= 0)
	{
	  (void) seek_on_layer_ (context, vector, closest_slot,top_limit);

	  candidates_view_t<Traits> closest_view;
	  {
	    std::vector<slot_id_t> *cached_neighbors = m_storage->get_neighbors_cached_ids (context, new_slot, context.m_level);
	    if (cached_neighbors != nullptr)
	      {
		cached_neighbors->clear();
	      }
#if 0
	    neighbors_ref_type neighbors = get_neighbors (new_node_blk, level);
	    neighbors.clear();
#endif

	    form_links_to_closest_ (context, new_node_blk, closest_view);
	    closest_slot = closest_view[0].slot;
	  }
	  form_reverse_links_ (context, new_node_blk, vector, closest_view);

	  if (context.m_is_debugging)
	    {
	      fprintf (context.m_debug_fp, "level: %d\n", context.m_level);
	      if (!context.m_accessed_nodes.empty ())
		{
		  for (const auto &node : context.m_accessed_nodes)
		    {
		      fprintf (context.m_debug_fp, "(%s) -> ", node.data ());
		    }
		  fprintf (context.m_debug_fp, "END \n");
		  context.m_accessed_nodes.clear();
		}
	    }

	  --context.m_level;
	}
    }

    if (new_target_level > curr_max_level)
      {
	HNSW_ALGO_PRINT ("[add] promotion required: new_target_level: %d, curr_max_level: %d\n", (int)new_target_level,
			 (int)curr_max_level);
	// TODO: latch promotion is required
	{
	  m_storage->promote_root (root_block);
	}
	root_node.set_entry (new_slot);
	root_node.set_level (new_target_level);
	m_storage->set_empty (false);

	context.m_stats.entrypoint_updates++;
      }

    if (context.m_is_debugging)
      {
	context.close_debug_file();
      }

    // recommend the function name "add_perf_stats"
    context.collect_perf_stats();

    return result;
  }

  template <typename Traits>
  search_result_t<Traits>
  algo<Traits>::search (cubthread::entry *thread_p, const float *query, const std::size_t k, const std::size_t expansion)
  {
    search_result_t<Traits> result;
    if (k == 0)
      {
	return result;
      }

    algo_context_t<Traits> context;
    context.m_thread_p = thread_p;
    context.clear_candidates();

    top_candidates_t<Traits> &top = context.m_top_candidates;
    next_candidates_t<Traits> &next = context.m_next_candidates;

    std::size_t expansion_size = std::max (k, expansion);

    // pre-reserve for visits
    context.m_visits.reserve (expansion_size);

    // pre-reserve for top and next
    if (!top.reserve (expansion_size) || !next.reserve (expansion_size))
      {
	er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, expansion_size * sizeof (candidate_t<Traits>));
	assert (false);
	return result;
      }

    slot_id_t entry_slot;
    level_t root_level;
    {
      pinned_t root_block = m_storage->get_root (context, lock_mode::shared);
      root_type root_node = root_type (root_block->data);
      entry_slot = root_node.get_entry();
      root_level = root_node.get_level();
    }

    if (m_metric == vector_distance_metric_t::COSINE)
      {
	if (!cubvec_cosine_normalize ((float *) query, m_dimension))
	  {
	    abort ();
	  }
      }

    slot_id_t closest_slot;

    context.m_level = root_level;
    if (seek_down_ (context, query, entry_slot, /* target level */ 0, closest_slot) != NO_ERROR)
      {
	// TODO: error handling
	assert (false);
      }

    // search from base level (0)
    assert (context.m_level == 0);

    if (seek_on_layer_ (context, query, closest_slot, expansion_size) != NO_ERROR)
      {
	// TODO: error handling
	assert (false);
      }

    // not to collect stats after seeking on base layer (0-level)
    context.m_level--;

    top.sort_ascending();
    top.shrink (k);

    result.results.assign (top.data(), top.data() + top.size());
    for (std::size_t i = 0; i < top.size (); ++i)
      {
	pinned_t node_blk = m_storage->get_node_by_slot_id (context, result.results[i].slot, lock_mode::shared);
	result.oids.push_back (node_type (node_blk->data).get_key());
      }

    context.collect_perf_stats();

    return result;
  }

  template <typename Traits>
  int
  algo<Traits>::seek_on_layer_ (algo_context_t<Traits> &context, const float *query, const slot_id_t &start_slot,
				const std::size_t expansion_limit)
  {
    next_candidates_t<Traits> &next = context.m_next_candidates;
    top_candidates_t<Traits> &top = context.m_top_candidates;
    visited_set_t<Traits> &visits = context.m_visits;

    context.clear_candidates();

    distance_t radius = compute_distance_from_query_ (context, query, start_slot);

    next.insert_reserved (candidate_t<Traits> (-radius, start_slot));
    top.insert_reserved (candidate_t<Traits> (radius, start_slot));
    visits.insert (start_slot);

    // ------------------------------------------------------------------
    // Neighbor expansion pruning:
    // - Standard HNSW termination: stop when the closest candidate in "next"
    //   is already worse than current radius (worst in "top") while "top" is full.
    // - Additional cap: do not expand more than `expansion_limit` candidates.
    //   This bounds the number of neighbor expansions (and thus distance calls)
    //   under degenerate graph layouts / cache effects.
    // ------------------------------------------------------------------
    std::size_t expanded = 0;
    while (!next.empty ())
      {
	candidate_t candidacy = next.top ();
	if ((-candidacy.distance) > radius && top.size () == expansion_limit)
	  {
	    break;
	  }

	// Hard cap on number of expanded candidates at this layer.
	if (expanded >= expansion_limit)
	  {
	    break;
	  }
	expanded++;

	next.pop ();

	slot_id_t candidate_slot = candidacy.slot;

	// Try neighbors cache first (disk storage); fallback to direct neighbors_ref_type.
	const std::vector<slot_id_t> *cached_neighbors =
		m_storage->get_neighbors_cached_ids (context, candidate_slot, context.m_level);

	if (cached_neighbors != nullptr)
	  {
	    for (slot_id_t successor_slot : *cached_neighbors)
	      {
		auto [it, inserted] = visits.insert (successor_slot);
		if (!inserted)
		  {
		    continue;
		  }

		distance_t sucessor_dist = compute_distance_from_query_ (context, query, successor_slot);
		if (top.size () < expansion_limit || sucessor_dist < radius)
		  {
		    next.insert (candidate_t<Traits> (-sucessor_dist, successor_slot));
		    top.insert (candidate_t<Traits> (sucessor_dist, successor_slot), expansion_limit);
		    radius = top.top ().distance;

		    HNSW_ALGO_PRINT ("[search_to_insert] radius: %f\n", radius);
		    HNSW_ALGO_PRINT ("[search_to_insert] sucessor_dist: %f\n", sucessor_dist);
		    HNSW_ALGO_PRINT ("[search_to_insert] top.size(), expansion_limit: %zu, %zu\n", top.size(), expansion_limit);
		  }
	      }
	    continue;
	  }

	// No cache: load node and neighbors directly.
	pinned_t candidate_node_blk = m_storage->get_node_by_slot_id (context, candidate_slot, lock_mode::shared);
	neighbors_ref_type candidate_neighbors = get_neighbors (candidate_node_blk, context.m_level);
	for (std::size_t i = 0; i < candidate_neighbors.size (); ++i)
	  {
	    slot_id_t successor_slot = candidate_neighbors.at (i);

	    auto [it, inserted] = visits.insert (successor_slot);
	    if (!inserted)
	      {
		continue;
	      }

	    distance_t sucessor_dist = compute_distance_from_query_ (context, query, successor_slot);
	    if (top.size () < expansion_limit || sucessor_dist < radius)
	      {
		next.insert (candidate_t<Traits> (-sucessor_dist, successor_slot));
		top.insert (candidate_t<Traits> (sucessor_dist, successor_slot), expansion_limit);
		radius = top.top ().distance;

		HNSW_ALGO_PRINT ("[search_to_insert] radius: %f\n", radius);
		HNSW_ALGO_PRINT ("[search_to_insert] sucessor_dist: %f\n", sucessor_dist);
		HNSW_ALGO_PRINT ("[search_to_insert] top.size(), expansion_limit: %zu, %zu\n", top.size(), expansion_limit);
	      }
	  }
      }

    return NO_ERROR;
  }

  template <typename Traits>
  int
  algo<Traits>::seek_down_ (algo_context_t<Traits> &context, const float *query, const slot_id_t &start_slot,
			    const level_t target_level, slot_id_t &out_slot)
  {
    visited_set_t<Traits> &visits = context.m_visits;
    visits.clear ();

    slot_id_t closest_slot = start_slot;
    distance_t closest_dist = compute_distance_from_query_ (context, query, closest_slot);
    for (; context.m_level > target_level; --context.m_level)
      {
	bool changed = false;
	do
	  {
	    changed = false;

	    // Try neighbors cache first; fallback to direct neighbors_ref_type.
	    const std::vector<slot_id_t> *cached_neighbors =
		    m_storage->get_neighbors_cached_ids (context, closest_slot, context.m_level);
	    if (cached_neighbors != nullptr)
	      {
		for (slot_id_t neighbor_id : *cached_neighbors)
		  {
		    distance_t candidate_dist = compute_distance_from_query_ (context, query, neighbor_id);
		    if (candidate_dist < closest_dist)
		      {
			closest_dist = candidate_dist;
			closest_slot = neighbor_id;
			changed = true;
		      }
		  }
	      }
	    else
	      {
		pinned_t closest_node_blk = m_storage->get_node_by_slot_id (context, closest_slot, lock_mode::shared);

		neighbors_ref_type neighbors = get_neighbors (closest_node_blk, context.m_level);
		for (std::size_t i = 0; i < neighbors.size (); ++i)
		  {
		    slot_id_t neighbor_id = neighbors.at (i);

		    distance_t candidate_dist = compute_distance_from_query_ (context, query, neighbor_id);
		    if (candidate_dist < closest_dist)
		      {
			closest_dist = candidate_dist;
			closest_slot = neighbor_id;
			changed = true;
		      }
		  }
	      }

	  }
	while (changed);
      }

    out_slot = closest_slot;
    return NO_ERROR;
  }

  template <typename Traits>
  void
  algo<Traits>::form_links_to_closest_ (algo_context_t<Traits> &context, const pinned_t &new_node_blk,
					candidates_view_t<Traits> &top_view)
  {
    top_candidates_t<Traits> &top = context.m_top_candidates;

    std::size_t level = context.m_level;
    std::size_t layer_connectivity = context.layer_connectivity (level, m_connectivity);

    std::size_t stat_computed_distance = 0;
    refine_ (context, layer_connectivity, top, top_view, stat_computed_distance);
    context.m_stats.computed_distances_in_refines += stat_computed_distance;
    if (context.m_level == 0)
      {
	context.m_stats.computed_distances_in_refines_l0 += stat_computed_distance;
      }

    // outgoing links from new node
    std::vector<slot_id_t> *cached_neighbors = m_storage->get_neighbors_cached_ids (context, new_node_blk->id,
	context.m_level);
    if (cached_neighbors != nullptr)
      {
	for (std::size_t i = 0; i != top_view.size(); i++)
	  {
	    cached_neighbors->push_back (top_view[i].slot);
	  }
      }

#if 0
    neighbors_ref_type new_neighbors = get_neighbors (new_node_blk, level);
    for (std::size_t i = 0; i != top_view.size(); i++)
      {
	new_neighbors.push_back (top_view[i].slot);
      }

    // neighbors of new node changed, refresh neighbors cache (if storage supports it)
    m_storage->refresh_neighbors_cache (context, new_node_blk->id, level);
#endif
  }

  template <typename Traits>
  int
  algo<Traits>::form_reverse_links_ (algo_context_t<Traits> &context, const pinned_t &new_node_blk, const float *value,
				     candidates_view_t<Traits> &new_neighbors)
  {
    static constexpr float REVERSE_REFINE_PRUNE_RATIO = 1.10f;

    std::size_t level = context.m_level;
    std::size_t layer_connectivity = context.layer_connectivity (level, m_connectivity);

    slot_id_t new_slot = new_node_blk->id;

    for (auto n : new_neighbors)
      {
	slot_id_t close_slot = n.slot;
	if (close_slot == new_slot)
	  {
	    continue;
	  }

	// lock close node once
	pinned_t close_node_blk = m_storage->get_node_by_slot_id (context, close_slot, lock_mode::exclusive);
	neighbors_ref_type close_header = get_neighbors (close_node_blk, level);

	// fast path: still has room
	if (close_header.size () < layer_connectivity)
	  {
	    close_header.push_back (new_slot);
	    continue;
	  }

	// ------------------------------------------------------------------
	// reverse refine pruning
	//
	// If the new node is clearly worse than the worst current neighbor
	// (from the close node's perspective), skip the expensive refine_().
	//
	// This is an approximate pruning heuristic for build-time optimization.
	// ------------------------------------------------------------------
	distance_t dist_new = compute_distance_from_query_ (context, value, close_slot);

	distance_t worst_existing = distance_t {};
	bool has_existing = false;

	for (std::size_t i = 0; i < close_header.size (); ++i)
	  {
	    slot_id_t successor_slot = close_header.at(i);
	    if (successor_slot == new_slot)
	      {
		continue;
	      }

	    distance_t d = compute_distance_between (context, close_slot, successor_slot);

	    if (!has_existing || d > worst_existing)
	      {
		worst_existing = d;
		has_existing = true;
	      }
	  }

	// if all current neighbors are valid and the new one is significantly
	// worse than the worst existing neighbor, do not run full refine.
	if (has_existing
	    && dist_new >= worst_existing * REVERSE_REFINE_PRUNE_RATIO)
	  {
	    continue;
	  }

	top_candidates_t<Traits> &top_for_refine = context.m_top_for_refine;
	top_for_refine.clear ();

	top_for_refine.insert_reserved (candidate_t<Traits> (dist_new, new_slot));

	for (std::size_t i = 0; i < close_header.size (); ++i)
	  {
	    slot_id_t successor_slot = close_header.at(i);
	    distance_t d = compute_distance_between (context, close_slot, successor_slot);
	    top_for_refine.insert_reserved (candidate_t<Traits> (d, successor_slot));
	  }

	close_header.clear ();

	candidates_view_t<Traits> top_view;
	std::size_t stat_computed_distance = 0;

	(void) refine_ (context,
			layer_connectivity,
			top_for_refine,
			top_view,
			stat_computed_distance);

	context.m_stats.computed_distances_in_reverse_refines += stat_computed_distance;
	if (context.m_level == 0)
	  {
	    context.m_stats.computed_distances_in_reverse_refines_l0 += stat_computed_distance;
	  }

	for (std::size_t i = 0; i != top_view.size (); ++i)
	  {
	    close_header.push_back (top_view[i].slot);
	  }
      }

    return NO_ERROR;
  }

  template <typename Traits>
  void
  algo<Traits>::refine_ (algo_context_t<Traits> &context, std::size_t needed, top_candidates_t<Traits> &top,
			 candidates_view_t<Traits> &out, std::size_t &refines_counter) const
  {
    out = {};
    std::size_t old_computed_distances = 0;

    candidate_t<Traits> *top_data = top.data();
    std::size_t const top_count = top.size();
    if (top_count < needed)
      {
	out.assign (top_data, top_data + top_count);
	return;
      }

    top.sort_ascending();

    if (context.m_is_perf_tracking)
      {
	old_computed_distances = context.m_stats.computed_distances;
      }

    // Heuristic neighbor pruning (a.k.a. "diversified" selection in HNSW):
    // keep a candidate only if it is not closer to any already selected neighbor
    // than it is to the base point (represented by `candidate.distance`).
    std::vector<candidate_t<Traits>> discarded;
    discarded.reserve (top_count);

    std::size_t selected_count = 0;
    for (std::size_t i = 0; i < top_count && selected_count < needed; ++i)
      {
	candidate_t<Traits> candidate = top_data[i];
	bool good = true;

	for (std::size_t j = 0; j < selected_count; ++j)
	  {
	    const candidate_t<Traits> &selected = top_data[j];
	    distance_t inter_dist = compute_distance_between (context, candidate.slot, selected.slot);
	    if (inter_dist < candidate.distance)
	      {
		good = false;
		break;
	      }
	  }

	if (good)
	  {
	    top_data[selected_count++] = candidate;
	  }
	else
	  {
	    discarded.push_back (candidate);
	  }
      }

    // If we could not gather enough diverse neighbors, keep some pruned ones
    // to preserve connectivity (standard HNSW option).
    for (std::size_t i = 0; selected_count < needed && i < discarded.size (); ++i)
      {
	top_data[selected_count++] = discarded[i];
      }

    if (context.m_is_perf_tracking)
      {
	refines_counter = context.m_stats.computed_distances - old_computed_distances;
      }

    top.shrink (selected_count);
    out.assign (top_data, top_data + selected_count);
    // ======================================================================================
  }

  template <typename Traits>
  level_t
  algo<Traits>::choose_random_level_ (std::default_random_engine &generator, double inverse_log_connectivity)
  {
    std::uniform_real_distribution<double> distribution (0.0, 1.0);
    double r = -std::log (distribution (generator)) * inverse_log_connectivity;
    return (level_t)r;
  }
}

#endif
