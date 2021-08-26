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

#include "method_callback.hpp"

#include "method_def.hpp"

namespace cubmethod
{
  callback_handler::callback_handler (int max_query_handler)
  {
    m_query_handlers.resize (max_query_handler, nullptr);
  }

  cubmem::block
  callback_handler::callback_dispatch (packing_unpacker &unpacker)
  {
    UINT64 id;
    unpacker.unpack_bigint (id);

    int code;
    unpacker.unpack_int (code);

    cubmem::block response;
    switch (code)
      {
      case METHOD_CALLBACK_QUERY_PREPARE:
	response = std::move (prepare (unpacker));
	break;
      case METHOD_CALLBACK_QUERY_EXECUTE:
	break;
      default:
	assert (false);
	break;
      }
    return response;
  }

  cubmem::block
  callback_handler::prepare (packing_unpacker &unpacker)
  {
    std::string sql;
    int flag;

    unpacker.unpack_string (sql);
    unpacker.unpack_int (flag);

    int handle_id = new_query_handler ();
    if (handle_id < 0)
      {
	// TODO
	// error handling
      }

    query_handler *handler = find_query_handler (handle_id);

    prepare_info info = handler->prepare (sql, flag);
    info.handle_id = handle_id;

    packing_packer packer;
    cubmem::extensible_block eb;
    packer.set_buffer_and_pack_all (eb, info);

    cubmem::block b (packer.get_current_size(), eb.release_ptr());
    return b;
  }

  int
  callback_handler::new_query_handler ()
  {
    int idx = 0;
    int handler_size = m_query_handlers.size();
    for (; idx < handler_size; idx)
      {
	if (m_query_handlers[idx] == nullptr)
	  {
	    break;
	  }
      }

    m_query_handlers[idx] = new query_handler (m_error_ctx, idx);
    if (m_query_handlers[idx] == nullptr)
      {
	return -1;
      }
    return idx;
  }

  query_handler *
  callback_handler::find_query_handler (int id)
  {
    if (id < 0 || id >= m_query_handlers.size())
      {
	return nullptr;
      }

    return m_query_handlers[id];
  }

  void
  callback_handler::free_query_handle (int id)
  {
    if (id < 0 || id >= m_query_handlers.size())
      {
	return;
      }

    if (m_query_handlers[id] != nullptr)
      {
	delete m_query_handlers[id];
	m_query_handlers[id] = nullptr;
      }
  }

  void
  callback_handler::free_query_handle_all ()
  {
    for (int i = 0; i < m_query_handlers.size(); i++)
      {
	if (m_query_handlers[i] != nullptr)
	  {
	    delete m_query_handlers[i];
	    m_query_handlers[i] = nullptr;
	  }
      }
  }
}