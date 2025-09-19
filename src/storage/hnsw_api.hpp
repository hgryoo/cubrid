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

/*
 * hnsw_api.hpp -
 */

#ifndef _HNSW_API_HPP_
#define _HNSW_API_HPP_

#include "dbtype_def.h"
#include "storage_common.h"

struct hnsw_build_params
{
  int dimension;
  int m;
  int ef_construction;
  DB_VECTOR_DISTANCE_METRIC metric;

  hnsw_build_params() : dimension(10), m(16), ef_construction(64), metric(DB_VECTOR_DISTANCE_METRIC::METRIC_EUCLIDEAN) {}
  hnsw_build_params(int dimension, int m, int ef_construction, DB_VECTOR_DISTANCE_METRIC metric) : dimension(dimension), m(m), ef_construction(ef_construction), metric(metric) {}
};

struct hnsw_index_meta
{
  std::string backend_id;
  hnsw_build_params build_params;
};

// forward declarations
class hnsw_index_backend;
class hnsw_index;
class hnsw_index_manager;

class hnsw_index_registry {
  using backend_creator = std::function<std::unique_ptr<hnsw_index_backend>()>;

  static hnsw_index_registry &instance()
  {
    static hnsw_index_registry inst;
    return inst;
  }

  // For DI: at engine startup
  void set_manager(hnsw_index_manager* mgr);
  hnsw_index_manager& get_manager() const;

  // To register backend
  void register_backend(const std::string& id, backend_creator creator);
  
  // create backend
  std::unique_ptr<hnsw_index_backend> create_backend(const std::string& id) const;
  std::unique_ptr<hnsw_index_backend> create_backend_for (BTID *btid);

  // maintain BTID -> index_meta
  void attach(BTID *btid, const hnsw_index_meta& index_meta);
  void detach(BTID *btid);
  bool find_index_meta(BTID *btid, hnsw_index_meta& index_meta) const;

  hnsw_index_backend* get_default_backend() const;
  hnsw_index_backend* get_index_backend(const std::string& id) const;

  private:
    hnsw_index_registry() : m_mgr(nullptr) {}
    ~hnsw_index_registry() = default;

    hnsw_index_registry (const hnsw_index_registry &) = delete;
    hnsw_index_registry &operator= (const hnsw_index_registry &) = delete;
    hnsw_index_registry (hnsw_index_registry &&) = delete;
    hnsw_index_registry &operator= (hnsw_index_registry &&) = delete;

    bool recover_and_cache_meta(BTID *btid, hnsw_index_meta& index_meta);

    static std::string default_backend_id();

    hnsw_index_manager* m_mgr;
    std::unordered_map<std::string, backend_creator> m_backend;
    std::unordered_map<BTID*, hnsw_index_meta> m_btid_meta;
    mutable std::mutex m_mutex;
};

template <class T>
struct hnsw_backend_auto_registrar {
  explicit hnsw_backend_auto_registrar(std::string id) {
    hnsw_index_registry::instance().register_backend(std::move(id),
      [](hnsw_index_backend& backend) { return std::make_unique<T>(backend); });
  }
};

#define REGISTER_HNSW_INDEX_BACKEND(TYPE, ID) \
  // static hnsw_backend_auto_registrar<TYPE> _hnsw_registrar_##TYPE{ID};
  static hnsw_backend_auto_registrar<TYPE> {ID};
#endif

/* only memory-based index registry is supported */
/* interface of CUBRID's HNSW index storage management*/
class hnsw_index_manager
{
  public:
    static hnsw_index_manager &instance()
    {
      static hnsw_index_manager inst;
      return inst;
    }

    void register_backend(std::unique_ptr<hnsw_index_backend> backend);
    const hnsw_index_backend* find_backend(std::string_view id) const;
    hnsw_index_backend* find_backend(std::string_view id);

