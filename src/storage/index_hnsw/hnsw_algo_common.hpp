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
#include "hnsw_graph_base.hpp"
#include "thread_entry.hpp"
#include "vector_distance.hpp"

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

  template <typename T>
  struct visit_set_helper
  {
    using type = ankerl::unordered_dense::set<T>;
  };

  template <>
  struct visit_set_helper<OID>
  {
    using type = ankerl::unordered_dense::set<OID, oid_hash, oid_equal>;
  };

  template <typename Traits>
  using visited_set_t = typename visit_set_helper<typename Traits::slot_id_t>::type;

  template <typename T>
  struct vector_cache_helper
  {
    using type = ankerl::unordered_dense::map<T, std::vector<float>>;
  };

  template <>
  struct vector_cache_helper<OID>
  {
    using type = ankerl::unordered_dense::map<OID, std::vector<float>, oid_hash, oid_equal>;
  };

  template <typename Traits>
  using vector_cache_t = typename vector_cache_helper<typename Traits::slot_id_t>::type;

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

  struct vpid_hash
  {
    inline std::size_t operator() (const VPID &vpid) const noexcept
    {
      return (uint64_t (uint32_t (vpid.pageid)) << 32)
	     |  uint64_t (uint16_t (vpid.volid));
    }
  };

  struct vpid_equal
  {
    inline bool operator() (const VPID &a, const VPID &b) const noexcept
    {
      return a.pageid == b.pageid && a.volid == b.volid;
    }
  };

  struct cached_page_entry
  {
    PAGE_PTR page_ptr {nullptr};
    bool is_exclusive {false};

    // eviction 정책용
    level_t level {0};          // 이 페이지가 관측된 최소 레벨 (0이 가장 “낮음”)
    uint16_t fix_count {0};     // local reuse counter (saturating)
    uint16_t in_use {0};        // pinned_t가 살아있는 동안 보호
    uint32_t last_touch{0};
  };

  using page_cache_t =
	  ankerl::unordered_dense::map<VPID, cached_page_entry, vpid_hash, vpid_equal>;

  template <typename Traits>
  struct algo_context_t
  {
    top_candidates_t<Traits> m_top_candidates;
    top_candidates_t<Traits> m_top_for_refine;
    next_candidates_t<Traits> m_next_candidates;
    visited_set_t<Traits> m_visits;
    cubthread::entry *m_thread_p {nullptr};

    // stats
    bool m_is_perf_tracking {false};
    std::size_t m_visited_nodes{};
    std::size_t m_computed_distances{};
    std::size_t m_computed_distances_in_refines{};
    std::size_t m_computed_distances_in_reverse_refines{};

    static constexpr std::size_t MAX_CACHE_SIZE = 128;
    page_cache_t m_page_cache;

    std::size_t m_page_visits {0};

    // cache stats
    std::size_t m_page_cache_hits {};
    std::size_t m_page_cache_misses {};
    std::size_t m_page_cache_promotes {};
    std::size_t m_page_cache_promote_fallbacks {};
    std::size_t m_page_cache_evictions {};

    ~algo_context_t()
    {
      // print cache stats
      printf ("page cache hits: %zu\n", m_page_cache_hits);
      printf ("page cache misses: %zu\n", m_page_cache_misses);
      printf ("page cache promotes: %zu\n", m_page_cache_promotes);
      printf ("page cache promote fallbacks: %zu\n", m_page_cache_promote_fallbacks);
      printf ("page cache evictions: %zu\n\n", m_page_cache_evictions);

      for (auto &kv : m_page_cache)
	{
	  if (kv.second.page_ptr != nullptr)
	    {
	      pgbuf_unfix (m_thread_p, kv.second.page_ptr);
	    }
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