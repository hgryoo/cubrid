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

#include "scan_javasp.hpp"

#include "dbtype.h"
#include "xasl.h"
#include "object_representation.h"
#include "packer.hpp"

#if !defined(WINDOWS)
#include <sys/socket.h>
#else /* not WINDOWS */
#include <winsock2.h>
#endif /* not WINDOWS */

#include <vector>

namespace cubscan
{
  namespace javasp
  {

//////////////////////////////////////////////////////////////////////////
// main scanner routine implementation
//////////////////////////////////////////////////////////////////////////

    void
    scanner::init (cubthread::entry *thread_p, qfile_list_id *list_id, method_sig_list *meth_sig_list)
    {
      m_sock_fd = INVALID_SOCKET;
      m_thread_p = thread_p;
      METHOD_INFO *method_ctl_p = &m_scan_buf.s.method_ctl;
      method_ctl_p->list_id = list_id;
      method_ctl_p->method_sig_list = meth_sig_list;
    }

    int
    scanner::open ()
    {
      if (IS_INVALID_SOCKET (m_sock_fd))
	{
	  connect ();
	}
      return open_value_array ();
    }

    SCAN_CODE scanner::next_scan (val_list_node &vl)
    {
      METHOD_INFO *method_ctl = &m_scan_buf.s.method_ctl;
      METHOD_SIG_LIST *method_sig_list = method_ctl->method_sig_list;
      METHOD_SIG *method_sig = method_sig_list->method_sig;
      qproc_db_value_list *dbval_list = m_scan_buf.dbval_list;

      vl.val_cnt = method_sig_list->num_methods;
      int i = 0;
      while (method_sig)
	{
	  int error = request (method_sig);
	  if (error != NO_ERROR)
	    {
	      //TODO
	      fprintf (stdout, "error");
	    }

	  DB_VALUE *dbval_p = (DB_VALUE *) malloc (sizeof (DB_VALUE));
	  db_make_null (dbval_p);
	  error = receive (method_sig, dbval_p);
	  if (error != NO_ERROR)
	    {
	      //TODO
	      fprintf (stdout, "error");
	    }
	  else
	    {
	      m_scan_buf.dbval_list[i].val = dbval_p;
	    }
	  m_scan_buf.dbval_list[i].next = NULL;
	  method_sig = method_sig->next;
	  i++;
	}

      next_value_array (vl);

      return S_SUCCESS;
    }

    int
    scanner::close ()
    {
      disconnect ();
      return close_value_array ();
    }

//////////////////////////////////////////////////////////////////////////
// communication with Java SP Server routine implementation
//////////////////////////////////////////////////////////////////////////

    bool
    scanner::connect ()
    {
      if (IS_INVALID_SOCKET (m_sock_fd))
	{
	  int server_port = jsp_server_port ();
	  m_sock_fd = jsp_connect_server (server_port);
	}
      return !IS_INVALID_SOCKET (m_sock_fd);
    }

    bool
    scanner::disconnect ()
    {
      if (!IS_INVALID_SOCKET (m_sock_fd))
	{
	  jsp_disconnect_server (m_sock_fd);
	}
      return IS_INVALID_SOCKET (m_sock_fd);
    }

    int
    scanner::request (METHOD_SIG *&method_sig)
    {
      int strlen = 0;
      size_t req_size = get_request_size (method_sig, strlen);

      packing_packer packer;
      cubmem::extensible_block ext_blk;

      ext_blk.extend_to (req_size);
      packer.set_buffer (ext_blk.get_ptr(), req_size);
      pack_arg (packer, method_sig, strlen);

      assert (ext_blk.get_size() == req_size);
      
      size_t nbytes = jsp_writen (m_sock_fd, ext_blk.get_read_ptr (), ext_blk.get_size());
      if (nbytes != req_size)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	  return er_errid ();
	}

      return NO_ERROR;
    }

    int
    scanner::receive (METHOD_SIG *&method_sig, DB_VALUE *v)
    {
      size_t nbytes;
      int start_code = -1, end_code = -1;
      char *buffer = NULL, *ptr = NULL;
      int error_code = NO_ERROR;

      /* read request code */
      nbytes = jsp_readn (m_sock_fd, (char *) &start_code, (int) sizeof (int));
      if (nbytes != (int) sizeof (int))
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	  return ER_SP_NETWORK_ERROR;
	}
      start_code = ntohl (start_code);

      if (start_code == SP_CODE_INTERNAL_JDBC)
	{
	  // TODO
	  assert (false);
	  /*
	  tran_begin_libcas_function ();
	  error_code = libcas_main (sockfd);
	  tran_end_libcas_function ();
	  if (error_code != NO_ERROR)
	  {
	  goto exit;
	  }
	  goto redo;
	  */
	}

      if (start_code == SP_CODE_RESULT || start_code == SP_CODE_ERROR)
	{
	  /* read size of buffer to allocate and data */
	  int res_size;
	  int nbytes = jsp_readn (m_sock_fd, (char *) &res_size, (int) sizeof (int));
	  if (nbytes != (int) sizeof (int))
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	      return ER_SP_NETWORK_ERROR;
	    }
	  res_size = ntohl (res_size);

