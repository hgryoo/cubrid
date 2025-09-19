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
// hnsw_usearch.cpp - implementation of HNSW index using USearch
//

#include "hnsw_api.hpp"

#include <usearch/index.hpp>
#include <usearch/index_dense.hpp>
#include <usearch/index_plugins.hpp>

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

using namespace unum;

static hnsw_oid_encoder_default oid_encoder;

class hnsw_index_backend_usearch final: public hnsw_index_backend
{
  public:
    hnsw_index_backend_usearch (hnsw_index_manager& mgr) : hnsw_index_backend (mgr) {}
    ~hnsw_index_backend_usearch() = default;

    std::string get_id() const override;

    virtual hnsw_index *create_index (const hnsw_build_params &build_params) override;

  private:
    usearch::metric_kind_t to_usearch_metric_kind (const DB_VECTOR_DISTANCE_METRIC &metric)
};

class hnsw_index_usearch: public hnsw_index
{
  public:

    hnsw_index_usearch (hnsw_index_backend& backend, BTID* btid, const std::string& name, const hnsw_build_params& build_params);
    ~hnsw_index_usearch() = default;

  private:
    std::mutex m_index_mutex;
    usearch::index_dense_t *m_index;
};

// ====================
// hnsw_index_backend_usearch
// ====================

std::string
hnsw_index_backend_usearch::get_id() const
{
  return "usearch";
}

usearch::metric_kind_t
hnsw_index_backend_usearch::to_usearch_metric_kind (const DB_VECTOR_DISTANCE_METRIC &metric)
{
  switch (metric)
    {
    case METRIC_COSINE:
      return usearch::metric_kind_t::cos_k;
    case METRIC_DOT:
      return usearch::metric_kind_t::ip_k;
    case METRIC_EUCLIDEAN:
      return usearch::metric_kind_t::l2sq_k;
    case METRIC_MANHATTAN:
      return usearch::metric_kind_t::l1_k;
    default:
      assert (false);
      return usearch::metric_kind_t::unknown_k;
    }
}

bool
hnsw_index_backend_usearch::is_metric_supported (const DB_VECTOR_DISTANCE_METRIC &metric)
{
  switch (metric)
    {
    case METRIC_COSINE:
    case METRIC_DOT:
    case METRIC_EUCLIDEAN:
      return true;
    case METRIC_UNKNOWN:
    case METRIC_MANHATTAN:
    default:
      return false;
    }
}

hnsw_index_usearch *
hnsw_index_backend_usearch::create_index (const hnsw_build_param &build_params)
{
  usearch::metric_kind_t metric_kind = hnsw_index_usearch::to_usearch_metric_kind (build_params.metric);
  usearch::metric_punned_t metric_punned (static_cast <std::size_t> (build_params.dimension), metric_kind,
					  usearch::scalar_kind_t::f32_k);

  usearch::index_dense_config_t config;
  config.connectivity = build_params.m;
  config.expansion_add = build_params.ef_construction;

  auto usearch_index = usearch::index_dense_t::make (metric_punned, config);
  const int initial_size = 1024;
  usearch_index->reserve (initial_size);
  hnsw_index_usearch *index = new hnsw_index_usearch (usearch_index);

  return index;
}

bool
hnsw_index_backend_usearch::save_index (const hnsw_index *index)
{
  assert (false);
  return ER_FAILED;
}

REGISTER_HNSW_INDEX_BACKEND (hnsw_index_backend_usearch, "usearch");

// ====================
// hnsw_index_usearch
// ====================

int
hnsw_index_usearch::prepare_to_add (int n_vectors, const OID *oid, const float *vector)
{
  std::lock_guard<std::mutex> lock (m_index_mutex);
	size_t need = m_index->size () + static_cast<size_t> (n_vectors);
	m_index->reserve (need + 1024);
  return NO_ERROR;
}

int
hnsw_index_usearch::add (int n_vectors, const OID *oid, const float *vector)
{
  try
    {
      int64_t encoded_oid;
      for (int i = 0; i < n_vectors; ++i)
	{
	  encoded_oid = oid_encoder.encode_oid (oid[i]);
	  m_index->add (encoded_oid, vector + i * m_index->dimensions());
	}
    }
  catch (const std::runtime_error &e)
    {
      er_log_debug (ARG_FILE_LINE, "USearch exception during add: %s", e.what());
      return ER_FAILED;
    }
  return NO_ERROR;
}

int
hnsw_index_usearch::search (const float *query, const int k, const int ef_search, OID *rec_oids, float *distances)
{
  m_index->change_expansion_search (ef_search);
  auto results = m_index->search (query, k);
  for (std::size_t i = 0; i != results.size(); ++i)
    {
      rec_oids[i] = oid_encoder.decode_oid (results[i].member.key);
      distances[i] = results[i].distance;
    }
  return NO_ERROR;
}

int
hnsw_index_usearch::filtered_search (const float *query, const int k, const SCAN_PRED& filter, OID *rec_oids, float *distances)
{
  assert (false);
  return ER_FAILED;
}

int
hnsw_index_usearch::remove (const OID* oid)
{
  assert (false);
  return ER_FAILED;
}

int
hnsw_index_usearch::update (const OID* oid, const float *vector)
{
  assert (false);
  return ER_FAILED;
}

int
hnsw_index_usearch::dump (FILE *fp)
{
  assert (false);
  return ER_FAILED;
}
