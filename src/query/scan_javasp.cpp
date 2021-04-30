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

#include "list_file.h"

#include "db_date.h"
#include "numeric_opfunc.h"
#include "set_object.h"
#include "language_support.h"

#include "query_opfunc.h"

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

    scanner::scanner ()
      : m_num_args (0),
	m_method_ctl (nullptr),
	m_thread_p (nullptr),
	m_sock_fd (INVALID_SOCKET) {}

//////////////////////////////////////////////////////////////////////////
// main scanner routine implementation
//////////////////////////////////////////////////////////////////////////

    void
    scanner::init (cubthread::entry *thread_p, qfile_list_id *list_id, method_sig_list *meth_sig_list)
    {
      m_thread_p = thread_p;
      m_method_ctl = &m_scan_buf.s.method_ctl;
      m_method_ctl->list_id = list_id;
      m_method_ctl->method_sig_list = meth_sig_list;
    }

    int
    scanner::open ()
    {
	  connect ();
      return open_value_array ();
    }



    SCAN_CODE scanner::next_scan (val_list_node &vl)
    {
      METHOD_INFO *method_ctl = &m_scan_buf.s.method_ctl;
      METHOD_SIG_LIST *method_sig_list = method_ctl->method_sig_list;
      METHOD_SIG *method_sig = method_sig_list->method_sig;
      qproc_db_value_list *dbval_list = m_scan_buf.dbval_list;

      qfile_list_id *list_id = method_ctl->list_id;
      std::vector<DB_VALUE> arg_vals;

      vl.val_cnt = method_sig_list->num_methods;
      method_sig = method_sig_list->method_sig;
      int i = 0;
      while (method_sig)
	{
	  int error = get_single_tuple_from_list_id (method_sig, list_id, arg_vals);
	  if (error != NO_ERROR)
	    {
	      //TODO
	      assert (false);
	    }
	  error = request (method_sig, arg_vals);
	  if (error != NO_ERROR)
	    {
	      //TODO
	      assert (false);
	    }

	  arg_vals.clear ();

	  DB_VALUE *dbval_p = (DB_VALUE *) db_private_alloc (m_thread_p, sizeof (DB_VALUE));
	  db_make_null (dbval_p);
	  error = receive (method_sig, dbval_p);
	  if (error != NO_ERROR)
	    {
	      //TODO
	      assert (false);
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
      //disconnect ();
      return close_value_array ();
    }

    int
    scanner::get_single_tuple_from_list_id (METHOD_SIG *sig, qfile_list_id *list_id, std::vector<DB_VALUE> &arg_vals)
    {
      QFILE_LIST_SCAN_ID scan_id;
      QFILE_TUPLE_RECORD tuple_record = { NULL, 0 };
      SCAN_CODE qp_scan;

      TP_DOMAIN *domain;
      PR_TYPE *pr_type;
      OR_BUF buf;
      QFILE_TUPLE_VALUE_FLAG flag;

      int error_code = NO_ERROR;
      int tuple_count = list_id->tuple_cnt;

      DB_VALUE v;
      char *ptr;
      int length;

      std::vector<DB_VALUE> vec;
      if (tuple_count == 1)
	{
	  error_code = qfile_open_list_scan (list_id, &scan_id);
	  if (error_code != NO_ERROR)
	    {
	      return error_code;
	    }

	  if (qfile_scan_list_next (m_thread_p, &scan_id, &tuple_record, PEEK) != S_SUCCESS)
	    {
	      qfile_close_scan (m_thread_p, &scan_id);
	      return ER_FAILED;
	    }

	  for (int i = 0; i < list_id->type_list.type_cnt; i++)
	    {
	      domain = list_id->type_list.domp[i];
	      if (domain == NULL || domain->type == NULL)
		{
		  qfile_close_scan (m_thread_p, &scan_id);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_QRY_SINGLE_TUPLE, 0);
		  return ER_QPROC_INVALID_QRY_SINGLE_TUPLE;
		}

	      db_make_null (&v);
	      if (db_value_domain_init (&v, TP_DOMAIN_TYPE (domain), domain->precision, domain->scale) !=
		  NO_ERROR)
		{
		  qfile_close_scan (m_thread_p, &scan_id);
		  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_QRY_SINGLE_TUPLE, 0);
		  return ER_QPROC_INVALID_QRY_SINGLE_TUPLE;
		}

	      pr_type = domain->type;
	      if (pr_type == NULL)
		{
		  qfile_close_scan (m_thread_p, &scan_id);
		  return ER_FAILED;
		}

	      flag = (QFILE_TUPLE_VALUE_FLAG) qfile_locate_tuple_value (tuple_record.tpl, i, &ptr, &length);
	      OR_BUF_INIT (buf, ptr, length);
	      if (flag == V_BOUND)
		{
		  if (pr_type->data_readval (&buf, &v, domain, -1, true, NULL, 0) != NO_ERROR)
		    {
		      qfile_close_scan (m_thread_p, &scan_id);
		      return ER_FAILED;
		    }
		}
	      else
		{
		  /* If value is NULL, properly initialize the result */
		  db_value_domain_init (&v, pr_type->id, DB_DEFAULT_PRECISION, DB_DEFAULT_SCALE);
		}
	      vec.push_back (v);
	    }

	  int num_args = sig->num_method_args;
	  arg_vals.resize (num_args);
	  for (int i = 0; i < num_args; i++)
	    {
	      int pos = sig->method_arg_pos[i];
	      db_value_clone (&vec[pos], &arg_vals[i]);
	    }

	  qfile_close_scan (m_thread_p, &scan_id);
	}
      else
	{
	  assert (false);
	}
      return error_code;
    }

