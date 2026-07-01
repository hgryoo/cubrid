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

#include "lockfree_transaction_table.hpp"

#include "lockfree_bitmap.hpp"
#include "lockfree_transaction_descriptor.hpp"
#include "lockfree_transaction_system.hpp"

#include <cassert>
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

namespace lockfree
{
  namespace tran
  {
    //
    // table
    //
    table::table (system &sys, bool epoch_mode)
      : m_sys (sys)
      , m_all (new descriptor[m_sys.get_max_transaction_count ()] ())
      , m_global_tranid { 0 }
      , m_min_active_tranid { 0 }
      , m_epoch_mode (epoch_mode)
    {
      for (size_t i = 0; i < m_sys.get_max_transaction_count (); i++)
	{
	  m_all[i].set_table (*this);
	}
    }

    table::~table ()
    {
      delete [] m_all;
    }

    descriptor &
    table::get_descriptor (const index &tran_index)
    {
      assert (tran_index <= m_sys.get_max_transaction_count ());
      return m_all[tran_index];
    }

    void
    table::start_tran (const index &tran_index)
    {
      get_descriptor (tran_index).start_tran ();
    }

    void
    table::end_tran (const index &tran_index)
    {
      get_descriptor (tran_index).end_tran ();
    }

    id
    table::get_new_global_tranid ()
    {
      if (m_epoch_mode)
	{
	  id current = m_global_tranid.load (std::memory_order_relaxed);
	  id min_active = m_min_active_tranid.load (std::memory_order_relaxed);
	  if (min_active >= current)
	    {
	      m_global_tranid.compare_exchange_strong (current, current + 1,
						       std::memory_order_release, std::memory_order_relaxed);
	    }
	  compute_min_active_tranid ();
	  return m_global_tranid.load (std::memory_order_relaxed);
	}

      id ret = ++m_global_tranid;
      if (ret % MATI_REFRESH_INTERVAL == 0)
	{
	  compute_min_active_tranid ();
	}
      return ret;
    }

    id
    table::get_current_global_tranid () const
    {
      return m_global_tranid;
    }

    void
    table::compute_min_active_tranid ()
    {
      id minvalue = INVALID_TRANID;
      const bitmap &bmp = m_sys.get_index_bitmap ();
      int word_count = (bmp.entry_count + LF_BITFIELD_WORD_SIZE - 1) / LF_BITFIELD_WORD_SIZE;
      for (int i = 0; i < word_count; i++)
	{
	  unsigned int word = bmp.bitfield[i].load (std::memory_order_relaxed);
	  if (word == 0)
	    {
	      continue;
	    }
	  for (int j = 0; j < LF_BITFIELD_WORD_SIZE; j++)
	    {
	      if (word & (1U << j))
		{
		  size_t pos = (size_t) i * LF_BITFIELD_WORD_SIZE + j;
		  if (pos >= m_sys.get_max_transaction_count ())
		    {
		      break;
		    }
		  id tranid = m_all[pos].get_transaction_id ();
		  if (minvalue > tranid)
		    {
		      minvalue = tranid;
		    }
		}
	    }
	}
      m_min_active_tranid.store (minvalue);
    }

    id
    table::get_min_active_tranid () const
    {
      return m_min_active_tranid;
    }

    size_t
    table::get_total_retire_count () const
    {
      size_t total = 0;
      for (size_t idx = 0; idx < m_sys.get_max_transaction_count (); idx++)
	{
	  total += m_all[idx].get_total_retire_count ();
	}
      return total;
    }

    size_t
    table::get_total_reclaim_count () const
    {
      size_t total = 0;
      for (size_t idx = 0; idx < m_sys.get_max_transaction_count (); idx++)
	{
	  total += m_all[idx].get_total_reclaim_count ();
	}
      return total;
    }

    size_t
    table::get_current_retire_count () const
    {
      size_t total = 0;
      for (size_t idx = 0; idx < m_sys.get_max_transaction_count (); idx++)
	{
	  total += m_all[idx].get_current_retire_count ();
	}
      return total;
    }
  } // namespace tran
} // namespace lockfree
