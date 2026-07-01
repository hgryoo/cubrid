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
 * lock_manager_conn.cpp - conn_entry helpers for the lock manager.
 *
 * Keeps the css_conn_entry dereference behind a single extern seam so that
 * lock_manager.c does not need to see the css_conn_entry class shape.
 */

#ident "$Id$"

#include "config.h"

#if defined(SERVER_MODE)

#include "lock_manager_internal.hpp"

#include "connection_sr.h"
#include "thread_entry.hpp"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

//
// lk_resolve_loaddb_tran_index - return the tran_index of the loaddb workers
//   manager thread that owns this loaddb worker. A loaddb worker thread does
//   not carry its own tran_index, so the owning conn_entry is consulted
//   instead.
//
int
lk_resolve_loaddb_tran_index (THREAD_ENTRY * thread_p)
{
  assert (thread_p != NULL);
  assert (thread_p->conn_entry != NULL);

  return thread_p->conn_entry->get_tran_index ();
}

#endif /* SERVER_MODE */
