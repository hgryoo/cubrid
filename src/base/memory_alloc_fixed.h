/*
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
 * memory_alloc_fixed.h - Memory allocation module
 */

#ifndef _MEMORY_ALLOC_FIXED_H_
#define _MEMORY_ALLOC_FIXED_H_

#include "memory_alloc.h"

extern HL_HEAPID db_create_fixed_heap (int req_size, int recs_per_chunk);
extern void db_destroy_fixed_heap (HL_HEAPID heap_id);
extern void *db_fixed_alloc (HL_HEAPID heap_id, size_t size);
extern void db_fixed_free (HL_HEAPID heap_id, void *ptr);

#endif