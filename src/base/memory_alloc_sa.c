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

#include "memory_alloc_sa.h"

/*
 * db_standalone_alloc () - call allocation function for SA mode
 *   return: allocated memory pointer
 *   heap_id(in): heap id
 *   alloc_type(in): allocation type
 *   size(in): size to allocate
 */
void *
db_standalone_alloc (HL_HEAPID & heap_id, private_alloc_type alloc_type, size_t size)
{
  size_t req_sz = private_request_size (size);
  PRIVATE_MALLOC_HEADER *h = (PRIVATE_MALLOC_HEADER *) hl_lea_alloc (heap_id, req_sz);

  if (h != NULL)
    {
      h->magic = PRIVATE_MALLOC_HEADER_MAGIC;
      h->alloc_type = alloc_type;
      return private_hl2user_ptr (h);
    }
  return NULL;
}

/*
 * db_standalone_realloc () - call re-allocation function for SA mode
 *   return: allocated memory pointer
 *   heap_id(in): heap id
 *   ptr(in): memory pointer to reallocate
 *   size(in): size to reallocate
 */
void *
db_standalone_realloc (void *ptr, size_t size)
{
  PRIVATE_MALLOC_HEADER *h = private_user2hl_ptr (ptr);
  if (h->magic != PRIVATE_MALLOC_HEADER_MAGIC)
    {
      return NULL;
    }

  size_t req_sz = private_request_size (size);
  PRIVATE_MALLOC_HEADER *new_h = NULL;
  if (h->alloc_type == PRIVATE_ALLOC_TYPE_WS)
    {
      new_h = (PRIVATE_MALLOC_HEADER *) hl_lea_realloc (ws_heap_id, h, req_sz);
    }
  else if (h->alloc_type == PRIVATE_ALLOC_TYPE_LEA)
    {
      new_h = (PRIVATE_MALLOC_HEADER *) hl_lea_realloc (private_heap_id, h, req_sz);
    }
  else
    {
      return NULL;
    }

  if (new_h == NULL)
    {
      return NULL;
    }
  return private_hl2user_ptr (new_h);
}

/*
 * db_standalone_free () - call free function for SA mode
 *   return:
 *   ptr(in): memory pointer to free
 */
void
db_standalone_free (void *ptr)
{
  PRIVATE_MALLOC_HEADER *h = private_user2hl_ptr (ptr);
  if (h->magic != PRIVATE_MALLOC_HEADER_MAGIC)
    {
      /* assertion point */
      return;
    }

  if (h->alloc_type == PRIVATE_ALLOC_TYPE_WS)
    {
      hl_lea_free (ws_heap_id, h);
    }
  else if (h->alloc_type == PRIVATE_ALLOC_TYPE_LEA)
    {
      hl_lea_free (private_heap_id, h);
    }
  else
    {
      return;
    }
}