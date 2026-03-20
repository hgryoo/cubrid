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

#include "test_lock_manager_mock_runtime.hpp"

#if defined (TEST_LOCK_MANAGER_WITH_CUBRID_API)
#include "lock_manager.h"
#include "thread_entry.hpp"
#include "thread_manager.hpp"
#include "oid.h"
#endif

#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace test_lock_manager
{
#if defined (TEST_LOCK_MANAGER_WITH_CUBRID_API)
  namespace
  {
    class real_thread_entry
    {
      public:
	explicit real_thread_entry (int tran_index)
	  : m_thread_entry ()
	{
	  m_thread_entry.tran_index = tran_index;
	}

	void activate (void)
	{
	  cubthread::set_thread_local_entry (m_thread_entry);
	}

	THREAD_ENTRY *get (void)
	{
	  return &m_thread_entry;
	}

      private:
	THREAD_ENTRY m_thread_entry;
    };

    OID
    make_real_oid (const mock_oid &mock)
    {
      OID oid;
      oid.volid = mock.volid;
      oid.pageid = mock.pageid;
      oid.slotid = mock.slotid;
      return oid;
    }

    LOCK
    to_lock (const std::string &lock_mode)
    {
      if (lock_mode == "S_LOCK")
        {
	  return S_LOCK;
        }
      if (lock_mode == "X_LOCK")
        {
	  return X_LOCK;
        }
      if (lock_mode == "IX_LOCK")
        {
	  return IX_LOCK;
        }
      if (lock_mode == "IS_LOCK")
        {
	  return IS_LOCK;
        }
      throw std::invalid_argument ("Unsupported lock mode: " + lock_mode);
    }
  } // namespace
#endif

  mock_runtime::mock_runtime ()
  {
  }

  std::string
  mock_runtime::make_resource_key (const operation &op) const
  {
    std::ostringstream out;
    out << op.class_oid.volid << ':' << op.class_oid.pageid << ':' << op.class_oid.slotid
        << '/' << op.oid.volid << ':' << op.oid.pageid << ':' << op.oid.slotid;
    return out.str ();
  }

  simulation_stats
  mock_runtime::simulate (const std::vector<operation> &operations)
  {
    simulation_stats stats { 0, 0, 0, 0, 0, 0, 0 };

#if defined (TEST_LOCK_MANAGER_WITH_CUBRID_API)
    LK_INIT_CONFIG config;
    std::map<int, real_thread_entry> entries;
    int error = NO_ERROR;

    lock_initialize_default_config (&config);
    config.start_deadlock_detector = false;
    config.initial_object_locks = 256;
    config.object_res_block_count = 2;
    config.object_entry_block_count = 1;

    error = lock_initialize_with_config (&config);
    if (error != NO_ERROR)
      {
	throw std::runtime_error ("lock_initialize_with_config failed");
      }

    for (const operation &op : operations)
      {
        if (entries.find (op.txn_id) == entries.end ())
          {
            entries.emplace (op.txn_id, real_thread_entry (op.txn_id + 1));
          }

        real_thread_entry &entry = entries.find (op.txn_id)->second;
        OID oid = make_real_oid (op.oid);
        OID class_oid = make_real_oid (op.class_oid);
        OID *class_oid_ptr = op.is_class_lock ? oid_Root_class_oid : &class_oid;
        LOCK lock = to_lock (op.lock_mode);

        entry.activate ();

        switch (op.kind)
          {
          case operation_kind::acquire:
            stats.acquire_attempts++;
            error = lock_object (entry.get (), &oid, class_oid_ptr, lock, LK_COND_LOCK);
            if (error == LK_GRANTED)
              {
                stats.acquire_grants++;
              }
            else
              {
                stats.acquire_conflicts++;
                if (lock == X_LOCK)
                  {
                    stats.deadlock_pairs++;
                  }
              }
            break;

          case operation_kind::convert:
            error = lock_object (entry.get (), &oid, class_oid_ptr, lock, LK_COND_LOCK);
            if (error == LK_GRANTED)
              {
                stats.conversions++;
              }
            else
              {
                stats.acquire_conflicts++;
                if (lock == X_LOCK)
                  {
                    stats.deadlock_pairs++;
                  }
              }
            break;

          case operation_kind::release:
            lock_unlock_object (entry.get (), &oid, class_oid_ptr, lock, false);
            stats.releases++;
            break;
          }

        if (op.is_class_lock && lock >= IX_LOCK)
          {
            stats.escalation_candidates++;
          }
      }

    for (std::pair<const int, real_thread_entry> &entry : entries)
      {
        entry.second.activate ();
        lock_unlock_all (entry.second.get ());
      }
    lock_finalize ();
#else
    std::map<std::string, int> owners;

    for (const operation &op : operations)
      {
        const std::string resource_key = make_resource_key (op);
        std::map<std::string, int>::iterator owner_it = owners.find (resource_key);

        switch (op.kind)
          {
          case operation_kind::acquire:
            stats.acquire_attempts++;
            if (owner_it == owners.end () || owner_it->second == op.txn_id)
              {
                owners[resource_key] = op.txn_id;
                stats.acquire_grants++;
              }
            else
              {
                stats.acquire_conflicts++;
                if (op.lock_mode == "X_LOCK")
                  {
                    stats.deadlock_pairs++;
                  }
              }
            break;

          case operation_kind::convert:
            stats.conversions++;
            break;

          case operation_kind::release:
            owners.erase (resource_key);
            stats.releases++;
            break;
          }

        if (op.is_class_lock)
          {
            stats.escalation_candidates++;
          }
      }
#endif

    return stats;
  }
} // namespace test_lock_manager
