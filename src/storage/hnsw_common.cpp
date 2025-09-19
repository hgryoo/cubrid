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
// hnsw_common.cpp - implementation of HNSW index
//

#include "hnsw.hpp"
#include "error_manager.h"
#include "system_parameter.h"
#include "vector_opfunc.hpp"
#include "boot_sr.h"
#include "file_io.h"
#include "system_parameter.h"
#include "dbtype.h"
#include "db_vector.hpp"
#include "porting.h"
#include "vector_distance_enum.h"
#include "heap_file.h"
#include <cstddef>
#include <fstream>
#include <filesystem>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"


// TODO : When cub_server terminates, hnsw_index_id will be reset to 0.
//        This is not a problem in current implementation, but it may be a problem in the future,
//        such as duplicate hnsw_index_id when cub_server restarts.
//        We need to consider a better way to identify the hnsw index.

int hnsw_index_id = 0;
std::unordered_map<int, std::unique_ptr<usearch::index_dense_t>> hnsw_index_map;
char hnsw_index_directory[PATH_MAX] = {0};
bool hnsw_index_directory_created = false;
static std::mutex hnsw_elem_mutex;

static int dump_hnsw_index (int hnsw_id, const std::unique_ptr<usearch::index_dense_t> &index);
static int get_hnsw_index_file_path (int hnsw_id, char *out_path);
static int create_hnsw_index_directory ();
static bool is_hnsw_index_file_exists (int hnsw_id);
static int load_hnsw_index_from_file (int hnsw_id);
static int hnsw_check_and_load_index (int hnsw_id);
static int64_t encode_oid (const OID &oid);
static OID decode_oid (int64_t encoded_oid);

/* memory-based index registry */
static hnsw_index_manager index_registry = hnsw_index_manager::instance();

int
xhnsw_add_index (THREAD_ENTRY *thread_p, const hnsw_build_params& params)
{
  const hnsw_index_backend *spec = index_registry.get_index_spec();
  if (spec == NULL)
    {
      er_log_error (ARG_FILE_LINE, "Any hnsw index backend is not loaded");
      return ER_FAILED;
    }

  bool is_metric_supported = spec->is_metric_supported(params.metric);
  if (!is_metric_supported)
    {
      er_log_error (ARG_FILE_LINE, "Metric %s is not supported", vector_distance_metric_to_string(params.metric));
      return ER_FAILED;
    }

  int error = index_registry.create_index(spec, params);
  if (error != NO_ERROR)
    {
      er_log_error (ARG_FILE_LINE, "Failed to register index");
      return error;
    }

  return error;
}

int xhnsw_delete_index (THREAD_ENTRY *thread_p, BTID *btid)
{
    const hnsw_index_backend *spec = index_registry.get_index_spec();
    if (spec == NULL)
      {
        er_log_error (ARG_FILE_LINE, "Any hnsw index backend is not loaded");
        return ER_FAILED;
      }
      
  return index_registry.delete_index(spec, btid);
}

BTID *xhnsw_load_index (THREAD_ENTRY *thread_p, BTID *btid, OID *oid, int n_classes, int n_attrs, int *attr_ids,
			HFID *hfids, int dimension, int m, int ef_construction, int metric)
{
  HEAP_SCANCACHE scan_cache;
  SCAN_CODE scan_result;
  RECDES in_recdes;
  DB_VALUE *key_dbvalue;
  HEAP_CACHE_ATTRINFO attr_info;
  OID cur_oid;
  int cur_class = 0;
  int attr_offset = 0;
  OID_SET_NULL (&cur_oid);
  const DB_VECTOR_FLOAT *vf = NULL;

  BTID *new_btid = xhnsw_add_index (thread_p, btid, dimension, m, ef_construction, metric);

  while (cur_class < n_classes && HFID_IS_NULL (&hfids[cur_class]))
    {
      cur_class++;
    }

  if (heap_scancache_start (thread_p, &scan_cache, &hfids[cur_class], &oid[cur_class], true, NULL) != NO_ERROR)
    {
      return NULL;
    }

  attr_offset = cur_class * n_attrs;

  if (heap_attrinfo_start (thread_p, &oid[cur_class], n_attrs, &attr_ids[attr_offset], &attr_info) != NO_ERROR)
    {
      return NULL;
    }

  do
    {
      scan_result = heap_next (thread_p, &hfids[cur_class], &oid[cur_class], &cur_oid,
			       &in_recdes, &scan_cache,
			       scan_cache.cache_last_fix_page ? PEEK : COPY);

      switch (scan_result)
	{
	case S_SUCCESS:
	  heap_attrinfo_read_dbvalues (thread_p, &cur_oid, &in_recdes, &attr_info);

	  key_dbvalue = &attr_info.values[0].dbvalue;
	  assert (db_value_type (key_dbvalue) == DB_TYPE_VECTOR);

	  vf = db_get_vector_float (key_dbvalue);
	  hnsw_add_element (new_btid, &cur_oid, vf->float_array, 1);
	  continue;
	case S_END:
	  heap_attrinfo_end (thread_p, &attr_info);
	  (void) heap_scancache_end (thread_p, &scan_cache);
	  return new_btid;
	  break;
	default:
	  assert (false);
	  return NULL;
	}
    }
  while (true);

  return new_btid;
}

