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

#include "hnsw_storage_disk.hpp"

#include "file_manager.h" // FILE_DESCRIPTORS
#include "slotted_page.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace cubhnsw
{
  disk_storage::disk_storage (
	  const BTID &giid,
	  const hnsw_build_params &build_params)
    : base (giid, build_params)
  {
    m_vfid = giid.vfid;
    m_root_vpid = VPID { giid.root_pageid, giid.vfid.volid };
    m_last_node_vpid = m_root_vpid;
  }

  disk_storage::~disk_storage ()
  {

  }

  // The root is not initialized yet
  bool
  disk_storage::is_empty ()
  {
    return m_is_empty;
  }

  // not yet
  void
  disk_storage::init_root (std::byte *root_block, std::size_t &root_size)
  {
    root_disk_t<disk_traits_t> root { reinterpret_cast<byte_t *> (root_block) };

    root_size = root.get_size();
  }

  disk_storage::slot_id_t
  disk_storage::add_vector (algo_context_t<traits> &context, const OID &key, const float *vector)
  {
    std::size_t bytes = this->get_dimension () * sizeof (float);

    if (m_vec_pool_vfid.fileid == 0)
      {
	assert (false);
	return slot_id_t { -1, -1, -1 };
      }

    page_handle page_ptr = get_page_to_insert (context, m_vec_pool_vfid, m_last_vec_vpid, bytes);
    assert (page_ptr.get() != nullptr);

    RECDES recdes = { IO_MAX_PAGE_SIZE, (int) bytes, REC_HOME, (char *) vector };
    PGSLOTID slot_id;

    int error_code = spage_insert (context.m_thread_p, page_ptr.get(), &recdes, &slot_id);
    if (error_code != SP_SUCCESS)
      {
	assert (false);
	return slot_id_t { -1, -1, -1 };
      }

    return slot_id_t { m_last_vec_vpid.pageid, slot_id, m_last_vec_vpid.volid };
  }

  disk_storage::page_handle
  disk_storage::get_page_to_insert (algo_context_t<traits> &context, VFID &vfid, VPID &last_vpid, std::size_t bytes)
  {
    PAGE_PTR page_ptr = nullptr;

    cubthread::entry *thread_p = context.m_thread_p;
    if (VPID_ISNULL (&last_vpid))
      {
	// alloc a new page in case of root page
	page_ptr = alloc_new_page (thread_p, vfid, last_vpid);
      }
    else
      {
	page_ptr = pgbuf_fix (thread_p, &last_vpid, OLD_PAGE, PGBUF_LATCH_WRITE, PGBUF_UNCONDITIONAL_LATCH);
	if (spage_get_free_space (thread_p, page_ptr) < static_cast<int> (bytes))
	  {
	    // not enough
	    pgbuf_unfix (thread_p, page_ptr);
	    page_ptr = alloc_new_page (thread_p, vfid, last_vpid);
	  }
      }

    return page_handle (page_ptr, [this, thread_p] (PAGE_PTR page_ptr) noexcept
    {
      pgbuf_set_dirty (thread_p, page_ptr, FREE);
    });
  }

  PAGE_PTR
  disk_storage::alloc_new_page (cubthread::entry *thread_p, VFID &vfid, VPID &vpid)
  {
    PAGE_PTR page_ptr = NULL;

    (void) file_alloc (thread_p, &vfid, initialize_new_page, NULL, &vpid, &page_ptr);
    assert (page_ptr != NULL);

    if (page_ptr == NULL)
      {
	assert (false);
	return page_ptr;
      }

#if !defined (NDEBUG)
    pgbuf_check_page_ptype (thread_p, page_ptr, PAGE_HNSW);
#endif /* !NDEBUG */

    return page_ptr;
  }

  disk_storage::slot_id_t
  disk_storage::add_node (algo_context_t<traits> &context, const OID &key, const float *vector, const level_t &level)
  {
    // insert node
    std::size_t bytes = this->node_bytes_ (level, get_dimension(), get_connectivity());
    page_handle page_ptr = get_page_to_insert (context, m_vfid, m_last_node_vpid, bytes);

    RECDES recdes;
    char rec_buf[IO_MAX_PAGE_SIZE];
    memset (rec_buf, 0, bytes);

    /* create header record */
    recdes.area_size = DB_PAGESIZE;
    recdes.data = rec_buf;
    recdes.type = REC_HOME;
    recdes.length = bytes;

    node_t<disk_traits_t> node { reinterpret_cast<byte_t *> (rec_buf) };
    node.set_key (key);
    node.set_level (level);
    node.set_vector (vector, get_dimension());

    PGSLOTID slot_id;

    int error_code = spage_insert (context.m_thread_p, page_ptr.get(), &recdes, &slot_id);
    if (error_code != SP_SUCCESS)
      {
	ASSERT_ERROR ();
	return slot_id_t { -1, -1, -1 };
      }

    return { m_last_node_vpid.pageid, slot_id, m_last_node_vpid.volid };
  }

  disk_storage::pinned_t
  disk_storage::get_root (algo_context_t<traits> &context, lock_mode mode)
  {
    VPID root_vpid = m_root_vpid;

    PGBUF_LATCH_MODE pgbuf_mode = PGBUF_LATCH_READ;
    if (mode == lock_mode::exclusive)
      {
	pgbuf_mode = PGBUF_LATCH_WRITE;
      }

    PAGE_PTR root_page_ptr = pgbuf_fix (context.m_thread_p, &root_vpid, OLD_PAGE, pgbuf_mode, PGBUF_UNCONDITIONAL_LATCH);
    assert (root_page_ptr != nullptr);

    // TODO: hardcoded slot id 1
    SPAGE_SLOT *slotp = spage_get_slot (root_page_ptr, 1);
    assert (slotp != nullptr);

    OID oid = { root_vpid.pageid, 1, root_vpid.volid };

    cubthread::entry *thread_p = context.m_thread_p;
    return make_pinned_block<disk_traits_t> (oid, (std::byte *) root_page_ptr + slotp->offset_to_record,
	   slotp->record_length, mode,
	   [this, root_page_ptr, thread_p] (auto& blk) noexcept
    {
      if (blk.mode == lock_mode::exclusive)
	{
	  pgbuf_set_dirty (thread_p, reinterpret_cast<PAGE_PTR> (root_page_ptr), FREE);
	}
      else
	{
	  pgbuf_unfix (thread_p, reinterpret_cast<PAGE_PTR> (root_page_ptr));
	}
    }

					    );
  }

  disk_storage::pinned_t
  disk_storage::get_node_by_slot_id (algo_context_t<traits> &context, const slot_id_t &id, const lock_mode &mode,
				     const level_t &level)
  {
    VPID vpid = { id.pageid, id.volid };

    PAGE_PTR node_page_ptr = nullptr;
    bool is_cached_page = false;

    context.m_page_visits++;
    /*
    * Eviction helper:
    * - scan_n = cache_size/4
    * - remove_target = scan_n/2
    * - within scan set: evict level 0 -> 1 -> 2 ...
    * - within same level: evict lower fix_count first
    * - erase is VPID -> find -> erase
    */
    auto evict_if_needed = [&] ()
    {
      const std::size_t cache_size =
	      context.m_page_cache.size();

      if (cache_size <
	  algo_context_t<traits>::MAX_CACHE_SIZE)
	{
	  return;
	}

      const std::size_t scan_n =
	      std::max<std::size_t> (1,
				     cache_size / 4);

      const std::size_t remove_target =
	      std::min<std::size_t> (4,
				     scan_n / 2);

      struct cand
      {
	VPID vpid;
	level_t level;
	uint16_t fix_count;
	uint32_t last_touch;
	uint32_t score;
      };

      std::vector<cand> scan_list;
      scan_list.reserve (scan_n);

      /*
       * 최대 last_touch 찾기
       * (= 가장 최근 접근)
       */

      uint32_t max_touch = 0;

      std::size_t scanned = 0;

      for (auto it = context.m_page_cache.begin();
	   it != context.m_page_cache.end()
	   && scanned < scan_n;
	   ++it, ++scanned)
	{
	  const auto &e = it->second;

	  if (e.in_use != 0)
	    {
	      continue;
	    }

	  if (e.last_touch > max_touch)
	    {
	      max_touch = e.last_touch;
	    }
	}

      if (max_touch == 0)
	{
	  return;
	}

      /*
       * candidate 생성
       */

      scanned = 0;

      for (auto it = context.m_page_cache.begin();
	   it != context.m_page_cache.end()
	   && scanned < scan_n;
	   ++it, ++scanned)
	{
	  const auto &e = it->second;

	  if (e.in_use != 0)
	    {
	      continue;
	    }

	  uint32_t age =
		  max_touch - e.last_touch;

	  /*
	   * eviction score
	   */

	  uint32_t score =
		  (uint32_t)e.level * 1000000u
		  + age
		  - ((uint32_t)e.fix_count << 3);

	  scan_list.push_back (
		  cand
	  {
	    it->first,
	    e.level,
	    e.fix_count,
	    e.last_touch,
	    score});
	}

      if (scan_list.empty())
	{
	  return;
	}

      /*
       * score 높은 순 정렬
       */

      std::sort (
	      scan_list.begin(),
	      scan_list.end(),
	      [] (const cand &a,
		  const cand &b)
      {
	return a.score > b.score;
      });

      /*
       * eviction 수행
       */

      std::size_t removed = 0;

      for (const auto &c : scan_list)
	{
	  if (removed >= remove_target)
	    {
	      break;
	    }

	  auto it2 =
		  context.m_page_cache.find (c.vpid);

	  if (it2 ==
	      context.m_page_cache.end())
	    {
	      continue;
	    }

	  auto &e = it2->second;

	  if (e.in_use != 0)
	    {
	      continue;
	    }

	  pgbuf_unfix (
		  context.m_thread_p,
		  e.page_ptr);

	  context.m_page_cache.erase (it2);

	  context.m_page_cache_evictions++;

	  removed++;
	}
    };
    auto it = context.m_page_cache.find (vpid);

    /*
     * ============================
     * Cache Hit
     * ============================
     */
    if (it != context.m_page_cache.end ())
      {
	cached_page_entry &e = it->second;

	context.m_page_cache_hits++;
	e.last_touch = context.m_page_visits;

	// 이 페이지가 관측된 최소 level 갱신 (0이 우선)
	if (level < e.level)
	  {
	    e.level = level;
	  }

	// local reuse counter (saturating)
	if (e.fix_count != UINT16_MAX)
	  {
	    e.fix_count++;
	  }

	// in_use는 pinned_t lifetime 동안 보호
	if (e.in_use != UINT16_MAX)
	  {
	    e.in_use++;
	  }

	/*
	 * Shared request:
	 *  - shared cache -> use
	 *  - exclusive cache -> use
	 */
	if (mode == lock_mode::shared)
	  {
	    node_page_ptr = e.page_ptr;
	    is_cached_page = true;
	  }
	else
	  {
	    /*
	     * Exclusive request
	     */
	    if (e.is_exclusive)
	      {
		node_page_ptr = e.page_ptr;
		is_cached_page = true;
	      }
	    else
	      {
		/*
		 * Shared -> Exclusive promote
		 */
		context.m_page_cache_promotes++;

		PAGE_PTR pg = e.page_ptr;

		int rc = pgbuf_promote_read_latch (
				 context.m_thread_p,
				 &pg,
				 PGBUF_PROMOTE_ONLY_READER);

		if (rc == NO_ERROR)
		  {
		    e.page_ptr = pg;     // promote가 pg를 바꿀 수 있으니 갱신
		    e.is_exclusive = true;

		    node_page_ptr = e.page_ptr;
		    is_cached_page = true;
		  }
		else
		  {
		    /*
		     * promote failed
		     * fallback: unfix and re-fix exclusive
		     *
		     * 주의: 기존 hit에서 in_use++ 해둔 상태이므로,
		     * 여기서 entry 자체를 지우기 전에 in_use를 되돌릴 필요가 있다.
		     * (엔트리를 지우므로 별도 decrement는 의미 없음)
		     */
		    context.m_page_cache_promote_fallbacks++;

		    // 기존 shared fix 해제
		    pgbuf_unfix (context.m_thread_p, e.page_ptr);

		    // 캐시 엔트리 삭제
		    context.m_page_cache.erase (it);

		    // 캐시가 가득 찼으면 먼저 eviction
		    evict_if_needed ();

		    node_page_ptr =
			    pgbuf_fix (context.m_thread_p,
				       &vpid,
				       OLD_PAGE,
				       PGBUF_LATCH_WRITE,
				       PGBUF_UNCONDITIONAL_LATCH);

		    cached_page_entry new_e;
		    new_e.page_ptr = node_page_ptr;
		    new_e.is_exclusive = true;
		    new_e.level = level;
		    new_e.fix_count = 1;
		    new_e.in_use = 1;
		    new_e.last_touch = context.m_page_visits;

		    context.m_page_cache.emplace (vpid, new_e);

		    is_cached_page = true;
		  }
	      }
	  }
      }
    else
      {
	/*
	 * ============================
	 * Cache Miss
	 * ============================
	 */
	context.m_page_cache_misses++;

	// 캐시가 가득 찼으면 먼저 eviction
	evict_if_needed ();

	PGBUF_LATCH_MODE pgbuf_mode =
		(mode == lock_mode::exclusive)
		? PGBUF_LATCH_WRITE
		: PGBUF_LATCH_READ;

	node_page_ptr =
		pgbuf_fix (context.m_thread_p,
			   &vpid,
			   OLD_PAGE,
			   pgbuf_mode,
			   PGBUF_UNCONDITIONAL_LATCH);

	cached_page_entry new_e;
	new_e.page_ptr = node_page_ptr;
	new_e.is_exclusive = (mode == lock_mode::exclusive);
	new_e.level = level;
	new_e.fix_count = 1;
	new_e.in_use = 1;

	context.m_page_cache.emplace (vpid, new_e);

	is_cached_page = true;
      }

    assert (node_page_ptr != nullptr);

    SPAGE_SLOT *slotp =
	    spage_get_slot (node_page_ptr, id.slotid);
    assert (slotp != nullptr);

    if (context.m_is_perf_tracking)
      {
	context.m_visited_nodes++;
      }

    cubthread::entry *thread_p = context.m_thread_p;

    return make_pinned_block<disk_traits_t> (
		   id,
		   (std::byte *) node_page_ptr + slotp->offset_to_record,
		   slotp->record_length,
		   mode,
		   [this,
		    &context,
		    vpid,
		    node_page_ptr,
		    thread_p,
		    is_cached_page] (auto &blk) noexcept
    {
      PAGE_PTR page = reinterpret_cast<PAGE_PTR> (node_page_ptr);

      if (blk.mode == lock_mode::exclusive)
	{
	  // cached page는 FREE 금지 (dirty만)
	  pgbuf_set_dirty (thread_p,
			   page,
			   is_cached_page ? DONT_FREE : FREE);
	}
      else
	{
	  // non-cached shared는 unfix
	  if (!is_cached_page)
	    {
	      pgbuf_unfix (thread_p, page);
	    }
	}

      // cached entry 보호 해제
      if (is_cached_page)
	{
	  auto it3 = context.m_page_cache.find (vpid);
	  if (it3 != context.m_page_cache.end ())
	    {
	      auto &e = it3->second;
	      if (e.in_use > 0)
		{
		  e.in_use--;
		}
	    }
	}
    }
	   );
  }

  const float *
  disk_storage::get_vector_by_slot_id (algo_context_t<traits> &context, const slot_id_t &slot, const lock_mode &mode,
				       const level_t &level)
  {
    auto it = m_vector_cache.find (slot);
    if (it != m_vector_cache.end ())
      {
	return it->second.data ();
      }

    pinned_t node_blk = get_node_by_slot_id (context, slot, mode, level);
    node_t<disk_traits_t> node { reinterpret_cast<byte_t *> (node_blk->data) };
    const float *vec = node.get_vector ();

    std::vector<float> &cached = m_vector_cache[slot];
    cached.assign (vec, vec + get_dimension ());

    return cached.data ();
  }

  // promote lockmode from shared to exclusive
  void
  disk_storage::promote_root (pinned_t &old)
  {
    // not implemented yet
    // int error_code = pgbuf_promote_read_latch (m_thread_p, reinterpret_cast<PAGE_PTR*>(old.data()), PGBUF_PROMOTE_SHARED_READER);
  }

  void
  disk_storage::set_empty (bool is_empty) noexcept
  {
    m_is_empty = is_empty;
  }

  int
  disk_storage::initialize_new_page (THREAD_ENTRY *thread_p, PAGE_PTR page, void *args)
  {
    pgbuf_set_page_ptype (thread_p, page, PAGE_HNSW);
    spage_initialize (thread_p, page, UNANCHORED_KEEP_SEQUENCE, HNSW_MAX_ALIGN, DONT_SAFEGUARD_RVSPACE);
    pgbuf_set_dirty (thread_p, page, DONT_FREE);

    return NO_ERROR;
  }

  int
  disk_storage::create_continous_file (THREAD_ENTRY *thread_p, VFID &vfid, VPID &vpid)
  {
    int error_code = NO_ERROR;
    FILE_DESCRIPTORS des;

    memset (&des, 0, sizeof (des));

    error_code = file_create_with_npages (thread_p, FILE_BTREE, 1, &des, (VFID *) &vfid);
    if (error_code != NO_ERROR)
      {
	return error_code;
      }

    log_sysop_start (thread_p);
    error_code = file_alloc_sticky_first_page (thread_p, &vfid, initialize_new_page, NULL, &vpid, NULL);
    if (error_code != NO_ERROR)
      {
	ASSERT_ERROR ();
	log_sysop_abort (thread_p);
	return error_code;
      }
    log_sysop_commit (thread_p);

#if 0  // TODO: I think we don't need TDE for vector index files
    error_code = heap_get_class_tde_algorithm (thread_p, &btid->topclass_oid, &tde_algo);
    if (error_code != NO_ERROR)
      {
	VFID_SET_NULL (&btid->ovfid);
	return error_code;
      }
    error_code = file_apply_tde_algorithm (thread_p, &btid->ovfid, tde_algo);
    if (error_code != NO_ERROR)
      {
	VFID_SET_NULL (&btid->ovfid);
	return error_code;
      }
#endif

    return error_code;
  }
}
