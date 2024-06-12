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
 * sp_executor.hpp: managing subprograms of a server task
 */

#ifndef _SP_GROUP_HPP_
#define _SP_GROUP_HPP_

#if !defined (SERVER_MODE) && !defined (SA_MODE)
#error Belongs to server module
#endif /* !defined (SERVER_MODE) && !defined (SA_MODE) */

#include "dbtype_def.h"
#include "query_list.h"
#include "query_executor.h"
#include "xasl_sp.hpp"
#include "mem_block.hpp"

#include "method_connection_pool.hpp"

// thread_entry.hpp
namespace cubthread
{
  class entry;
}

namespace cubsp
{
  class sp_executor
  {
    public:
      sp_executor () = delete; // Not DefaultConstructible
      sp_executor (cubthread::entry *thread_p, const cubxasl::sp_node &sp_node);

      sp_executor (sp_executor &&other) = delete; // Not MoveConstructible
      sp_executor (const sp_executor &copy) = delete; // Not CopyConstructible

      sp_executor &operator= (sp_executor &&other) = delete; // Not MoveAssignable
      sp_executor &operator= (const sp_executor &copy) = delete; // Not CopyAssignable

      ~sp_executor () = default;

      cubthread::entry *get_thread_entry () const
      {
	return m_thread_p;
      }
      SESSION_ID get_session_id () const
      {
	return m_sid;
      }
      TRANID get_tran_id () const
      {
	return m_tid;
      }

      int execute (VAL_DESCR *val_desc_p, OID *obj_oid_p, QFILE_TUPLE tuple);

      inline int get_and_increment_request_id ()
      {
	// TODO
	return m_req_id++;
      }

      SOCKET
      get_socket () const
      {
	return m_connection ? m_connection->get_socket () : INVALID_SOCKET;
      }

      std::queue<cubmem::block> &
      get_data_queue ()
      {
	return m_data_queue;
      }

    private:
      
      int on_receive ();

      std::uint64_t m_id;
      SESSION_ID m_sid; // session ID
      TRANID m_tid;

      int m_req_id;

      cubthread::entry *m_thread_p;
      const cubxasl::sp_node &m_sp_node;

      cubmethod::connection *m_connection;
      std::queue<cubmem::block> m_data_queue;
  };
}

#endif //_SP_GROUP_HPP_