BTID *xhnsw_load_index_batch (THREAD_ENTRY *thread_p, BTID *btid, OID *oid, int n_classes, int n_attrs, int *attr_ids,
			      HFID *hfids, const hnsw_build_params& params)
{
  HEAP_SCANCACHE scan_cache;
  SCAN_CODE scan_result;
  RECDES in_recdes;
  DB_VALUE *key_dbvalue;
  HEAP_CACHE_ATTRINFO attr_info;
  OID cur_oid;
  int cur_class = 0;
  int attr_offset = 0;
  OID_SET_NULL (&cur_oid);

  BTID *new_btid = xhnsw_add_index (thread_p, btid, params);
  if (new_btid == NULL)
    {
      return NULL;
    }

  while (cur_class < n_classes && HFID_IS_NULL (&hfids[cur_class]))
    {
      cur_class++;
    }

  if (heap_scancache_start (thread_p, &scan_cache, &hfids[cur_class], &oid[cur_class], true, NULL) != NO_ERROR)
    {
      return NULL;
    }

  attr_offset = cur_class * n_attrs;

  if (heap_attrinfo_start (thread_p, &oid[cur_class], n_attrs, &attr_ids[attr_offset], &attr_info) != NO_ERROR)
    {
      (void) heap_scancache_end (thread_p, &scan_cache);
      return NULL;
    }

  /* -------- Batch buffers --------
     - oids:    growable array of OID (count elements)
     - vectors: contiguous float buffer of size (capacity * dimension)
  */
  int capacity = 1024;
  int count = 0;
  OID *oids = (OID *) malloc ((size_t) capacity * sizeof (OID));
  float *vectors = (float *) malloc ((size_t) capacity * (size_t) dimension * sizeof (float));
  if (oids == NULL || vectors == NULL)
    {
      if (oids)
	{
	  free (oids);
	}
      if (vectors)
	{
	  free (vectors);
	}
      heap_attrinfo_end (thread_p, &attr_info);
      (void) heap_scancache_end (thread_p, &scan_cache);
      return NULL;
    }

  auto ensure_capacity = [&] (void) -> bool
  {
    if (count < capacity)
      {
	return true;
      }
    int new_cap = capacity * 2;
    OID *new_oids = (OID *) realloc (oids, (size_t) new_cap * sizeof (OID));
    float *new_vectors = (float *) realloc (vectors, (size_t) new_cap * (size_t) dimension * sizeof (float));
    if (new_oids == NULL || new_vectors == NULL)
      {
	if (new_oids)
	  {
	    oids = new_oids;
	  }
	if (new_vectors)
	  {
	    vectors = new_vectors;
	  }
	return false;
      }
    oids = new_oids;
    vectors = new_vectors;
    capacity = new_cap;
    return true;
  };

  do
    {
      scan_result = heap_next (thread_p, &hfids[cur_class], &oid[cur_class], &cur_oid,
			       &in_recdes, &scan_cache,
			       scan_cache.cache_last_fix_page ? PEEK : COPY);

      switch (scan_result)
	{
	case S_SUCCESS:
	  heap_attrinfo_read_dbvalues (thread_p, &cur_oid, &in_recdes, &attr_info);

	  key_dbvalue = &attr_info.values[0].dbvalue;
	  assert (db_value_type (key_dbvalue) == DB_TYPE_VECTOR);

	  {
	    const DB_VECTOR_FLOAT *vf = db_get_vector_float (key_dbvalue);
	    assert (vf != NULL && vf->dim == dimension);

	    if (!ensure_capacity ())
	      {
		if (oids)
		  {
		    free (oids);
		  }
		if (vectors)
		  {
		    free (vectors);
		  }
		heap_attrinfo_end (thread_p, &attr_info);
		(void) heap_scancache_end (thread_p, &scan_cache);
		return NULL;
	      }

	    oids[count] = cur_oid;
	    float *dst = vectors + ((size_t) count * (size_t) dimension);
	    memcpy (dst, vf->float_array, (size_t) dimension * sizeof (float));

	    count++;
	  }
	  continue;

	case S_END:
	{
	  hnsw_add_element (new_btid, oids, vectors, count);

	  if (oids)
	    {
	      free (oids);
	    }
	  if (vectors)
	    {
	      free (vectors);
	    }

	  heap_attrinfo_end (thread_p, &attr_info);
	  (void) heap_scancache_end (thread_p, &scan_cache);

	  return new_btid;
	}

	default:
	  if (oids)
	    {
	      free (oids);
	    }
	  if (vectors)
	    {
	      free (vectors);
	    }
	  heap_attrinfo_end (thread_p, &attr_info);
	  (void) heap_scancache_end (thread_p, &scan_cache);
	  assert (false);
	  return NULL;
	}
    }
  while (true);

  return new_btid;
}

