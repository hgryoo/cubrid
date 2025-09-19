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

//
// hnsw.cpp - implementation of HNSW index interface
//

#include "hnsw.hpp"
#include "hnsw_api.hpp"

static hnsw_index_registry &index_registry = hnsw_index_registry::instance();

void hnsw_initialize ()
{
  index_registry.set_manager(&hnsw_index_manager::instance());
}

int hnsw_finalize ()
{
  return NO_ERROR;
}

int
xhnsw_add_index (THREAD_ENTRY *thread_p, const hnsw_build_params& params)
{
  const hnsw_index_backend *backend = index_registry.get_default_backend();
  if (backend == NULL)
    {
      er_log_error (ARG_FILE_LINE, "Any hnsw index backend is not loaded");
      return ER_FAILED;
    }

  bool is_metric_supported = backend->is_metric_supported(params.metric);
  if (!is_metric_supported)
    {
      er_log_error (ARG_FILE_LINE, "Metric %s is not supported", vector_distance_metric_to_string(params.metric));
      return ER_FAILED;
    }

  int error = index_registry.create_index(backend, params);
  if (error != NO_ERROR)
    {
      er_log_error (ARG_FILE_LINE, "Failed to register index");
      return error;
    }

  return error;
}

int xhnsw_delete_index (THREAD_ENTRY *thread_p, BTID *btid)
{
    const hnsw_index_backend *backend = index_registry.get_default_backend();
    if (backend == NULL)
      {
        er_log_error (ARG_FILE_LINE, "Any hnsw index backend is not loaded");
        return ER_FAILED;
      }
      
  return index_registry.delete_index(backend, btid);
}

