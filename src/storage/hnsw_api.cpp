#include "hnsw_api.hpp"

// ====================
// hnsw_index_registry
// ====================

void
hnsw_index_registry::set_manager(hnsw_index_manager* mgr)
{
    if (m_mgr != nullptr)
    {
        throw std::runtime_error("Manager already set");
    }
    m_mgr = mgr;
}

hnsw_index_manager&
hnsw_index_registry::get_manager() const
{
    if (m_mgr == nullptr)
    {
        throw std::runtime_error("Manager not set");
    }
    return *m_mgr;
}

void
hnsw_index_registry::register_backend(const std::string& id, backend_creator creator)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto [it, ok] = m_backend.emplace(std::move(id), std::move(creator));
    if (!ok)
    {
        throw std::runtime_error("Backend already registered with ID: " + id);
    }
}

std::unique_ptr<hnsw_index_backend>
hnsw_index_registry::create_backend(const std::string& id) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_backend.find(std::string(id));
    if (it == m_backend.end()) 
    {
        throw std::runtime_error("Unknown HNSW backend id");
    }
    if (m_mgr == nullptr)
    {
        throw std::runtime_error("Manager not set");
    }
    return (it->second)(*m_mgr);
}

std::unique_ptr<hnsw_index_backend>
hnsw_index_registry::create_backend_for (BTID *btid)
{
    hnsw_index_meta meta;
    if (!find_index_meta(btid, meta) && !recover_and_cache_meta(btid, meta))
    {
        throw std::runtime_error("No index meta for BTID (missing/corrupt)");
    }
    return create_backend(meta.backend_id);
}

void
hnsw_index_registry::attach(BTID *btid, const hnsw_index_meta& index_meta)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_btid_meta[btid] = index_meta;
}

void
hnsw_index_registry::detach(BTID *btid)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_btid_meta.erase(btid);
}

bool
hnsw_index_registry::find_index_meta(BTID *btid, hnsw_index_meta& index_meta) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_btid_meta.find(btid);
    if (it != m_btid_meta.end()) {
        index_meta = it->second;
        return true;
    }
    return false;
}

bool
hnsw_index_registry::recover_and_cache_meta(BTID *btid, hnsw_index_meta& index_meta)
{
    auto& mgr = get_manager();
    if (mgr.read_index_meta(btid, index_meta))
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_btid_meta[btid] = index_meta;
        return true;
    }
    else
    {
        return false;
    }
}

hnsw_index_backend*
hnsw_index_registry::get_index_backend(const std::string& id) const
{
    return create_backend(id).get();
}

// ====================
// hnsw_index_manager
// ====================

const std::string
hnsw_index_manager::get_index_file_path(const hnsw_index_backend* spec, BTID *btid)
{
    return get_index_directory_path(spec) + std::string(PATH_SEPARATOR) + spec->get_name() + std::string("_") + std::to_string(btid->root_pageid) + std::string(".bin");
}

const std::string
hnsw_index_manager::get_index_meta_file_path(const hnsw_index_backend* spec, BTID *btid)
{
    return get_index_directory_path(spec) + std::string(PATH_SEPARATOR) + spec->get_name() + std::string("_") + std::to_string(btid->root_pageid) + std::string(".meta");
}

bool
hnsw_index_manager::has_index_loaded(const hnsw_index_backend* spec, BTID *btid) const
{
    return find_index(btid) != nullptr;
}

bool
hnsw_index_manager::has_index_file(const hnsw_index_backend* spec, BTID *btid) const
{
    return std::filesystem::exists(get_index_file_path(spec, btid));
}

void
hnsw_index_manager::create_index_directory(const hnsw_index_backend* spec)
{
    if (m_root_path.empty())
    {
        char db_path[PATH_MAX];
        fileio_get_directory_path (db_path, boot_db_full_name());
        m_root_path = std::string(db_path) + std::string(PATH_SEPARATOR) + std::string("vindex");
    }

    if (!std::filesystem::exists (m_root_path))
    {
        std::filesystem::create_directory (m_root_path);
    }
    
    std::string index_path = get_index_path(spec);
    if (!std::filesystem::exists (index_path))
    {
        std::filesystem::create_directory (index_path);
    }
}

const std::string
hnsw_index_manager::get_index_directory_path(const hnsw_index_backend* spec)
{
  return m_root_path + std::string(PATH_SEPARATOR) + spec->get_name();
}


hnsw_index*
hnsw_index_manager::find_index(const BTID *btid)
{
    auto it = m_index_map.find(*btid);
    if (it != m_index_map.end()) {
        return it->second.get();
    }
    return nullptr;
}

BTID
hnsw_index_manager::create_btid(const hnsw_index_backend* backend)
{
    BTID btid = {.vfid = VFID_INITIALIZER, .root_pageid = m_index_id};
    while (true)
    {
        if (has_index_file(backend, &btid))
        {
            m_index_id++;
            btid.root_pageid = m_index_id;
            continue;
        }
        else
        {
            if (has_index_loaded(backend, &btid))
            {
                break;
            }
            else
            {
                m_index_id++;
                btid.root_pageid = m_index_id;
                continue;
            }
        }
    }
    return btid;
}

