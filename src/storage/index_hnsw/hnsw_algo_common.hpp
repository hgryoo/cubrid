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
// hnsw_algo_common.hpp
//

#ifndef _HNSW_ALGO_COMMON_HPP_
#define _HNSW_ALGO_COMMON_HPP_

#include <random>
#include <ankerl/unordered_dense.h>

#include "hnsw_api.hpp"
#include "hnsw_utils.hpp"
#include "thread_entry.hpp"
#include "vector_distance.hpp"
#include "environment_variable.h"

namespace cubhnsw
{
  // =====================================================================
  // algo's base structs
  // =====================================================================
  using index_id_t = BTID;
  using block_group_id_t = VFID;
  using block_id_t = VPID;
  using slot_id_t = OID;
  using key_id_t = OID;

  using level_t = int16_t;

  constexpr level_t MAX_LEVELS = 16;

  struct candidate_t
  {
    distance_t distance;
    slot_id_t slot;

    candidate_t (distance_t distance, slot_id_t slot): distance (distance), slot (slot) {}
    inline bool operator< (candidate_t other) const noexcept
    {
      return distance < other.distance;
    }
  };

  struct closer_candidate_t
  {
    bool operator() (candidate_t const &a,
		     candidate_t const &b) const noexcept
    {
      return a.distance < b.distance; // min-heap or ascending
    }
  };

  struct oid_hash
  {
    inline std::size_t operator() (const OID &o) const noexcept
    {
      // bit packing of oid
      return (uint64_t (uint32_t (o.pageid)) << 32)
	     | (uint64_t (uint16_t (o.slotid)) << 16)
	     |  uint64_t (uint16_t (o.volid));
    }
  };

  struct oid_equal
  {
    inline bool operator() (const OID &a, const OID &b) const noexcept
    {
      return a.pageid == b.pageid && a.slotid == b.slotid && a.volid == b.volid;
    }
  };

  struct visit_set_helper
  {
    using type = ankerl::unordered_dense::set<OID, oid_hash, oid_equal>;
  };

  using visited_set_t = visit_set_helper::type;

  struct vector_cache_helper
  {
    using type = ankerl::unordered_dense::map<OID, std::vector<float>, oid_hash, oid_equal>;
  };

  using vector_cache_t = vector_cache_helper::type;

  struct neighbors_key
  {
    slot_id_t slot;
    level_t level;

    bool operator== (const neighbors_key &o) const noexcept
    {
      static constexpr oid_equal eq {};
      return level == o.level && eq (slot, o.slot);
    }
  };

  struct neighbors_key_hash
  {
    std::size_t operator() (neighbors_key const &k) const noexcept
    {
      std::size_t h = oid_hash {} (k.slot);
      std::size_t x = std::hash<level_t> {} (k.level);
      h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      return h;
    }
  };

  using neighbors_cache_t =
	  ankerl::unordered_dense::map<neighbors_key, std::vector<slot_id_t>, neighbors_key_hash>;

  using candidates_view_t = std::vector<candidate_t>;

  using candidates_allocator_t = std::allocator<candidate_t>;

  using top_candidates_t =
	  sorted_buffer_gt<candidate_t, std::less<candidate_t>, candidates_allocator_t>;

  using next_candidates_t =
	  max_heap_gt<candidate_t, std::less<candidate_t>, candidates_allocator_t>;

  struct add_result_t
  {
    int error {NO_ERROR};
    slot_id_t result;
  };

  struct search_result_t
  {
    int error {NO_ERROR};
    candidates_view_t results {};
    std::vector<OID> oids {};
  };

  struct graph_profile_t
  {
    std::array<std::atomic<size_t>, MAX_LEVELS> nodes_per_level;
    std::array<std::atomic<size_t>, MAX_LEVELS> degree_sum_per_level;

    level_t max_level {0};
    std::atomic<std::size_t> total_nodes;

    graph_profile_t ()
    {
      total_nodes.store (0, std::memory_order_relaxed);
      for (size_t i = 0; i < MAX_LEVELS; ++i)
	{
	  nodes_per_level[i].store (0, std::memory_order_relaxed);
	  degree_sum_per_level[i].store (0, std::memory_order_relaxed);
	}
    }

  };

