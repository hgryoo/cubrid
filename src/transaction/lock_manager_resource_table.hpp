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
 * lock_manager_resource_table.hpp - Thin pimpl wrapper around
 *                                   cubthread::lockfree_hashmap for the lock
 *                                   manager's resource table.
 *
 * Hides the lockfree_hashmap template internals so that includers do not
 * need to pull in the cubthread headers.
 */

#ifndef _LOCK_MANAGER_RESOURCE_TABLE_HPP_
#define _LOCK_MANAGER_RESOURCE_TABLE_HPP_

#ident "$Id$"

#include "config.h"

#if defined(SERVER_MODE)

#include "lock_manager.h"
#include "lock_free.h"
#include "thread_compat.hpp"

#include <cstddef>

// *INDENT-OFF*
namespace lock_manager
{
  class resource_table_impl;            // pimpl: opaque to includers
  class resource_table_iterator_impl;   // pimpl: opaque to includers

  class resource_table
  {
    public:
      class iterator;

      resource_table ();
      ~resource_table ();

      resource_table (const resource_table &) = delete;
      resource_table &operator= (const resource_table &) = delete;

      void init (lf_tran_system &transys, int entry_idx, int hash_size, int freelist_block_size,
                 int freelist_block_count, lf_entry_descriptor &edesc);
      void destroy ();

      LK_RES *find (THREAD_ENTRY *thread_p, LK_RES_KEY &key);
      bool    find_or_insert (THREAD_ENTRY *thread_p, LK_RES_KEY &key, LK_RES *&res);
      bool    erase_locked (THREAD_ENTRY *thread_p, LK_RES_KEY &key, LK_RES *&res);

      std::size_t get_element_count () const;
      std::size_t get_alloc_element_count () const;

    private:
      friend class iterator;
      resource_table_impl *m_impl;
  };

  class resource_table::iterator
  {
    public:
      iterator (THREAD_ENTRY *thread_p, resource_table &table);
      ~iterator ();

      iterator (const iterator &) = delete;
      iterator &operator= (const iterator &) = delete;

      LK_RES *iterate ();

    private:
      resource_table_iterator_impl *m_impl;
  };
} // namespace lock_manager
// *INDENT-ON*

#endif /* SERVER_MODE */

#endif /* _LOCK_MANAGER_RESOURCE_TABLE_HPP_ */