	  /* alloc response */
	  cubmem::extensible_block ext_blk;
	  ext_blk.extend_to (res_size);

	  if (ext_blk.get_ptr () == NULL)
	    {
	      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OUT_OF_VIRTUAL_MEMORY, 1, (size_t) res_size);
	      return ER_OUT_OF_VIRTUAL_MEMORY;
	    }

    nbytes = jsp_readn (m_sock_fd, ext_blk.get_ptr (), res_size);
	      if (nbytes != res_size)
		{
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
		  return ER_SP_NETWORK_ERROR;
		}
	  packing_unpacker unpacker;
	  unpacker.set_buffer (ext_blk.get_ptr (), (size_t) res_size);

	  switch (start_code)
	    {
	    case SP_CODE_RESULT:
	    {
	      int type, res;
	      packing_unpacker unpacker;
	      unpacker.set_buffer (ext_blk.get_ptr (), (size_t) res_size);

	      unpacker.unpack_int (type);
	      unpacker.unpack_int (res);
	      db_make_int (v, res);

	      // jsp_unpack_value (buffer, sp_args->returnval);
	      for (int i = 0; i < method_sig->num_method_args; i++)
		{
		  if (method_sig->arg_mode[i] == 1)
		    {
		      continue;
		    }
		}
	      // process TODO OUT / INOUT Parameter
	    }
	    break;
	    case SP_CODE_ERROR:
        db_make_null (v);

	      // assert (false);
	      // error_code = jsp_receive_error (buffer, ptr, sp_args);
	      break;
	    }
	}

      return error_code;
    }

    size_t
    scanner::get_request_size (METHOD_SIG *&method_sig, int &strlen)
    {
      size_t req_size = sizeof (int) * 4 + or_packed_string_length (method_sig->method_name,
			&strlen) + get_argument_size (method_sig);
      return req_size;
    }

    void
    scanner::pack_arg (cubpacking::packer &serializator, METHOD_SIG *&method_sig, int &strlen)
    {
      serializator.pack_int (SP_CODE_INVOKE);
      serializator.pack_int (strlen);
      serializator.pack_c_string (method_sig->method_name, strlen);

      int arg_count = get_argument_count (method_sig);
      serializator.pack_int (arg_count);

      DB_VALUE val;
      db_make_int (&val, 1);
      for (int i = 0; i < method_sig->num_method_args; i++)
	{
	  serializator.pack_int (method_sig->arg_mode[i]);
	  serializator.pack_int (method_sig->arg_type[i]);
	  pack_arg_value (serializator, val);
	}

      serializator.pack_int (method_sig->result_type);
      serializator.pack_int (SP_CODE_INVOKE);
    }

    void
    scanner::pack_arg_value (cubpacking::packer &serializator, DB_VALUE &v)
    {
      int param_type = db_value_type (&v);
      serializator.pack_int (param_type);

      switch (param_type)
	{
	case DB_TYPE_INTEGER:
	{
	  serializator.pack_int (sizeof (int));
	  serializator.pack_int (db_get_int (&v));
	}
	break;

	default:
	  assert (false);
	  break;
	}
    }

//////////////////////////////////////////////////////////////////////////
// Value array scanning implementation
//////////////////////////////////////////////////////////////////////////

    int
    scanner::open_value_array ()
    {
      int num_methods = m_scan_buf.s.method_ctl.method_sig_list->num_methods;
      if (num_methods <= 0)
	{
	  num_methods = MAX_XS_SCANBUF_DBVALS;	/* for safe-guard */
	}

      m_scan_buf.dbval_list = (struct qproc_db_value_list *) malloc (sizeof (struct qproc_db_value_list) * num_methods);

      if (m_scan_buf.dbval_list == NULL)
	{
	  return ER_FAILED;
	}

#ifdef SERVER_MODE

      /*
      //socket = INVALID_SOCKET;

      scan_buffer_p->vacomm_buffer = method_initialize_vacomm_buffer ();
      if (scan_buffer_p->vacomm_buffer == NULL)
          {
          return ER_FAILED;
          }
      */
#endif /* SERVER_MODE */

      return NO_ERROR;
    }

    int
    scanner::close_value_array ()
    {
      free_and_init (m_scan_buf.dbval_list);
      return NO_ERROR;
    }

    SCAN_CODE scanner::next_value_array (val_list_node &vl)
    {
      SCAN_CODE scan_result = S_SUCCESS;
      struct qproc_db_value_list *dbval_list;
      int n;

      dbval_list = m_scan_buf.dbval_list;
      for (n = 0; n < vl.val_cnt; n++)
	{
	  dbval_list->next = dbval_list + 1;
	  dbval_list++;
	}

      m_scan_buf.dbval_list[vl.val_cnt - 1].next = NULL;
      vl.valp = m_scan_buf.dbval_list;

      return scan_result;
    }

  }
}