  struct algo_context_t
  {
    top_candidates_t m_top_candidates;
    top_candidates_t m_top_for_refine;
    next_candidates_t m_next_candidates;
    visited_set_t m_visits;

    cubthread::entry *m_thread_p {nullptr};
    level_t m_level {0};

    // stats
    bool m_is_perf_tracking {false};
    bool m_is_debugging {false};

    FILE *m_debug_fp {nullptr};
    std::vector<std::string> m_accessed_nodes; // for debug

    void open_debug_file (std::size_t level_start_debug_cnt, std::size_t debug_cnt, int level)
    {
      char path[PATH_MAX];
      if (!m_is_debugging)
	{
	  return;
	}

      constexpr std::size_t GROUP_SIZE = 10000;
      std::size_t group_start =
	      level_start_debug_cnt +
	      ((debug_cnt - level_start_debug_cnt) / GROUP_SIZE) * GROUP_SIZE;

      std::string filename =
	      "hnsw_debug_" +
	      std::to_string (group_start) +
	      "_L" + std::to_string (level) +
	      ".log";

      envvar_tmpdir_file (path, PATH_MAX, filename.c_str());

      m_debug_fp = fopen (path, "a");
    }

    void close_debug_file()
    {
      if (m_debug_fp)
	{
	  fclose (m_debug_fp);
	  m_debug_fp = nullptr;
	}
    }

    struct stats
    {
      // ===========================
      // base stats
      // ===========================
      std::size_t visited_nodes{};
      std::size_t computed_distances{};
      std::size_t computed_distances_in_refines{};
      std::size_t computed_distances_in_reverse_refines{};

      std::size_t candidates_push{};
      std::size_t candidates_pop{};
      std::size_t candidates_prune{};
      std::size_t neighbors_scan{};

      std::size_t page_access{};
      std::size_t vector_access{};
      std::size_t vector_cache_hit{};
      std::size_t vector_cache_miss{};

      std::size_t neighbors_cache_hits{};
      std::size_t neighbors_page_fixes{};

      // ===========================
      // layer 0 stats
      // ===========================
      std::size_t visited_nodes_l0{};
      std::size_t computed_distances_l0{};
      std::size_t computed_distances_in_refines_l0{};
      std::size_t computed_distances_in_reverse_refines_l0{};

      std::size_t candidates_push_l0{};
      std::size_t candidates_pop_l0{};
      std::size_t candidates_prune_l0{};
      std::size_t neighbors_scan_l0{};

      std::size_t page_access_l0{};
      std::size_t vector_access_l0{};
      std::size_t vector_cache_hit_l0{};
      std::size_t vector_cache_miss_l0{};

      std::size_t neighbors_cache_hits_l0{};
      std::size_t neighbors_page_fixes_l0{};

      // ===========================
      // entry point
      // ===========================
      std::size_t entrypoint_updates{};
    };

    stats m_stats;

    void clear_candidates ()
    {
      m_top_candidates.clear ();
      m_next_candidates.clear();
      m_visits.clear();
    }