int
hnsw_index_manager::create_index(const hnsw_index_backend* backend, const hnsw_build_params& build_params)
{
  /* create index object */
  auto index_ptr = backend->create_index(build_params);
  if (index_ptr == NULL)
    {
      er_log_error (ARG_FILE_LINE, "Failed to create index");
      return ER_FAILED;
    }

  /* register index object */
  BTID btid = create_btid(backend);
  if (btid == NULL)
    {
      er_log_error (ARG_FILE_LINE, "Failed to create BTID");
      return ER_FAILED;
    }

    if (find_index(btid) != nullptr)
    {
        return ER_FAILED;
    }
    
    m_index_map[*btid] = std::move(index);
    return NO_ERROR;
}

int
hnsw_index_manager::delete_index(const hnsw_index_backend* backend, BTID *btid)
{
    auto index_ptr = find_index(btid);
    if (index_ptr == nullptr)
    {
        er_log_debug (ARG_FILE_LINE, "HNSW Index not found with ID %d", btid->root_pageid);
        return ER_FAILED;
    }
    else
    {
        er_log_debug (ARG_FILE_LINE, "HNSW Index deleted with ID %d", btid->root_pageid);
        m_index_map.erase(*btid);
    }

    std::string index_path = get_index_file_path(backend, btid);
    if (std::filesystem::exists(index_path))
    {
        std::filesystem::remove(index_path);
    }

    return NO_ERROR;
}

int
hnsw_index_manager::save_all_indices()
{
    for (const auto &pair : m_index_map)
    {
        if (save_index(backend, &pair.first) != NO_ERROR)
        {
            assert (false);
            er_log_debug (ARG_FILE_LINE, "Failed to dump HNSW Index with ID %d", pair.first.root_pageid);
        }
    }
    return NO_ERROR;
}

int
hnsw_index_manager::save_index(const hnsw_index_backend* spec, BTID *btid)
{
    auto index_ptr = find_index(btid);
    if (index_ptr == nullptr)
    {
        er_log_debug (ARG_FILE_LINE, "HNSW Index not found with ID %d", btid->root_pageid);
        return ER_FAILED;
    }

    std::string index_directory_path = get_index_directory_path(spec);
    return index_ptr->save_index(index_directory_path);
}

int
hnsw_index_manager::load_index(const hnsw_index_backend* spec, BTID *btid)
{
    if (has_index_loaded(spec, btid))
    {
        return NO_ERROR;
    }

    create_index_directory(spec);

    std::string index_path = get_index_file_path(spec, btid);
    if (!std::filesystem::exists(index_path))
    {
        er_log_debug (ARG_FILE_LINE, "HNSW Index file does not exist with ID %d", btid->root_pageid);
        return ER_FAILED;
    }

    // get build params from index file
    hnsw_build_params build_params = get_build_params
    

    auto index_ptr = spec->load_index(index_path);
    if (index_ptr == nullptr)
    {
        er_log_debug (ARG_FILE_LINE, "Failed to load HNSW Index with ID %d", btid->root_pageid);
        return ER_FAILED;
    }

    m_index_map[*btid] = std::move(index_ptr);
    return NO_ERROR;
}

void hnsw_index_manager::register_backend(std::unique_ptr<hnsw_index_backend> backend)
{
    std::string key(backend->get_id());
    m_backends.emplace(std::move(key), std::move(backend));
}

const hnsw_index_backend* hnsw_index_manager::find_backend(std::string_view id) const
{
    auto it = m_backends.find(std::string(id));
    return it == m_backends.end() ? nullptr : it->second.get();
}

hnsw_index_backend* hnsw_index_manager::find_backend(std::string_view id)
{
    auto it = m_backends.find(std::string(id));
    return it == m_backends.end() ? nullptr : it->second.get();
}

// ====================
// hnsw_index
// ====================

hnsw_index::hnsw_index(hnsw_index_backend& backend, BTID* btid, const std::string& name, const hnsw_build_params& build_params)
{
    m_btid = btid;
    m_name = name;
    m_build_params = build_params;
    m_backend = backend;
}

const BTID *
hnsw_index::get_id()
{
    return m_btid;
}

const std::string 
hnsw_index::get_name()
{
    return m_name;
}

const DB_VECTOR_DISTANCE_METRIC 
hnsw_index::get_metric()
{
    return m_build_params.metric;
}

const int 
hnsw_index::get_dimension()
{
    return m_build_params.dimension;
}

const int 
hnsw_index::get_ef_construction()
{
    return m_build_params.ef_construction;
}

// ====================
// hnsw_oid_encoder_default
// ====================

int64_t
hnsw_oid_encoder_default::encode_oid (const OID& oid)
{
    return (static_cast<int64_t> (oid.pageid) << 32) |
           (static_cast<uint32_t> (oid.slotid) << 16) |
           (static_cast<uint16_t> (oid.volid));
}

OID
hnsw_oid_encoder_default::decode_oid (const int64_t& id)
{
    return {.pageid = static_cast<uint32_t> (id >> 32), .slotid = static_cast<uint16_t> ((id >> 16) & 0xFFFF), .volid = static_cast<uint16_t> (id & 0xFFFF)};
}
