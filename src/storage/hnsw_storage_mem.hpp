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

#pragma once

#include <cstddef>
#include <vector>
#include <unordered_map>
#include <cstring>

#include "slotted_page.h"

#include "hnsw_storage.hpp"          // storage<memory_id_traits>

namespace cubhnsw
{
  struct block_entry_t
  {
    int num_elements {0};
    int remaining_size {IO_MAX_PAGE_SIZE};
    std::byte block[IO_MAX_PAGE_SIZE];
  };

  struct block_slot_t
  {
    uint64_t id {0};
    block_entry_t *entry {nullptr};
    std::size_t offset {0};
    std::size_t size {0};
  };

  template <>
  struct storage_traits<storage_kind::memory>
  {
    static constexpr storage_kind kind = storage_kind::memory;
    using block_group_id_t = std::forward_list<block_entry_t *>;
    using block_id_t = block_entry_t;
    using slot_id_t = uint64_t; // memory offset of the block
  };

  using memory_traits_t = storage_traits<storage_kind::memory>;

  class memory_storage : public storage<memory_traits_t>
  {
    public:
      using base        = storage<memory_traits_t>;
      using block_group_id_t = memory_traits_t::block_group_id_t;
      using block_id_t = memory_traits_t::block_id_t;
      using slot_id_t = memory_traits_t::slot_id_t;

    public:
      memory_storage (const BTID &giid, const hnsw_build_params &params)
	: base (giid, params)
      {
	m_root_vpid = VPID { giid.root_pageid, giid.vfid.volid };
      }
      virtual ~memory_storage()
      {
	for (auto &[slot_id, slot] : m_block_table)
	  {
	    delete slot;
	  }
	m_block_table.clear();
	m_last_node_entry = nullptr;
	m_last_vec_entry = nullptr;
      }

      // The root is not initialized yet
      virtual bool is_empty () override
      {
	return m_is_empty;
      }
      virtual void set_empty (bool is_empty) noexcept override
      {
	m_is_empty = is_empty;
      }

      virtual void init_root (std::byte *root_block, std::size_t &root_size) override
      {
	root_t<memory_traits_t> root { reinterpret_cast<byte_t *> (root_block) };
	root_size = root.get_size();
      }

      virtual slot_id_t add_node (const OID &key, const float *vector, const level_t &level) override
      {
	// insert vector first
	slot_id_t vec_slot = add_vector (key, vector);

	std::size_t bytes = this->node_bytes_ (level);
	block_slot_t *node_slot = get_block_ptr_to_insert (m_last_node_entry, bytes);

	node_t<memory_traits_t> node { reinterpret_cast<byte_t *> (node_slot->entry->block + node_slot->offset) };
	node.set_key (key);
	node.set_vec_slot (vec_slot);
	node.set_level (level);

	return node_slot->id;
      }

      virtual pinned_t get_root (lock_mode mode) override
      {
	// exactly same with disk storage
	VPID root_vpid = m_root_vpid;

	PGBUF_LATCH_MODE pgbuf_mode = PGBUF_LATCH_READ;
	if (mode == lock_mode::exclusive)
	  {
	    pgbuf_mode = PGBUF_LATCH_WRITE;
	  }

	PAGE_PTR root_page_ptr = pgbuf_fix (m_thread_p, &root_vpid, OLD_PAGE, pgbuf_mode, PGBUF_UNCONDITIONAL_LATCH);
	assert (root_page_ptr != nullptr);

	// TODO: hardcoded slot id 1
	SPAGE_SLOT *slotp = spage_get_slot (root_page_ptr, 1);
	assert (slotp != nullptr);

	OID oid = { root_vpid.pageid, 1, root_vpid.volid };

	return make_pinned_block<memory_traits_t> (0 /* dummy */, (std::byte *) root_page_ptr + slotp->offset_to_record,
	       slotp->record_length, mode,
	       [this, root_page_ptr] (auto& blk) noexcept
	{
	  if (blk.mode == lock_mode::exclusive)
	    {
	      pgbuf_set_dirty (m_thread_p, reinterpret_cast<PAGE_PTR> (root_page_ptr), FREE);
	    }
	  else
	    {
	      pgbuf_unfix (m_thread_p, reinterpret_cast<PAGE_PTR> (root_page_ptr));
	    }
	}

						  );
      }

      virtual pinned_t get_node_by_slot_id (const slot_id_t &slot_id, const lock_mode &mode) override
      {
	auto it = m_block_table.find (slot_id);
	if (it == m_block_table.end())
	  {
	    assert (false);
	  }
	return make_pinned_block<memory_traits_t> (slot_id, (std::byte *) it->second->entry->block + it->second->offset,
	       it->second->size, mode,
	       [this, it] (auto& blk) noexcept
	{
	  // do nothing
	});
      }

      virtual pinned_t get_vector_by_slot_id (const slot_id_t &slot_id, const lock_mode &mode) override
      {
	pinned_t node_blk = get_node_by_slot_id (slot_id, lock_mode::shared);
	node_t<memory_traits_t> node = node_t<memory_traits_t> (node_blk.get().data);
	slot_id_t vec_slot = node.get_vec_slot();

	auto it = m_block_table.find (vec_slot);
	if (it == m_block_table.end())
	  {
	    assert (false);
	  }

	return make_pinned_block<memory_traits_t> (vec_slot, (std::byte *) it->second->entry->block + it->second->offset,
	       it->second->size, mode,
	       [this, it] (auto& blk) noexcept
	{
	  // do nothing
	});
      }

      // promote lockmode from shared to exclusive
      // TODO: not implemented
      virtual void promote_root (pinned_t &root) override
      {
	// do nothing
      }

    protected:

      slot_id_t add_vector (const OID &key, const float *vector)
      {
	std::size_t bytes = this->get_dimension () * sizeof (float);
	block_slot_t *slot = get_block_ptr_to_insert (m_last_vec_entry, bytes);
	std::memcpy (slot->entry->block + slot->offset, vector, bytes);
	return slot->id;
      }

      block_slot_t *get_block_ptr_to_insert (block_entry_t *&entry, std::size_t size)
      {
	std::lock_guard<std::mutex> lock (m_block_pool_mutex);
	if (entry == nullptr || (std::size_t) entry->remaining_size < size)
	  {
	    entry = new block_entry_t;
	    entry->num_elements = 0;
	    entry->remaining_size = IO_MAX_PAGE_SIZE;
	    memset (entry->block, 0, IO_MAX_PAGE_SIZE);
	    m_block_pool.push_front (entry);
	  }

	std::size_t offset = (std::size_t) IO_MAX_PAGE_SIZE - entry->remaining_size;
	entry->num_elements++;
	entry->remaining_size -= size;

	slot_id_t slot_id = m_slot_id.fetch_add (1, std::memory_order_relaxed);
	block_slot_t *slot = new block_slot_t { slot_id, entry, offset, size };
	m_block_table.emplace (slot_id, slot);
	return slot;
      }

    private:
      VPID m_root_vpid;

      block_group_id_t m_block_pool {};
      std::unordered_map<slot_id_t, block_slot_t *> m_block_table {};

      block_entry_t *m_last_node_entry {nullptr};
      block_entry_t *m_last_vec_entry {nullptr};
      std::mutex m_block_pool_mutex {};

      bool m_is_empty = true;

      std::atomic<slot_id_t> m_slot_id {0};
  };
}