static int dump_hnsw_index (int hnsw_id, const std::unique_ptr<usearch::index_dense_t> &index)
{
  char filepath[PATH_MAX];

  if (!index)
    {
      return ER_FAILED;
    }

  if (get_hnsw_index_file_path (hnsw_id, filepath) != NO_ERROR)
    {
      return ER_FAILED;
    }

  index->save (filepath);

  return NO_ERROR;
}

static int get_hnsw_index_file_path (int hnsw_id, char *out_path)
{
  int written = snprintf (out_path, PATH_MAX, "%s%c%s_hnsw_%d.bin", hnsw_index_directory, PATH_SEPARATOR, boot_db_name(),
			  hnsw_id);
  if (written < 0 || written >= PATH_MAX)
    {
      er_log_debug (ARG_FILE_LINE, "Failed to create path for dumping HNSW Index %d since path is too long", hnsw_id);
      return ER_FAILED;
    }

  return NO_ERROR;
}

static int create_hnsw_index_directory ()
{
  char db_path[PATH_MAX];

  if (hnsw_index_directory_created)
    {
      return NO_ERROR;
    }
  else
    {
      fileio_get_directory_path (db_path, boot_db_full_name());
      int written = snprintf (hnsw_index_directory, PATH_MAX, "%s%cvindex", db_path, PATH_SEPARATOR);
      if (written < 0 || written >= PATH_MAX)
	{
	  er_log_debug (ARG_FILE_LINE, "Failed to create path for HNSW Index directory since path is too long");
	  return ER_FAILED;
	}

      if (std::filesystem::exists (hnsw_index_directory))
	{
	  hnsw_index_directory_created = true;
	  return NO_ERROR;
	}

      if (std::filesystem::create_directory (hnsw_index_directory))
	{
	  hnsw_index_directory_created = true;
	  return NO_ERROR;
	}
      else
	{
	  assert (false);
	  _er_log_debug (ARG_FILE_LINE, "Failed to create HNSW Index directory");
	  return ER_FAILED;
	}
    }
}

static bool is_hnsw_index_file_exists (int hnsw_id)
{
  char filepath[PATH_MAX];

  if (get_hnsw_index_file_path (hnsw_id, filepath) != NO_ERROR)
    {
      return false;
    }

  return std::filesystem::exists (filepath);
}

static int load_hnsw_index_from_file (int hnsw_id)
{
  char filepath[PATH_MAX];

  if (!hnsw_index_directory_created)
    {
      if (create_hnsw_index_directory () != NO_ERROR)
	{
	  return ER_FAILED;
	}
    }

  if (get_hnsw_index_file_path (hnsw_id, filepath) != NO_ERROR)
    {
      return ER_FAILED;
    }

  if (!std::filesystem::exists (filepath))
    {
      assert (false);
      _er_log_debug (ARG_FILE_LINE, "HNSW Index file does not exist for ID %d in %s", hnsw_id, filepath);
      return ER_FAILED;
    }

  try
    {
      auto index = std::make_unique<usearch::index_dense_t> ();
      index->load (filepath);
      // index->view (filepath);

      hnsw_index_map[hnsw_id] = std::move (index);
      er_log_debug (ARG_FILE_LINE, "HNSW Index loaded from file for ID %d in %s", hnsw_id, filepath);

      return NO_ERROR;
    }
  catch (const std::runtime_error &e)
    {
      er_log_debug (ARG_FILE_LINE, "Failed to load/create HNSW Index %d: %s", hnsw_id, e.what());
      return ER_FAILED;
    }

  return NO_ERROR;
}

