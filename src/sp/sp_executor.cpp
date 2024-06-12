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

#include "sp_executor.hpp"

#include "dbtype.h"		/* db_value_* */
#include "fetch.h"
#include "session.h"
#include "log_impl.h" // logtb_find_current_tranid
#include "memory_alloc.h"

#include "jsp_comm.h"
#include "method_connection_sr.hpp"
#include "method_connection_java.hpp"
#include "method_connection_pool.hpp"
#include "method_struct_invoke.hpp"
#include "method_struct_value.hpp"

static cubmethod::connection_pool g_conn_pool (METHOD_MAX_RECURSION_DEPTH + 1);

namespace cubsp
{
  sp_executor::sp_executor (cubthread::entry *thread_p, const cubxasl::sp_node &sp_node)
    : m_id ((std::uint64_t) this)
    , m_thread_p (thread_p)
    , m_sp_node (sp_node)
  {
    session_get_session_id (thread_p, &m_sid);
    m_tid = logtb_find_current_tranid (thread_p);
    m_connection = g_conn_pool.claim();

    m_req_id = 0;
  }

  int
  sp_executor::execute (VAL_DESCR *val_desc_p, OID *obj_oid_p, QFILE_TUPLE tuple)
  {
    int error = NO_ERROR;
    int index = 0;
    REGU_VARIABLE_LIST operand;
    DB_VALUE **args = NULL;

    /* begin */
    {
      if (m_sp_node.arg_size > 0)
	{
	  DB_VALUE *value = NULL;
	  args = (DB_VALUE **) db_private_alloc (m_thread_p, sizeof (DB_VALUE *) * m_sp_node.arg_size);

	  operand = m_sp_node.args;
	  while (operand != NULL)
	    {
	      error = fetch_peek_dbval (m_thread_p, &operand->value, val_desc_p, NULL, obj_oid_p, tuple, &value);
	      if (error != NO_ERROR)
		{
		  goto exit;
		}

	      args[index++] = value;

	      operand = operand->next;
	    }
	}

      // push to runtime context's stack

      /* prepare */
      std::vector<std::reference_wrapper<DB_VALUE>> prepared_args;
      for (int i = 0; i < m_sp_node.arg_size; i++)
	{
	  prepared_args.push_back (std::ref (*args[i]));
	}

      cubmethod::header pheader (get_session_id(), SP_CODE_PREPARE_ARGS, get_and_increment_request_id ());
      cubmethod::prepare_args parg (m_id, get_tran_id (), METHOD_TYPE_JAVA_SP, prepared_args);
      error = mcon_send_data_to_java (get_socket (), pheader, parg);
      if (error != NO_ERROR)
	{
	  goto exit;
	}

      /* invoke */
      cubmethod::header iheader (get_session_id (), SP_CODE_INVOKE, get_and_increment_request_id ());
      cubmethod::invoke_java iarg ((METHOD_GROUP_ID) m_id, get_tran_id (), (cubxasl::sp_node *) &m_sp_node,
				   false);
      error = mcon_send_data_to_java (get_socket (), iheader, iarg);
      if (error != NO_ERROR)
	{
	  goto exit;
	}

      /* get_return */
      int start_code = NO_ERROR;
      do
	{
	  /* read request code */
	  cubmem::block response_blk;
	  int nbytes = -1;
	  error = cubmethod::mcon_read_data_from_java (get_socket(), response_blk);
	  if (error == NO_ERROR)
	    {
	      packing_unpacker unpacker (response_blk);
	      unpacker.unpack_int (start_code);

	      char *aligned_ptr = PTR_ALIGN (unpacker.get_curr_ptr(), MAX_ALIGNMENT);
	      cubmem::block payload_blk ((size_t) (unpacker.get_buffer_end() - aligned_ptr),
					 aligned_ptr);
	      get_data_queue().emplace (std::move (payload_blk));

	      /* processing */
	      /*
	      if (start_code == SP_CODE_INTERNAL_JDBC)
	        {
	      error_code = callback_dispatch (*thread_p);
	        }
	      else
	      */
	      if (start_code == SP_CODE_RESULT)
		{
		  // check queue
		  if (get_data_queue().empty() == true)
		    {
		      return ER_FAILED;
		    }

		  cubmem::block &blk = get_data_queue().front ();
		  packing_unpacker unpacker (blk);

		  cubmethod::dbvalue_java value_unpacker;
		  db_make_null (m_sp_node.value);
		  value_unpacker.value = m_sp_node.value;
		  value_unpacker.unpack (unpacker);

		  // m_sp_node.value = value_unpacker.value;

		  error = NO_ERROR;
		}
	      else if (start_code == SP_CODE_ERROR)
		{
		  db_make_string (m_sp_node.value, "error");
		  // nerror_code = receive_error ();
		  // db_make_null (&returnval);
		}
	      else
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1,
			  start_code);
		  error = ER_SP_NETWORK_ERROR;
		}

	      if (get_data_queue().empty() == false)
		{
		  get_data_queue().pop ();
		}
	    }

	  // free phase
	  if (response_blk.is_valid ())
	    {
	      delete [] response_blk.ptr;
	      response_blk.ptr = NULL;
	      response_blk.dim = 0;
	    }

	  if (error != NO_ERROR)
	    {
	      break;
	    }
	}
      while (error == NO_ERROR && start_code == SP_CODE_INTERNAL_JDBC);

      // error = invoke ();
    }
exit:
    if (args != NULL)
      {
	db_private_free_and_init (m_thread_p, args);
      }

    if (m_connection)
      {
	g_conn_pool.retire (m_connection, false);
	m_connection = nullptr;
      }

    return error;
  }
}