//////////////////////////////////////////////////////////////////////////
// communication with Java SP Server routine implementation
//////////////////////////////////////////////////////////////////////////

    bool
    scanner::connect ()
    {
      if (!is_connected)
	{
	  int server_port = jsp_server_port ();
	  m_sock_fd = jsp_connect_server (server_port);
	  is_connected = !IS_INVALID_SOCKET (m_sock_fd);
	}
      return is_connected;
    }

    bool
    scanner::disconnect ()
    {
      if (is_connected)
	{
	  jsp_disconnect_server (m_sock_fd);
	  is_connected = false;
	}
      return IS_INVALID_SOCKET (m_sock_fd);
    }

    int
    scanner::request (METHOD_SIG *&method_sig, std::vector<DB_VALUE> &arg_vals)
    {
      int strlen = 0;
      size_t req_size = get_request_size (method_sig, strlen, arg_vals);

      packing_packer packer;
      cubmem::extensible_block ext_blk;

      ext_blk.extend_to (req_size);
      packer.set_buffer (ext_blk.get_ptr (), ext_blk.get_size ());
      pack_arg (packer, method_sig, arg_vals, strlen);

      assert (ext_blk.get_size() == req_size);

      size_t nbytes = jsp_writen (m_sock_fd, ext_blk.get_read_ptr (), ext_blk.get_size());
      if (nbytes != req_size)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_SP_NETWORK_ERROR, 1, nbytes);
	  return er_errid ();
	}
	else
	{
      return NO_ERROR;
	}
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
	      int type;
	      packing_unpacker unpacker;
	      unpacker.set_buffer (ext_blk.get_ptr (), (size_t) res_size);
	      unpack_result (unpacker, v);

	      //db_make_int (v, res);

	      // jsp_unpack_value (buffer, sp_args->returnval);
	      for (int i = 0; i < method_sig->num_method_args; i++)
		{
		  if (method_sig->arg_mode[i] == 1)
		    {
		      continue;
		    }
		}
	      // TODO: Process OUT / INOUT Parameter
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
	scanner::get_arg_size (METHOD_SIG *&method_sig, std::vector<DB_VALUE> &arg_vals)
	{
	  size_t size = 0;
	  for (int i = 0; i < arg_vals.size(); i++)
	  {
		  size += get_arg_value_size (arg_vals[i]);
	  }
	  return size;
	}

    size_t
    scanner::get_arg_value_size (DB_VALUE &val)
    {
      size_t size = 0;

      char str_buf[NUMERIC_MAX_STRING_SIZE];
      DB_TYPE type = DB_VALUE_TYPE (&val);
      switch (type)
	{
	case DB_TYPE_INTEGER:
	case DB_TYPE_SHORT:
	  size = sizeof (int);
	  break;

	case DB_TYPE_BIGINT:
	  size = sizeof (DB_BIGINT);
	  break;

	case DB_TYPE_FLOAT:
	  size = sizeof (float);	/* need machine independent code */
	  break;

	case DB_TYPE_DOUBLE:
	case DB_TYPE_MONETARY:
	  size = sizeof (double);	/* need machine independent code */
	  break;

	case DB_TYPE_NUMERIC:
	  size = or_packed_string_length (numeric_db_value_print (&val, str_buf), NULL);
	  break;

	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	case DB_TYPE_STRING:
	  size = or_packed_string_length (db_get_string (&val), NULL);
	  break;

	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	  break;

	case DB_TYPE_OBJECT:
	case DB_TYPE_DATE:
	case DB_TYPE_TIME:
	  size = sizeof (int) * 3;
	  break;

	case DB_TYPE_TIMESTAMP:
	  size = sizeof (int) * 6;
	  break;

	case DB_TYPE_DATETIME:
	  size = sizeof (int) * 7;
	  break;

	case DB_TYPE_SET:
	case DB_TYPE_MULTISET:
	case DB_TYPE_SEQUENCE:
	{
	  DB_SET *set;
	  int ncol, i;
	  DB_VALUE v;

	  set = db_get_set (&val);
	  ncol = set_size (set);
	  size += 4;		/* set size */

	  for (i = 0; i < ncol; i++)
	    {
	      if (set_get_element (set, i, &v) != NO_ERROR)
		{
		  return 0;
		}

	      size += get_arg_value_size (v);
	      pr_clear_value (&v);
	    }
	}
	break;

	case DB_TYPE_NULL:
	default:
	  break;
	}

      size += 16;			/* type + value's size + mode + arg_data_type */

      return size;
    }

    size_t
    scanner::get_request_size (METHOD_SIG *&method_sig, int &strlen, std::vector<DB_VALUE> &arg_vals)
    {
      size_t req_size = sizeof (int) * 4
			+ or_packed_string_length (method_sig->signature, &strlen)
			+ get_arg_size (method_sig, arg_vals);
      return req_size;
    }

    void
    scanner::pack_arg (cubpacking::packer &serializator, METHOD_SIG *&method_sig, std::vector<DB_VALUE> &arg_vals,
		       int &strlen)
    {
      serializator.pack_int (SP_CODE_INVOKE);
      serializator.pack_c_string (method_sig->signature, strlen);

      int arg_count = get_argument_count (method_sig);
      serializator.pack_int (arg_count);

      for (int i = 0; i < method_sig->num_method_args; i++)
	{
	  serializator.pack_int (method_sig->arg_mode[i]);
	  serializator.pack_int (method_sig->arg_type[i]);
	  pack_arg_value (serializator, arg_vals[i]);
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
	  serializator.pack_int (sizeof (int));
	  serializator.pack_int (db_get_int (&v));
	  break;

	case DB_TYPE_BIGINT:
	  serializator.pack_int (sizeof (DB_BIGINT));
	  serializator.pack_bigint (db_get_bigint (&v));
	  break;

	case DB_TYPE_SHORT:
	  serializator.pack_int (sizeof (int));
	  serializator.pack_short (db_get_short (&v));
	  break;

	case DB_TYPE_FLOAT:
	  serializator.pack_int (sizeof (float));
	  serializator.pack_float (db_get_float (&v));
	  break;

	case DB_TYPE_DOUBLE:
	  serializator.pack_int (sizeof (double));
	  serializator.pack_double (db_get_double (&v));
	  break;

	case DB_TYPE_NUMERIC:
	{
	  char str_buf[NUMERIC_MAX_STRING_SIZE];
	  numeric_db_value_print (&v, str_buf);
	  serializator.pack_int (strlen (str_buf));
	  serializator.pack_c_string (str_buf, strlen (str_buf));
	  break;

	  case DB_TYPE_CHAR:
	  case DB_TYPE_NCHAR:
	  case DB_TYPE_VARNCHAR:
	  case DB_TYPE_STRING:
	    // TODO: support unicode decomposed string
	    serializator.pack_string (db_get_string (&v));
	    break;

	  case DB_TYPE_BIT:
	  case DB_TYPE_VARBIT:
	    // NOTE: This type was not implemented at the previous version
	    break;

	  case DB_TYPE_DATE:
	  {
	    int year, month, day;
	    db_date_decode (db_get_date (&v), &month, &day, &year);
	    serializator.pack_int (sizeof (int) * 3);
	    serializator.pack_int (year);
	    serializator.pack_int (month - 1);
	    serializator.pack_int (day);
	  }
	  break;

	  case DB_TYPE_TIME:
	  {
	    int hour, min, sec;
	    db_time_decode (db_get_time (&v), &hour, &min, &sec);
	    serializator.pack_int (sizeof (int) * 3);
	    serializator.pack_int (hour);
	    serializator.pack_int (min);
	    serializator.pack_int (sec);
	  }
	  break;

	  case DB_TYPE_TIMESTAMP:
	  {
	    int year, month, day, hour, min, sec;
	    DB_TIMESTAMP *timestamp = db_get_timestamp (&v);
	    DB_DATE date;
	    DB_TIME time;
	    (void) db_timestamp_decode_ses (timestamp, &date, &time);
	    db_date_decode (&date, &month, &day, &year);
	    db_time_decode (&time, &hour, &min, &sec);

	    serializator.pack_int (sizeof (int) * 6);
	    serializator.pack_int (year);
	    serializator.pack_int (month - 1);
	    serializator.pack_int (day);
	    serializator.pack_int (hour);
	    serializator.pack_int (min);
	    serializator.pack_int (sec);
	  }
	  break;

	  case DB_TYPE_DATETIME:
	  {
	    int year, month, day, hour, min, sec, msec;
	    DB_DATETIME *datetime = db_get_datetime (&v);
	    db_datetime_decode (datetime, &month, &day, &year, &hour, &min, &sec, &msec);

	    serializator.pack_int (sizeof (int) * 7);
	    serializator.pack_int (year);
	    serializator.pack_int (month - 1);
	    serializator.pack_int (day);
	    serializator.pack_int (hour);
	    serializator.pack_int (min);
	    serializator.pack_int (sec);
	    serializator.pack_int (msec);
	  }
	  break;

	  case DB_TYPE_SET:
	  case DB_TYPE_MULTISET:
	  case DB_TYPE_SEQUENCE:
	  {
	    DB_SET *set = db_get_set (&v);
	    int ncol = set_size (set);
	    DB_VALUE v;

	    serializator.pack_int (sizeof (int));
	    serializator.pack_int (ncol);
	    for (int i = 0; i < ncol; i++)
	      {
		if (set_get_element (set, i, &v) != NO_ERROR)
		  {
		    break;
		  }

		pack_arg_value (serializator, v);
		pr_clear_value (&v);
	      }
	  }
	  break;

	  case DB_TYPE_MONETARY:
	  {
	    DB_MONETARY *money = db_get_monetary (&v);
	    serializator.pack_int (sizeof (double));
	    serializator.pack_double (money->amount);
	  }
	  break;

	  case DB_TYPE_OBJECT:
	    assert (false);
	    break;

	  case DB_TYPE_NULL:
	    serializator.pack_int (0);
	    break;

	  default:
	    assert (false);
	    break;
	  }
	}
    }

    void
    scanner::unpack_result (cubpacking::unpacker &deserializator, DB_VALUE *retval)
    {
      int type;
      deserializator.unpack_int (type);

      switch (type)
	{
	case DB_TYPE_INTEGER:
	{
	  int val;
	  deserializator.unpack_int (val);
	  db_make_int (retval, val);
	}
	break;

	case DB_TYPE_BIGINT:
	{
	  DB_BIGINT val;
	  deserializator.unpack_bigint (val);
	  db_make_bigint (retval, val);
	}
	break;

	case DB_TYPE_SHORT:
	{
	  short val;
	  deserializator.unpack_short (val);
	  db_make_short (retval, val);
	}
	break;

	case DB_TYPE_FLOAT:
	{
	  float val;
	  deserializator.unpack_float (val);
	  db_make_float (retval, val);
	}
	break;

	case DB_TYPE_DOUBLE:
	{
	  double val;
	  deserializator.unpack_double (val);
	  db_make_double (retval, val);
	}
	break;

	case DB_TYPE_NUMERIC:
	{
	  std::string val;
	  deserializator.unpack_string (val);
	  numeric_coerce_string_to_num (val.data (), val.length (), LANG_SYS_CODESET, retval);
	}
	break;

	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	case DB_TYPE_STRING:
	  // TODO: support unicode composed string
	{
	  std::string val;
	  deserializator.unpack_string (val);
	  db_make_string (retval, val.data());
	  db_string_put_cs_and_collation (retval, LANG_SYS_CODESET, LANG_SYS_COLLATION);
	  retval->need_clear = true;
	}
	break;

	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	  break;

	case DB_TYPE_DATE:
	  assert (false);
	  break;
	/* describe_data(); */

	case DB_TYPE_TIME:
	  assert (false);
	  break;

	case DB_TYPE_TIMESTAMP:
	  assert (false);
	  break;

	case DB_TYPE_DATETIME:
	  assert (false);
	  break;

	case DB_TYPE_SET:
	case DB_TYPE_MULTISET:
	case DB_TYPE_SEQUENCE:
	  assert (false);
	  break;

	case DB_TYPE_OBJECT:
	  assert (false);
	  break;

	case DB_TYPE_MONETARY:
	  assert (false);
	  break;

	case DB_TYPE_RESULTSET:
	  assert (false);
	  break;

	case DB_TYPE_NULL:
	default:
	  db_make_null (retval);
	  break;
	}
    }