static int hnsw_check_and_load_index (int hnsw_id)
{
  if (hnsw_index_map.find (hnsw_id) == hnsw_index_map.end())
    {
      assert (hnsw_index_directory_created);
      if (load_hnsw_index_from_file (hnsw_id) != NO_ERROR)
	{
	  assert (false);
	  _er_log_debug (ARG_FILE_LINE, "Failed to load HNSW Index with ID %d", hnsw_id);
	  return ER_FAILED;
	}
    }

  return NO_ERROR;
}

/* Refactor the following functions to move into index object */

/* index_object.dump() */
int hnsw_print_index_info (BTID *btid)
{
  if (!btid)
    {
      return ER_FAILED;
    }

  int hnsw_id = btid->root_pageid;

  if (hnsw_check_and_load_index (hnsw_id) != NO_ERROR)
    {
      return ER_FAILED;
    }

  auto it = hnsw_index_map.find (hnsw_id);
  if (it == hnsw_index_map.end() || it->second == nullptr)
    {
      return ER_FAILED;
    }

  std::unique_ptr<usearch::index_dense_t> &index = it->second;

  std::ostringstream oss;

  oss << "HNSW Index Information for ID: " << hnsw_id << "\n";
  oss << "  - Dimension: " << index->dimensions() << "\n";
  oss << "  - Metric Type: " << metric_kind_name (index->metric_kind()) << "\n";
  oss << "  - Total Elements: " << index->size() << "\n";
  oss << "  - HNSW efConstruction: " << index->expansion_add() << "\n";
  oss << "  - HNSW efSearch: " << index->expansion_search() << "\n";

  er_log_debug (ARG_FILE_LINE, "%s", oss.str().c_str());

  return NO_ERROR;
}

/* index_object.add() */
int
xhnsw_add_element (BTID *btid, OID *oid, float *vector, int n_vectors)
{
  int error = NO_ERROR;

  auto index_ptr = index_registry.find_index(btid);
  if (index_ptr == NULL)
    {
      return ER_FAILED;
    }

  /* pre-processing */
  index_ptr->prepare_to_add(n_vectors,);

  /* add elements loop */
  #ifdef _OPENMP
        # pragma omp parallel for schedule(static)
  #endif
        size_t dimension = index_ptr->get_dimension();
        for (int i = 0; i < n_vectors; ++i)
        {
            if (index_ptr->get_metric () == DB_VECTOR_DISTANCE_METRIC::METRIC_COSINE && db_vector_is_all_zeros (vector + i * dimension, dimension))
            {
                er_log_debug (ARG_FILE_LINE, "Vector is all zeros, skipping add");
                continue;
            }

            /* thread-safe shoudl be checked */
            error = index_ptr->add(oid[i], vector + i * dimension);
            // TODO: error handling
        }

  return error;
}

/* index_object.search (query, k) */
/* filtered search should be supported too */
int hnsw_search_element (BTID *btid, DB_VALUE *key_dbvalue, int k, OID *rec_oids, float *distances)
{
  int error = NO_ERROR;

  auto index_ptr = index_registry.find_index(btid);
  if (index_ptr == NULL)
    {
      return ER_FAILED;
    }

  const DB_VECTOR_FLOAT *vf = db_get_vector_float (key_dbvalue);
  size_t dimension = index_ptr->get_dimension();

  if (index_ptr->get_metric () == DB_VECTOR_DISTANCE_METRIC::METRIC_COSINE && db_vector_is_all_zeros (vf->float_array, dimension))
    {
      er_log_debug (ARG_FILE_LINE, "Vector is all zeros, skipping search");
      return NO_ERROR;
    }

  int ef_search = prm_get_integer_value (PRM_ID_VECTOR_INDEX_EF_SEARCH);
  return index_ptr->search (vf->float_array, k, ef_search, rec_oids, distances);
}