    void collect_perf_stats ()
    {
      if (!m_is_perf_tracking)
	{
	  return;
	}

      auto add_stat_if_positive = [this] (PERF_STAT_ID stat_id, std::int64_t value)
      {
	if (value > 0)
	  {
	    perfmon_add_stat (m_thread_p, stat_id, value);
	  }
      };

      add_stat_if_positive (PSTAT_HNSW_NUM_VISITED_NODE, m_stats.visited_nodes);
      add_stat_if_positive (PSTAT_HNSW_NUM_COMPUTED_DISTANCES, m_stats.computed_distances);
      add_stat_if_positive (PSTAT_HNSW_NUM_COMPUTED_DISTANCES_IN_REFINES,
			    m_stats.computed_distances_in_refines);
      add_stat_if_positive (PSTAT_HNSW_NUM_COMPUTED_DISTANCES_IN_REVERSE_REFINES,
			    m_stats.computed_distances_in_reverse_refines);

      add_stat_if_positive (PSTAT_HNSW_NUM_VISITED_NODE_L0, m_stats.visited_nodes_l0);
      add_stat_if_positive (PSTAT_HNSW_NUM_COMPUTED_DISTANCES_L0, m_stats.computed_distances_l0);
      add_stat_if_positive (PSTAT_HNSW_NUM_COMPUTED_DISTANCES_IN_REFINES_L0,
			    m_stats.computed_distances_in_refines_l0);
      add_stat_if_positive (PSTAT_HNSW_NUM_COMPUTED_DISTANCES_IN_REVERSE_REFINES_L0,
			    m_stats.computed_distances_in_reverse_refines_l0);

      add_stat_if_positive (PSTAT_HNSW_NUM_PAGE_ACCESS, m_stats.page_access);
      add_stat_if_positive (PSTAT_HNSW_NUM_PAGE_ACCESS_L0, m_stats.page_access_l0);

      add_stat_if_positive (PSTAT_HNSW_NUM_VECTOR_ACCESS, m_stats.vector_access);
      add_stat_if_positive (PSTAT_HNSW_NUM_VECTOR_ACCESS_L0, m_stats.vector_access_l0);

      add_stat_if_positive (PSTAT_HNSW_NUM_VECTOR_CACHE_HIT, m_stats.vector_cache_hit);
      add_stat_if_positive (PSTAT_HNSW_NUM_VECTOR_CACHE_HIT_L0, m_stats.vector_cache_hit_l0);

      add_stat_if_positive (PSTAT_HNSW_NUM_VECTOR_CACHE_MISS, m_stats.vector_cache_miss);
      add_stat_if_positive (PSTAT_HNSW_NUM_VECTOR_CACHE_MISS_L0, m_stats.vector_cache_miss_l0);

      add_stat_if_positive (PSTAT_HNSW_NUM_ENTRYPOINT_UPDATES, m_stats.entrypoint_updates);

      add_stat_if_positive (PSTAT_HNSW_NUM_CANDIDATES_PUSH, m_stats.candidates_push);
      add_stat_if_positive (PSTAT_HNSW_NUM_CANDIDATES_POP, m_stats.candidates_pop);
      add_stat_if_positive (PSTAT_HNSW_NUM_CANDIDATES_PRUNE,
			    m_stats.candidates_prune);
      add_stat_if_positive (PSTAT_HNSW_NUM_NEIGHBORS_SCAN,
			    m_stats.neighbors_scan);

      add_stat_if_positive (PSTAT_HNSW_NUM_CANDIDATES_PUSH_L0, m_stats.candidates_push_l0);
      add_stat_if_positive (PSTAT_HNSW_NUM_CANDIDATES_POP_L0, m_stats.candidates_pop_l0);
      add_stat_if_positive (PSTAT_HNSW_NUM_CANDIDATES_PRUNE_L0,
			    m_stats.candidates_prune_l0);
      add_stat_if_positive (PSTAT_HNSW_NUM_NEIGHBORS_SCAN_L0,
			    m_stats.neighbors_scan_l0);

      add_stat_if_positive (PSTAT_HNSW_NUM_NEIGHBORS_CACHE_HIT,
			    m_stats.neighbors_cache_hits);
      add_stat_if_positive (PSTAT_HNSW_NUM_NEIGHBORS_PAGE_FIX,
			    m_stats.neighbors_page_fixes);

      add_stat_if_positive (PSTAT_HNSW_NUM_NEIGHBORS_CACHE_HIT_L0,
			    m_stats.neighbors_cache_hits_l0);
      add_stat_if_positive (PSTAT_HNSW_NUM_NEIGHBORS_PAGE_FIX_L0,
			    m_stats.neighbors_page_fixes_l0);

      // stop tracking after collecting stats
      m_is_perf_tracking = false;
    }

    inline void add_stat (std::size_t &stat, std::size_t &stat_l0, int counter)
    {
      if (!m_is_perf_tracking)
	{
	  return;
	}

      stat += counter;

      if (m_level == 0)
	{
	  stat_l0 += counter;
	}
    }

    inline void add_stat (std::size_t &stat, int counter)
    {
      if (!m_is_perf_tracking)
	{
	  return;
	}

      stat += counter;
    }
  };
}

#endif // _HNSW_ALGO_COMMON_HPP_