//////////////////////////////////////////////////////////////////////////
// Value array scanning implementation
//////////////////////////////////////////////////////////////////////////

    int
    scanner::open_value_array ()
    {
      METHOD_INFO *method_ctl = &m_scan_buf.s.method_ctl;
      METHOD_SIG_LIST *method_sig_list = method_ctl->method_sig_list;

      int num_methods = method_sig_list->num_methods;
      if (num_methods <= 0)
	{
	  num_methods = MAX_XS_SCANBUF_DBVALS;	/* for safe-guard */
	}

      m_scan_buf.dbval_list = (struct qproc_db_value_list *) db_private_alloc (m_thread_p,
			      sizeof (struct qproc_db_value_list) * num_methods);
      if (m_scan_buf.dbval_list == NULL)
	{
	  return ER_FAILED;
	}

	  METHOD_SIG *method_sig = method_sig_list->method_sig;
	  while (method_sig)
	    {
	      m_num_args += method_sig->num_method_args + 1;
	      method_sig = method_sig->next;
	    }

	  qfile_list_id *list_id = m_method_ctl->list_id;
	  if (list_id->type_list.type_cnt > m_num_args)
	    {
	      m_num_args = list_id->type_list.type_cnt;
	    }

      return NO_ERROR;
    }

    int
    scanner::close_value_array ()
    {
	  /*
      struct qproc_db_value_list *val_list = m_scan_buf.dbval_list;
      while (val_list)
	{
	  db_private_free_and_init (m_thread_p, val_list->val);
	  val_list = val_list->next;
	}
	*/
      db_private_free_and_init (m_thread_p, m_scan_buf.dbval_list);
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