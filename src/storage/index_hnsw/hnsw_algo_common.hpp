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

#include <atomic>
#include <array>

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

  struct vpid_hash
  {
    inline std::size_t operator() (const VPID &v) const noexcept
    {
      return (uint64_t (uint32_t (v.pageid)) << 32)
	     |  uint64_t (uint16_t (v.volid));
    }
  };

  struct vpid_equal
  {
    inline bool operator() (const VPID &a, const VPID &b) const noexcept
    {
      return a.pageid == b.pageid && a.volid == b.volid;
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

  struct cached_page
  {
    PAGE_PTR page_ptr{nullptr};
    uint16_t local_score{1};        // cache-local frequency-ish
    uint32_t last_touch{0};   // optional: aging용 tick
  };

  struct page_cache_helper
  {
    using cache_type = ankerl::unordered_dense::map<VPID, cached_page, vpid_hash, vpid_equal>;
    using access_count_type = ankerl::unordered_dense::map<VPID, uint16_t, vpid_hash, vpid_equal>;
  };

  class vpid_freq_ultra
  {
    public:
      static constexpr std::size_t WIDTH = 1u << 16;
      static constexpr std::size_t MASK  = WIDTH - 1;

      // 1024는 너무 느릴 수 있음. 128~256 추천.
      static constexpr uint32_t FLUSH_PERIOD = 1u << 8; // 256

      inline void increment (const VPID &vpid) noexcept
      {
	auto &tls = tls_state_();
	const std::size_t i = idx_ (vpid);

	// 처음 만난 bucket이면 dirty로 기록
	if (tls.local[i] == 0)
	  {
	    tls.dirty.push_back (static_cast<uint32_t> (i));
	  }

	if (tls.local[i] != UINT8_MAX)
	  {
	    tls.local[i]++;
	  }

	if ((++tls.ticks & (FLUSH_PERIOD - 1)) == 0)
	  {
	    flush_tls_();
	  }
      }

      inline uint8_t estimate_fast (const VPID &vpid) const noexcept
      {
	const std::size_t i = idx_ (vpid);
	uint8_t g = m_global[i].load (std::memory_order_relaxed);
	uint8_t l = tls_state_().local[i];
	return (g > l) ? g : l;
      }

      void decay_half_global() noexcept
      {
	for (auto &v : m_global)
	  {
	    uint8_t x = v.load (std::memory_order_relaxed);
	    v.store (static_cast<uint8_t> (x >> 1), std::memory_order_relaxed);
	  }
      }

    private:
      struct tls_state
      {
	std::array<uint8_t, WIDTH> local{};
	std::vector<uint32_t> dirty{};
	uint32_t ticks{0};

	tls_state()
	{
	  local.fill (0);
	  dirty.reserve (1024); // 대충
	}
      };

      static inline uint32_t mix32_ (uint32_t x) noexcept
      {
	x ^= x >> 16;
	x *= 0x7feb352d;
	x ^= x >> 15;
	return x;
      }

      static inline uint32_t vpid_to_u32_ (const VPID &vpid) noexcept
      {
	uint32_t a = static_cast<uint32_t> (vpid.pageid);
	uint32_t b = static_cast<uint32_t> (vpid.volid);
	return a * 0x9e3779b1u ^ (b + 0x7f4a7c15u);
      }

      static inline std::size_t idx_ (const VPID &vpid) noexcept
      {
	return static_cast<std::size_t> (mix32_ (vpid_to_u32_ (vpid))) & MASK;
      }

      static inline tls_state &tls_state_() noexcept
      {
	thread_local tls_state s;
	return s;
      }

      inline void flush_tls_() noexcept
      {
	auto &tls = tls_state_();

	for (uint32_t idx : tls.dirty)
	  {
	    uint8_t v = tls.local[idx];
	    if (v == 0)
	      {
		continue;
	      }

	    uint8_t cur = m_global[idx].load (std::memory_order_relaxed);
	    uint8_t next = (cur > UINT8_MAX - v) ? UINT8_MAX : static_cast<uint8_t> (cur + v);
	    m_global[idx].store (next, std::memory_order_relaxed);

	    tls.local[idx] = 0;
	  }

	tls.dirty.clear();
      }

    private:
      std::array<std::atomic<uint8_t>, WIDTH> m_global{};
  };

  class vpid_freq_sketch
  {
    public:

      // 256K buckets -> 약 512KB
      static constexpr std::size_t WIDTH = 1u << 18;
      static constexpr std::size_t MASK  = WIDTH - 1;

      inline void increment (const VPID &vpid) noexcept
      {
	inc_ (vpid, 0x9e3779b9u);
	inc_ (vpid, 0x85ebca6bu);
	inc_ (vpid, 0xc2b2ae35u);
	inc_ (vpid, 0x27d4eb2fu);
      }

      inline uint16_t estimate (const VPID &vpid) const noexcept
      {
	uint16_t a = get_ (vpid, 0x9e3779b9u);
	uint16_t b = get_ (vpid, 0x85ebca6bu);
	uint16_t c = get_ (vpid, 0xc2b2ae35u);
	uint16_t d = get_ (vpid, 0x27d4eb2fu);

	uint16_t m = a < b ? a : b;
	m = m < c ? m : c;
	m = m < d ? m : d;

	return m;
      }

      void decay_half() noexcept
      {
	for (auto &v : m_table)
	  {
	    uint16_t x = v.load (std::memory_order_relaxed);
	    v.store (x >> 1, std::memory_order_relaxed);
	  }
      }

    private:

      std::array<std::atomic<uint16_t>, WIDTH> m_table {};

      static inline uint32_t mix32_ (uint32_t x) noexcept
      {
	x ^= x >> 16;
	x *= 0x7feb352d;
	x ^= x >> 15;
	x *= 0x846ca68b;
	x ^= x >> 16;
	return x;
      }

      static inline uint32_t vpid_to_u32_ (const VPID &vpid) noexcept
      {
	uint32_t a = (uint32_t)vpid.pageid;
	uint32_t b = (uint32_t)vpid.volid;

	return a * 0x9e3779b1u ^ (b + 0x7f4a7c15u);
      }

      inline std::size_t idx_ (const VPID &vpid, uint32_t salt) const noexcept
      {
	uint32_t x = vpid_to_u32_ (vpid) ^ salt;
	return mix32_ (x) & MASK;
      }

      inline void inc_ (const VPID &vpid, uint32_t salt) noexcept
      {
	auto i = idx_ (vpid, salt);

	uint16_t cur = m_table[i].load (std::memory_order_relaxed);

	if (cur != UINT16_MAX)
	  {
	    m_table[i].store (cur + 1, std::memory_order_relaxed);
	  }
      }

      inline uint16_t get_ (const VPID &vpid, uint32_t salt) const noexcept
      {
	return m_table[idx_ (vpid, salt)].load (std::memory_order_relaxed);
      }
  };

  template <typename Traits>
  struct algo_context_t
  {
    top_candidates_t<Traits> m_top_candidates;
    top_candidates_t<Traits> m_top_for_refine;
    next_candidates_t<Traits> m_next_candidates;
    visited_set_t<Traits> m_visits;
    cubthread::entry *m_thread_p {nullptr};

    // page cache
    page_cache_helper::cache_type m_page_cache;
    page_cache_helper::access_count_type m_page_access_count;
    static constexpr uint16_t CACHE_THRESHOLD = 3;
    static constexpr std::size_t MAX_CACHE_SIZE = 128;
    std::size_t m_page_visits {0};
    std::size_t m_page_cache_hits {0};
    std::size_t m_page_cache_evictions {0};

    // stats
    bool m_is_perf_tracking {false};
    std::size_t m_visited_nodes{};
    std::size_t m_computed_distances{};
    std::size_t m_computed_distances_in_refines{};
    std::size_t m_computed_distances_in_reverse_refines{};

    void clear_candidates ()
    {
      m_top_candidates.clear ();
      m_next_candidates.clear();
      m_visits.clear();
    }

    ~algo_context_t()
    {
      // fprintf (stdout, "cache utilization: %zu / %zu\n", m_page_cache.size(), m_page_visits);
      // fprintf (stdout, "page cache hit ratio: %zu / %zu, evictions: %zu, cache size: %zu\n", m_page_cache_hits, m_page_visits,
      // 	       m_page_cache_evictions, m_page_cache.size());
      for (auto &[vpid, cp] : m_page_cache)
	{
	  pgbuf_unfix (m_thread_p, cp.page_ptr);
	}
    }
  };
}

#endif // _HNSW_ALGO_COMMON_HPP_