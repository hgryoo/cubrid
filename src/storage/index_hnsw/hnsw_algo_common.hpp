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
#include <deque>
#include <string_view>
#include <ankerl/unordered_dense.h>

#include "hnsw_api.hpp"
#include "hnsw_graph_base.hpp"
#include "thread_entry.hpp"
#include "vector_distance.hpp"
#include "environment_variable.h"

namespace cubhnsw
{
  // =====================================================================
  // algo's base structs
  // =====================================================================
  template <typename Traits>
  struct candidate_t
  {
    using slot_id_t = typename Traits::slot_id_t;
    distance_t distance;
    slot_id_t slot;

    candidate_t (distance_t distance, slot_id_t slot): distance (distance), slot (slot) {}
    inline bool operator< (candidate_t other) const noexcept
    {
      return distance < other.distance;
    }
  };

  template <typename Traits>
  struct closer_candidate_t
  {
    bool operator() (candidate_t<Traits> const &a,
		     candidate_t<Traits> const &b) const noexcept
    {
      return a.distance < b.distance; // min-heap or ascending
    }
  };

  using visited_set_t = ankerl::unordered_dense::set<uint64_t>;
  using vector_cache_t = ankerl::unordered_dense::map<uint64_t, std::vector<float>>;

  struct neighbor_cache_entry
  {
    std::vector<uint64_t> neighbors;
    std::vector<float> distances;

    void clear()
    {
      neighbors.clear();
      distances.clear();
    }
  };

  using neighbors_cache_t = ankerl::unordered_dense::map<uint64_t, neighbor_cache_entry>;

  template <typename Traits>
  using candidates_view_t = std::vector<candidate_t<Traits>>;

  template <typename Traits>
  using candidates_allocator_t = std::allocator<candidate_t<Traits>>;

  template <typename Traits>
  using top_candidates_t =
	  sorted_buffer_gt<candidate_t<Traits>, std::less<candidate_t<Traits>>, candidates_allocator_t<Traits>>;

  template <typename Traits>
  using next_candidates_t =
	  max_heap_gt<candidate_t<Traits>, std::less<candidate_t<Traits>>, candidates_allocator_t<Traits>>;


  inline uint64_t
  pack_oid (const OID &oid)
  {
    return (uint64_t (uint32_t (oid.pageid)) << 32) |
	   (uint64_t (uint16_t (oid.slotid)) << 16) |
	   uint64_t (uint16_t (oid.volid));
  }

  inline OID
  unpack_oid (uint64_t key)
  {
    OID oid;

    oid.pageid = int32_t (key >> 32);
    oid.slotid = int16_t ((key >> 16) & 0xFFFF);
    oid.volid  = int16_t (key & 0xFFFF);

    return oid;
  }

  inline uint64_t
  pack_oid_level (const OID &oid, level_t level) noexcept
  {
    return (uint64_t (oid.pageid) << 37)
	   | (uint64_t (uint16_t (oid.slotid)) << 21)
	   | (uint64_t (uint16_t (oid.volid)) << 5)
	   | uint64_t (level & 0x1F);
  }

  inline void
  unpack_oid_level (uint64_t key, OID &oid, level_t &level) noexcept
  {
    level      = key & 0x1F;
    oid.volid  = (key >> 5)  & 0xFFFF;
    oid.slotid = (key >> 21) & 0xFFFF;
    oid.pageid = key >> 37;
  }

  template <typename Traits>
  struct add_result_t
  {
    int error {NO_ERROR};
    typename Traits::slot_id_t result;
  };

  template <typename Traits>
  struct search_result_t
  {
    int error {NO_ERROR};
    candidates_view_t<Traits> results {};
    std::vector<OID> oids {};
  };

  template <typename Traits>
  struct algo_context_t
  {
    top_candidates_t<Traits> m_top_candidates;
    top_candidates_t<Traits> m_top_for_refine;
    next_candidates_t<Traits> m_next_candidates;
    visited_set_t m_visits;
    cubthread::entry *m_thread_p {nullptr};

    // stats
    bool m_is_perf_tracking {false};
    std::size_t m_visited_nodes{};
    std::size_t m_computed_distances{};
    std::size_t m_computed_distances_in_refines{};
    std::size_t m_computed_distances_in_reverse_refines{};

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

    void clear_candidates ()
    {
      m_top_candidates.clear ();
      m_next_candidates.clear();
      m_visits.clear();
    }
  };
}

#endif // _HNSW_ALGO_COMMON_HPP_