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
    VPID_SET_NULL (&m_last_node_vpid);
  }

  disk_storage::~disk_storage ()
  {

  }

  // The root is not initialized yet
  bool
  disk_storage::is_empty ()
  {
    return m_is_empty.load();
  }

  // not yet
  void
  disk_storage::init_root (std::byte *root_block, std::size_t &root_size)
  {
    root_disk_t<disk_traits_t> root { reinterpret_cast<byte_t *> (root_block) };

    root_size = root.get_size();
  }

  disk_storage::insert_page_t
  disk_storage::get_page_to_insert (cubthread::entry *thread_p, VFID &vfid, VPID &last_vpid, std::size_t bytes)
  {
    PAGE_PTR page_ptr = nullptr;

    PAGE_PTR raw = nullptr;
    VPID used_vpid;

    VPID vpid_snapshot;
    {
      vpid_snapshot = last_vpid;
    }

    if (VPID_ISNULL (&vpid_snapshot))
      {
	// alloc_new_page should update last_vpid under the same mutex (either here or inside)
	raw = alloc_new_page (thread_p, vfid, last_vpid);
	// page_ptr is already fixed/latched as alloc_new_page defines
	used_vpid = last_vpid;
      }
    else
      {
	raw = pgbuf_fix (thread_p, &vpid_snapshot,
			 OLD_PAGE,
			 PGBUF_LATCH_READ,
			 PGBUF_UNCONDITIONAL_LATCH);
	if (spage_get_free_space (thread_p, raw) < (int)bytes)
	  {
	    pgbuf_unfix (thread_p, raw);
	    raw = alloc_new_page (thread_p, vfid, last_vpid);
	    used_vpid = last_vpid;
	  }
	else
	  {
	    pgbuf_unfix (thread_p, raw);
	    raw = pgbuf_fix (thread_p, &vpid_snapshot,
			     OLD_PAGE,
			     PGBUF_LATCH_WRITE,
			     PGBUF_UNCONDITIONAL_LATCH);
	    used_vpid = vpid_snapshot;
	  }
      }

    return insert_page_t
    {
      used_vpid,
      PAGE_PTR_WITH_DELETER (
	      reinterpret_cast<PAGE_PTR> (raw),
      page_deleter{ thread_p })
    };
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
  disk_storage::add_node (cubthread::entry *thread_p, const OID &key, const float *vector, const level_t &level)
  {
    // insert node
    std::size_t bytes = this->node_bytes_ (level, get_dimension(), get_connectivity());

    bool is_new_allocated = false;
    std::unique_lock<std::mutex> lk (m_insert_mutex);
    insert_page_t insert_page = get_page_to_insert (thread_p, m_vfid, m_last_node_vpid, bytes);

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

    int error_code = spage_insert (thread_p, insert_page.page.get(), &recdes, &slot_id);
    if (error_code != SP_SUCCESS)
      {
	ASSERT_ERROR ();
	return slot_id_t { -1, -1, -1 };
      }

    return { insert_page.vpid.pageid, slot_id, insert_page.vpid.volid };
  }

  disk_storage::pinned_t
  disk_storage::get_root (cubthread::entry *thread_p, lock_mode mode)
  {
    VPID root_vpid = m_root_vpid;

    PGBUF_LATCH_MODE pgbuf_mode = PGBUF_LATCH_READ;
    if (mode == lock_mode::exclusive)
      {
	pgbuf_mode = PGBUF_LATCH_WRITE;
      }

    PAGE_PTR root_page_ptr = pgbuf_fix (thread_p, &root_vpid, OLD_PAGE, pgbuf_mode, PGBUF_UNCONDITIONAL_LATCH);
    assert (root_page_ptr != nullptr);

    // TODO: hardcoded slot id 1
    SPAGE_SLOT *slotp = spage_get_slot (root_page_ptr, 1);
    assert (slotp != nullptr);

    OID oid = { root_vpid.pageid, 1, root_vpid.volid };

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
  disk_storage::get_node_by_slot_id (cubthread::entry *thread_p, const slot_id_t &id, const lock_mode &mode)
  {
    VPID vpid = { id.pageid, id.volid };

    PGBUF_LATCH_MODE pgbuf_mode = PGBUF_LATCH_READ;
    if (mode == lock_mode::exclusive)
      {
	pgbuf_mode = PGBUF_LATCH_WRITE;
      }

    PAGE_PTR node_page_ptr = pgbuf_fix (thread_p, &vpid, OLD_PAGE, pgbuf_mode, PGBUF_UNCONDITIONAL_LATCH);
    assert (node_page_ptr != nullptr);

    SPAGE_SLOT *slotp = spage_get_slot (node_page_ptr, id.slotid);
    assert (slotp != nullptr);

    return make_pinned_block<disk_traits_t> (id, (std::byte *) node_page_ptr + slotp->offset_to_record,
	   slotp->record_length, mode,
	   [this, node_page_ptr, thread_p] (auto& blk) noexcept
    {
      if (blk.mode == lock_mode::exclusive)
	{
	  pgbuf_set_dirty (thread_p, reinterpret_cast<PAGE_PTR> (node_page_ptr), FREE);
	}
      else
	{
	  pgbuf_unfix (thread_p, reinterpret_cast<PAGE_PTR> (node_page_ptr));
	}
    }

					    );

  }

  disk_storage::pinned_t
  disk_storage::get_vector_by_slot_id (cubthread::entry *thread_p, const slot_id_t &slot, const lock_mode &mode)
  {
    // get node by slot id
    return get_node_by_slot_id (thread_p, slot, lock_mode::shared);
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
    m_is_empty.store (is_empty);
  }

  int
  disk_storage::initialize_new_page (THREAD_ENTRY *thread_p, PAGE_PTR page, void *args)
  {
    pgbuf_set_page_ptype (thread_p, page, PAGE_HNSW);
    spage_initialize (thread_p, page, UNANCHORED_KEEP_SEQUENCE, HNSW_MAX_ALIGN, DONT_SAFEGUARD_RVSPACE);
    pgbuf_set_dirty (thread_p, page, DONT_FREE);

    return NO_ERROR;
  }
}
