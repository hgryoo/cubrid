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

/*
 * lock_manager_resource_table.cpp - Implementation of the lock manager's
 *                                   resource-table wrapper over
 *                                   cubthread::lockfree_hashmap.
 */

#ident "$Id$"

#include "config.h"

#if defined(SERVER_MODE)

#include "lock_manager_resource_table.hpp"

#include "thread_entry.hpp"
#include "thread_lockfree_hash_map.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

// *INDENT-OFF*
namespace lock_manager
{
  class resource_table_impl
  {
    public:
      cubthread::lockfree_hashmap<LK_RES_KEY, LK_RES> m_hashmap;
  };

  class resource_table_iterator_impl
  {
    public:
      resource_table_iterator_impl (cubthread::entry *thread_p,
                                    cubthread::lockfree_hashmap<LK_RES_KEY, LK_RES> &hashmap)
        : m_iter { thread_p, hashmap }
      {
      }

      cubthread::lockfree_hashmap<LK_RES_KEY, LK_RES>::iterator m_iter;
  };

  resource_table::resource_table ()
    : m_impl (new resource_table_impl ())
  {
  }

  resource_table::~resource_table ()
  {
    delete m_impl;
  }

  void
  resource_table::init (lf_tran_system &transys, int entry_idx, int hash_size, int freelist_block_size,
                        int freelist_block_count, lf_entry_descriptor &edesc)
  {
    m_impl->m_hashmap.init (transys, entry_idx, hash_size, freelist_block_size, freelist_block_count, edesc);
  }

  void
  resource_table::destroy ()
  {
    m_impl->m_hashmap.destroy ();
  }

  LK_RES *
  resource_table::find (THREAD_ENTRY *thread_p, LK_RES_KEY &key)
  {
    return m_impl->m_hashmap.find (thread_p, key);
  }

  bool
  resource_table::find_or_insert (THREAD_ENTRY *thread_p, LK_RES_KEY &key, LK_RES *&res)
  {
    return m_impl->m_hashmap.find_or_insert (thread_p, key, res);
  }

  bool
  resource_table::erase_locked (THREAD_ENTRY *thread_p, LK_RES_KEY &key, LK_RES *&res)
  {
    return m_impl->m_hashmap.erase_locked (thread_p, key, res);
  }

  std::size_t
  resource_table::get_element_count () const
  {
    return m_impl->m_hashmap.get_element_count ();
  }

  std::size_t
  resource_table::get_alloc_element_count () const
  {
    return m_impl->m_hashmap.get_alloc_element_count ();
  }

  resource_table::iterator::iterator (THREAD_ENTRY *thread_p, resource_table &table)
    : m_impl (new resource_table_iterator_impl (thread_p, table.m_impl->m_hashmap))
  {
  }

  resource_table::iterator::~iterator ()
  {
    delete m_impl;
  }

  LK_RES *
  resource_table::iterator::iterate ()
  {
    return m_impl->m_iter.iterate ();
  }
} // namespace lock_manager
// *INDENT-ON*

#endif /* SERVER_MODE */
