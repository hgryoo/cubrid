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

  uint16_t
  disk_storage::compute_cache_threshold (const algo_context_t<traits> &context,
					 const VPID &vpid, uint8_t local_count) const noexcept
  {
    uint16_t threshold = algo_context_t<traits>::CACHE_THRESHOLD;

    const uint8_t freq = m_page_freq.estimate_fast (vpid);

    // very hot
    if (freq >= 32)
      {
	return 1;
      }

    // hot
    if (freq >= 12)
      {
	return 2;
      }

    // warm
    if (freq >= 6)
      {
	return 3;
      }

    return threshold;
  }

  disk_storage::pinned_t
  disk_storage::get_node_by_slot_id (algo_context_t<traits> &context, const slot_id_t &id, const lock_mode &mode)
  {
    VPID vpid = { id.pageid, id.volid };

    PGBUF_LATCH_MODE pgbuf_mode = PGBUF_LATCH_READ;
    if (mode == lock_mode::exclusive)
      {
	pgbuf_mode = PGBUF_LATCH_WRITE;
      }

    PAGE_PTR node_page_ptr = nullptr;

    // check cache first
    bool is_cached_page = false;
    if (mode == lock_mode::shared)
      {
	context.m_page_visits++;

	// sampling
	m_page_freq.increment (vpid);

	auto tick = m_freq_ticks.fetch_add (1, std::memory_order_relaxed);

	if ((tick & ((1u<<18) - 1)) == 0)
	  {
	    m_page_freq.decay_half_global();
	  }

	uint32_t now_tick =
		m_cache_tick.fetch_add (
			1,
			std::memory_order_relaxed);

	auto it = context.m_page_cache.find (vpid);
	if (it != context.m_page_cache.end())
	  {
	    node_page_ptr = it->second.page_ptr;

	    context.m_page_cache_hits++;

	    is_cached_page = true;

	    it->second.last_touch = now_tick;

	    if (it->second.local_score < 255)
	      {
		it->second.local_score++;
	      }
	  }
	else
	  {
	    node_page_ptr = pgbuf_fix (context.m_thread_p, &vpid, OLD_PAGE, pgbuf_mode, PGBUF_UNCONDITIONAL_LATCH);

	    // is it right?
	    auto &count = context.m_page_access_count[vpid];
	    if (count < 255)
	      {
		count++;
	      }

	    uint16_t threshold = compute_cache_threshold (context, vpid, (uint8_t) count);

	    uint8_t freq =
		    m_page_freq.estimate_fast (vpid);

	    bool admit =
		    (count >= threshold) &&
		    (freq >= 4);

	    cached_page *victim_entry = nullptr;
	    VPID victim_vpid;
	    uint32_t victim_score = 0;

	    if (admit &&
		context.m_page_cache.size()
		>= algo_context_t<traits>::MAX_CACHE_SIZE)
	      {
		auto victim_it = context.m_page_cache.end();

		uint32_t worst_score = 0;

		for (auto it = context.m_page_cache.begin();
		     it != context.m_page_cache.end();
		     ++it)
		  {
		    const auto &e = it->second;

		    uint32_t age =
			    now_tick - e.last_touch;

		    uint8_t victim_freq =
			    m_page_freq.estimate_fast (it->first);

		    uint32_t score =
			    age
			    - (victim_freq << 3)
			    - (e.local_score << 2);

		    if (victim_it
			== context.m_page_cache.end()
			|| score > worst_score)
		      {
			worst_score = score;
			victim_it = it;
		      }
		  }

		if (victim_it != context.m_page_cache.end())
		  {
		    victim_entry = &victim_it->second;
		    victim_vpid  = victim_it->first;
		    victim_score = worst_score;

		    /*
		     * Admission score (same metric)
		     */

		    uint32_t new_score =
			    0
			    - (freq << 3)
			    - (count << 2);

		    /*
		     * Reject if worse than victim
		     */

		    if (new_score >= victim_score)
		      {
			admit = false;
		      }
		  }
	      }

	    if (admit)
	      {
		if (victim_entry != nullptr)
		  {
		    auto it =
			    context.m_page_cache.find (
				    victim_vpid);

		    pgbuf_unfix (context.m_thread_p,
				 it->second.page_ptr);

		    context.m_page_cache.erase (it);

		    context.m_page_cache_evictions++;
		  }

		cached_page e;

		e.page_ptr   = node_page_ptr;
		e.local_score = count;
		e.last_touch = now_tick;

		context.m_page_cache.emplace (
			vpid,
			e);

		is_cached_page = true;
	      }
	  }
      }
    else
      {
	node_page_ptr = pgbuf_fix (context.m_thread_p, &vpid, OLD_PAGE, pgbuf_mode, PGBUF_UNCONDITIONAL_LATCH);
      }

    assert (node_page_ptr != nullptr);

    SPAGE_SLOT *slotp = spage_get_slot (node_page_ptr, id.slotid);
    assert (slotp != nullptr);

    if (context.m_is_perf_tracking)
      {
	context.m_visited_nodes++;
      }

    cubthread::entry *thread_p = context.m_thread_p;
    return make_pinned_block<disk_traits_t> (id, (std::byte *) node_page_ptr + slotp->offset_to_record,
	   slotp->record_length, mode,
	   [this, node_page_ptr, thread_p, is_cached_page] (auto& blk) noexcept
    {
      if (blk.mode == lock_mode::exclusive)
	{
	  pgbuf_set_dirty (thread_p, reinterpret_cast<PAGE_PTR> (node_page_ptr), FREE);
	}
      else
	{
	  if (!is_cached_page)
	    {
	      pgbuf_unfix (thread_p, reinterpret_cast<PAGE_PTR> (node_page_ptr));
	    }
	}
    }

					    );

  }

  disk_storage::pinned_t
  disk_storage::get_vector_by_slot_id (algo_context_t<traits> &context, const slot_id_t &slot, const lock_mode &mode)
  {
    // get node by slot id
    return get_node_by_slot_id (context, slot, lock_mode::shared);
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
