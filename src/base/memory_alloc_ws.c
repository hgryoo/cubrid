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
 * memory_alloc_ws.c - Memory allocation module to define functions for workspace module
 */

#include "memory_alloc.h"

#if defined(SA_MODE)
#include "memory_alloc_sa.h"
#endif

#include <cassert>

#if !defined (SERVER_MODE)
/*
 * db_workspace_alloc () - call allocation function for the workspace heap
 *   return: allocated memory pointer
 *   size(in): size to allocate
 */
void *
db_workspace_alloc (size_t size)
{
  void *ptr = NULL;

  if (ws_heap_id == 0)
    {
      /* not initialized yet */
      db_create_workspace_heap ();
    }

  if (ws_heap_id == 0 || size == 0)
    {
      return NULL;
    }
#if defined(SA_MODE)
  ptr = db_standalone_alloc (ws_heap_id, PRIVATE_ALLOC_TYPE_WS, size);
#else
  ptr = hl_lea_alloc (ws_heap_id, size);
#endif
  return ptr;
}

/*
 * db_workspace_free () - call free function for the workspace heap
 *   return:
 *   ptr(in): memory pointer to free
 */
void
db_workspace_free (void *ptr)
{
  assert (ws_heap_id != 0);
  if (ws_heap_id == 0 || ptr == NULL)
    {
      return;
    }

#if defined(SA_MODE)
  db_standalone_free (ptr);
#else
  hl_lea_free (ws_heap_id, ptr);
#endif
}

/*
 * db_workspace_realloc () - call re-allocation function for the workspace heap
 *   return: allocated memory pointer
 *   ptr(in): memory pointer to reallocate
 *   size(in): size to allocate
 */
void *
db_workspace_realloc (void *ptr, size_t size)
{
  if (ptr == NULL)
    {
      return db_workspace_alloc (size);
    }

  if (ws_heap_id == 0)
    {
      /* not initialized yet */
      db_create_workspace_heap ();
    }

  if (ws_heap_id == 0 || size == 0)
    {
      return NULL;
    }

#if defined(SA_MODE)
  return db_standalone_realloc (ptr, size);
#else
  return hl_lea_realloc (ws_heap_id, ptr, size);
#endif
}
#endif