	  bool has_index_loaded (const hnsw_index_backend* backend, BTID *btid) const;
    bool has_index_file (const hnsw_index_backend* backend, BTID *btid) const;
    std::string get_index_file_path(const hnsw_index_backend* backend, BTID *btid) const;
    std::string get_index_meta_file_path(const hnsw_index_backend* backend, BTID *btid) const;
    std::string get_index_directory_path(const hnsw_index_backend* backend);
    void create_index_directory(const hnsw_index_backend* backend);

    // index meta (on disk)
    virtual bool read_index_meta(BTID* btid, hnsw_index_meta& out) = 0;
    virtual bool write_index_meta(BTID* btid, const hnsw_index_meta& in) = 0;
  
    BTID create_btid(const hnsw_index_backend* backend);

  private:

    /* singleton */
    hnsw_index_manager() = default;
    ~hnsw_index_manager() = default;

    hnsw_index_manager (const hnsw_index_manager &) = delete;
    hnsw_index_manager &operator= (const hnsw_index_manager &) = delete;
    hnsw_index_manager (hnsw_index_manager &&) = delete;
    hnsw_index_manager &operator= (hnsw_index_manager &&) = delete;

    /* index directory root path */
	  std::string m_root_path;
    int m_index_id;

	  std::unordered_map<BTID, std::unique_ptr<hnsw_index>> m_index_map;
};

/* hnsw_index_manager uses this interface for arbtrary index */
class hnsw_index_backend {
	public:
    explicit hnsw_index_backend(hnsw_index_manager& mgr) : m_mgr(mgr) {}
    virtual ~hnsw_index_backend() = default;

    hnsw_index_backend(const hnsw_index_backend&) = delete;
    hnsw_index_backend& operator=(const hnsw_index_backend&) = delete;
    hnsw_index_backend(hnsw_index_backend&&) = delete;
    hnsw_index_backend& operator=(hnsw_index_backend&&) = delete;

	  virtual std::string get_id() const = 0;
	  virtual bool is_metric_supported(const DB_VECTOR_DISTANCE_METRIC& metric) const = 0;

    virtual hnsw_index* create_index(THREAD_ENTRY* thread_p, BTID* btid, const std::string& name, const hnsw_build_params& build_params) = 0;
    virtual int drop_index (THREAD_ENTRY* thread_p, const BTID *btid) = 0;

    virtual int save_index(THREAD_ENTRY* thread_p, const hnsw_index* index) = 0;
    virtual hnsw_index* load_index(THREAD_ENTRY* thread_p, const BTID *btid, const hnsw_build_params& build_params) = 0;

  protected:
    hnsw_index_manager& m_mgr;
};

class hnsw_index
{
  public:
    virtual ~hnsw_index() = default;

    const BTID *get_id() final;
    const std::string get_name() final;
    const DB_VECTOR_DISTANCE_METRIC get_metric() final;
    const int get_dimension() final;
    const int get_ef_construction() final;

    // operations
    virtual int prepare_to_add (int n_vectors, const OID *oid, const float *vector)=0;
    virtual int add (int n_vectors, const OID *oid, const float *vector)=0;

    virtual int search (const float *query, const int k, const int ef_search, OID *rec_oids, float *distances)=0;
    virtual int remove (const OID* oid)=0;
    virtual int update (const OID* oid, const float *vector)=0;

    // SCAN_PRED from query_evaluator.h
    virtual int filtered_search (const float *query, const int k, const SCAN_PRED& filter, OID *rec_oids, float *distances)=0;
	  virtual int dump (FILE* fp)=0;

  private:
    hnsw_index(hnsw_index_backend& backend, BTID* btid, const std::string& name, const hnsw_build_params& build_params);

    const hnsw_index_backend& m_backend;
    const BTID* m_btid;
    const std::string m_name;
    const hnsw_build_params m_build_params;
};

template <typename id_type>
class hnsw_oid_encoder
{
  public:
    virtual ~hnsw_oid_encoder() = default;

    virtual id_type encode_oid (const OID& oid)=0;
    virtual OID decode_oid (const id_type& id)=0;
};

class hnsw_oid_encoder_default: public hnsw_oid_encoder<int64_t>
{
  public:
    int64_t encode_oid (const OID& oid) override;
    OID decode_oid (const int64_t& id) override;
};
