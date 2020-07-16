/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */


/*
 * cas_procedure.c -
 */

#include "cas_procedure.h"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "cas_db_inc.h"

#include "cas.h"
#include "cas_common.h"
#include "cas_network.h"
#include "cas_util.h"
#include "cas_schema_info.h"
#include "cas_log.h"
#include "cas_str_like.h"
#include "cas_handle.h"

#include "broker_filename.h"
#include "cas_sql_log2.h"

#include "tz_support.h"
#include "release_string.h"
#include "perf_monitor.h"
#include "intl_support.h"
#include "language_support.h"
#include "unicode_support.h"
#include "transaction_cl.h"
#include "authenticate.h"
#include "trigger_manager.h"
#include "system_parameter.h"
#include "schema_manager.h"
#include "object_representation.h"

#include "db_set_function.h"
#include "dbi.h"
#include "dbtype.h"
#include "memory_alloc.h"
#include "object_primitive.h"

#define CAS_TYPE_COLLECTION(DB_TYPE, SET_TYPE)		\
	(((DB_TYPE) == DB_TYPE_SET) ? (CAS_TYPE_SET(SET_TYPE)) : \
	(((DB_TYPE) == DB_TYPE_MULTISET) ? (CAS_TYPE_MULTISET(SET_TYPE)) : \
	(CAS_TYPE_SEQUENCE(SET_TYPE))))

#define CAS_TYPE_SET(TYPE)		((TYPE) | CCI_CODE_SET)
#define CAS_TYPE_MULTISET(TYPE)		((TYPE) | CCI_CODE_MULTISET)
#define CAS_TYPE_SEQUENCE(TYPE)		((TYPE) | CCI_CODE_SEQUENCE)


static int cursor_tuple (T_QUERY_RESULT * q_result, int max_col_size, char sensitive_flag, DB_OBJECT * tuple_obj, T_NET_BUF * net_buf);
static void dbobj_to_casobj (DB_OBJECT * obj, T_OBJECT * cas_obj);
static int dbval_to_net_buf (DB_VALUE * val, T_NET_BUF * net_buf, char fetch_flag, int max_col_size, char column_type_flag);
static void add_res_data_bytes (T_NET_BUF * net_buf, const char *str, int size, unsigned char ext_type, int *net_size);
static void add_res_data_string (T_NET_BUF * net_buf, const char *str, int size, unsigned char ext_type,
				 unsigned char charset, int *net_size);
static void add_res_data_string_safe (T_NET_BUF * net_buf, const char *str, unsigned char ext_type,
				      unsigned char charset, int *net_size);
static void add_res_data_int (T_NET_BUF * net_buf, int value, unsigned char ext_type, int *net_size);
static void add_res_data_bigint (T_NET_BUF * net_buf, DB_BIGINT value, unsigned char ext_type, int *net_size);
static void add_res_data_short (T_NET_BUF * net_buf, short value, unsigned char ext_type, int *net_size);
static void add_res_data_float (T_NET_BUF * net_buf, float value, unsigned char ext_type, int *net_size);
static void add_res_data_double (T_NET_BUF * net_buf, double value, unsigned char ext_type, int *net_size);
static void add_res_data_timestamp (T_NET_BUF * net_buf, short yr, short mon, short day, short hh, short mm, short ss,
				    unsigned char ext_type, int *net_size);
static void add_res_data_timestamptz (T_NET_BUF * net_buf, short yr, short mon, short day, short hh, short mm, short ss,
				      char *tz_str, unsigned char ext_type, int *net_size);
static void add_res_data_datetime (T_NET_BUF * net_buf, short yr, short mon, short day, short hh, short mm, short ss,
				   short ms, unsigned char ext_type, int *net_size);
static void add_res_data_datetimetz (T_NET_BUF * net_buf, short yr, short mon, short day, short hh, short mm, short ss,
				     short ms, char *tz_str, unsigned char ext_type, int *net_size);
static void add_res_data_time (T_NET_BUF * net_buf, short hh, short mm, short ss, unsigned char ext_type,
			       int *net_size);
static void add_res_data_date (T_NET_BUF * net_buf, short yr, short mon, short day, unsigned char ext_type,
			       int *net_size);
static void add_res_data_object (T_NET_BUF * net_buf, T_OBJECT * obj, unsigned char ext_type, int *net_size);
static void add_res_data_lob_handle (T_NET_BUF * net_buf, T_LOB_HANDLE * lob, unsigned char ext_type, int *net_size);
static unsigned char set_extended_cas_type (T_CCI_U_TYPE u_set_type, DB_TYPE db_type);
char get_set_domain (DB_DOMAIN * set_domain, int *precision, short *scale, char *db_type, char *charset);

static char cas_u_type[] = { 0,	/* 0 */
  CCI_U_TYPE_INT,		/* 1 */
  CCI_U_TYPE_FLOAT,		/* 2 */
  CCI_U_TYPE_DOUBLE,		/* 3 */
  CCI_U_TYPE_STRING,		/* 4 */
  CCI_U_TYPE_OBJECT,		/* 5 */
  CCI_U_TYPE_SET,		/* 6 */
  CCI_U_TYPE_MULTISET,		/* 7 */
  CCI_U_TYPE_SEQUENCE,		/* 8 */
  0,				/* 9 */
  CCI_U_TYPE_TIME,		/* 10 */
  CCI_U_TYPE_TIMESTAMP,		/* 11 */
  CCI_U_TYPE_DATE,		/* 12 */
  CCI_U_TYPE_MONETARY,		/* 13 */
  0, 0, 0, 0,			/* 14 - 17 */
  CCI_U_TYPE_SHORT,		/* 18 */
  0, 0, 0,			/* 19 - 21 */
  CCI_U_TYPE_NUMERIC,		/* 22 */
  CCI_U_TYPE_BIT,		/* 23 */
  CCI_U_TYPE_VARBIT,		/* 24 */
  CCI_U_TYPE_CHAR,		/* 25 */
  CCI_U_TYPE_NCHAR,		/* 26 */
  CCI_U_TYPE_VARNCHAR,		/* 27 */
  CCI_U_TYPE_RESULTSET,		/* 28 */
  0, 0,				/* 29 - 30 */
  CCI_U_TYPE_BIGINT,		/* 31 */
  CCI_U_TYPE_DATETIME,		/* 32 */
  CCI_U_TYPE_BLOB,		/* 33 */
  CCI_U_TYPE_CLOB,		/* 34 */
  CCI_U_TYPE_ENUM,		/* 35 */
  CCI_U_TYPE_TIMESTAMPTZ,	/* 36 */
  CCI_U_TYPE_TIMESTAMPLTZ,	/* 37 */
  CCI_U_TYPE_DATETIMETZ,	/* 38 */
  CCI_U_TYPE_DATETIMELTZ,	/* 39 */
  CCI_U_TYPE_JSON,		/* 40 */
};

static void
dblob_to_caslob (DB_VALUE * lob, T_LOB_HANDLE * cas_lob)
{
  DB_ELO *elo;

  cas_lob->db_type = db_value_type (lob);
  assert (cas_lob->db_type == DB_TYPE_BLOB || cas_lob->db_type == DB_TYPE_CLOB);
  elo = db_get_elo (lob);
  if (elo == NULL)
    {
      cas_lob->lob_size = -1;
      cas_lob->locator_size = 0;
      cas_lob->locator = NULL;
    }
  else
    {
      cas_lob->lob_size = elo->size;
      cas_lob->locator_size = elo->locator ? strlen (elo->locator) + 1 : 0;
      /* including null character */
      cas_lob->locator = elo->locator;
    }
}

char
ux_db_type_to_cas_type (int db_type)
{
  /* todo: T_CCI_U_TYPE duplicates db types. */
  if (db_type < DB_TYPE_FIRST || db_type > DB_TYPE_LAST)
    {
      return CCI_U_TYPE_NULL;
    }

  return (cas_u_type[db_type]);
}

static bool
has_stmt_result_set (char stmt_type)
{
  switch (stmt_type)
    {
    case CUBRID_STMT_SELECT:
    case CUBRID_STMT_CALL:
    case CUBRID_STMT_GET_STATS:
    case CUBRID_STMT_EVALUATE:
      return true;

    default:
      break;
    }

  return false;
}

static bool
check_auto_commit_after_getting_result (T_SRV_HANDLE * srv_handle)
{
  // To close an updatable cursor is dangerous since it lose locks and updating cursor is allowed before closing it.

  if (srv_handle->auto_commit_mode == TRUE && srv_handle->cur_result_index == srv_handle->num_q_result
      && srv_handle->forward_only_cursor == TRUE && srv_handle->is_updatable == FALSE)
    {
      return true;
    }

  return false;
}

/*
 * fr_get_cursor_count()
 * return: int (srv_h_id)
 *
 * note:
 */
int 
fr_get_cursor_count (int srv_h_id)
{
    int err_code;
    T_SRV_HANDLE * srv_handle = hm_find_srv_handle (srv_h_id);
    if (srv_handle == NULL)
    {
      //err_code = ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
    }

    return srv_handle->q_result->tuple_count;
}

/*
 * fr_move_cursor()
 * return: int (srv_h_id)
 *
 * note:
 */
int 
fr_get_cursor_pos (int srv_h_id)
{
    int err_code;
    T_SRV_HANDLE * srv_handle = hm_find_srv_handle (srv_h_id);
    if (srv_handle == NULL)
    {
      //err_code = ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
    }

    return srv_handle->cursor_pos;
}

/*
 * fr_move_cursor()
 * return: int (DB_CURSOR_SUCCESS, DB_CURSOR_END, DB_CURSOR_ERROR)
 *
 * note:
 */
int 
fr_move_cursor (int srv_h_id, int cursor_pos, int origin)
{
    int err_code;

    T_SRV_HANDLE * srv_handle;
    T_QUERY_RESULT *q_result;
    DB_QUERY_RESULT *result;;

    srv_handle = hm_find_srv_handle (srv_h_id);
    if (srv_handle == NULL)
    {
      //err_code = ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
    }

    q_result = (T_QUERY_RESULT *) (srv_handle->cur_result);
      if (q_result == NULL)
	{
	  //err_code = ERROR_INFO_SET (CAS_ER_NO_MORE_DATA, CAS_ERROR_INDICATOR);
	}

    result = (DB_QUERY_RESULT *) q_result->result;
    if (result == NULL || has_stmt_result_set (q_result->stmt_type) == false)
    {
      //err_code = ERROR_INFO_SET (CAS_ER_NO_MORE_DATA, CAS_ERROR_INDICATOR);
    }

      if (srv_handle->cursor_pos != cursor_pos)
    {
        if (cursor_pos == 1)
        {
        err_code = db_query_first_tuple (result);
        }
        else
        {
        err_code = db_query_seek_tuple (result, cursor_pos - 1, 1);
        }

        srv_handle->cursor_pos = cursor_pos;
    }

    return err_code;
}

/*
 * fr_move_cursor()
 * return: int (DB_CURSOR_SUCCESS, DB_CURSOR_END, DB_CURSOR_ERROR)
 * srv_h_id(in)
 * DB_VALUE(out)
 *
 * note:
 */
int
fr_peek_cursor (int srv_h_id, char fetch_flag, T_NET_BUF * net_buf)
{
  int err_code;
  T_SRV_HANDLE *srv_handle;
  char sensitive_flag = fetch_flag & CCI_FETCH_SENSITIVE;
  
  T_QUERY_RESULT * q_result;
  DB_QUERY_RESULT *result;

  T_OBJECT tuple_obj;
  DB_OBJECT *db_obj;

  if (net_buf->alloc_size == 0 || net_buf->data == NULL)
  {
    net_buf->data = (char *) MALLOC (80 * 1024);
    if (net_buf->data == NULL)
      {
        return -1;
      }
    net_buf->alloc_size = 80 * 1024;
  }
  
  char fetch_end_flag = 0; // output
  
  srv_handle = hm_find_srv_handle (srv_h_id);
    if (srv_handle == NULL)
    {
      //ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
      //return FN_KEEP_CONN;
    }
    q_result = (T_QUERY_RESULT *) (srv_handle->cur_result);
      if (q_result == NULL)
	{
	  //return ERROR_INFO_SET (CAS_ER_NO_MORE_DATA, CAS_ERROR_INDICATOR);
	}

  int cursor_pos = srv_handle->cursor_pos;

      if ((sensitive_flag) && (q_result->col_updatable == TRUE) && (q_result->col_update_info != NULL))
    {
      sensitive_flag = TRUE;
      db_synchronize_cache ();
    }
  else
    {
      sensitive_flag = FALSE;
    }

  memset ((char *) &tuple_obj, 0, sizeof (T_OBJECT));
  db_obj = NULL;

  net_buf_cp_int (net_buf, cursor_pos, NULL);
  if (q_result->include_oid)
  {
      DB_VALUE oid_val;
      er_clear ();

      if (db_query_get_tuple_oid (result, &oid_val) >= 0)
    {
          if (db_value_type (&oid_val) == DB_TYPE_OBJECT)
          {
              db_obj = db_get_object (&oid_val);
              if (db_is_instance (db_obj) > 0)
                  {
                      dbobj_to_casobj (db_obj, &tuple_obj);
                  }
              else if (db_error_code () == 0 || db_error_code () == -48)
                  {
                      memset ((char *) &tuple_obj, 0xff, sizeof (T_OBJECT));
                      db_obj = NULL;
                  }
              else
                  {
                      //return ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
                  }
          }
          db_value_clear (&oid_val);
      }
  }
  net_buf_cp_object (net_buf, &tuple_obj);
  err_code = cursor_tuple (q_result, srv_handle->max_col_size, sensitive_flag, db_obj, net_buf);
  if (err_code < 0)
  {
    return err_code;
  }
  //cursor_pos++;

  if (srv_handle->max_row > 0 && cursor_pos > srv_handle->max_row)
  {
    if (check_auto_commit_after_getting_result (srv_handle) == true)
      {
        //ux_cursor_close (srv_handle);
        //req_info->need_auto_commit = TRAN_AUTOCOMMIT;
      }
    //break;
  }

  //err_code = db_query_next_tuple (result);
  /*
  if (err_code == DB_CURSOR_SUCCESS)
  {
  }
  else if (err_code == DB_CURSOR_END)
  {
    fetch_end_flag = 1;

    if (check_auto_commit_after_getting_result (srv_handle) == true)
      {
        //ux_cursor_close (srv_handle);
        //req_info->need_auto_commit = TRAN_AUTOCOMMIT;
      }
    //break;
  }
      else
  {
    //return ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
  }
  */

  /* Be sure that cursor is closed, if query executed with commit and not holdable. */
  assert (!tran_was_latest_query_committed () || srv_handle->is_holdable == true || err_code == DB_CURSOR_END);
  srv_handle->cursor_pos = cursor_pos;
  db_obj = NULL;

  return cursor_pos;
}

int
fr_fetch (int srv_h_id, int cursor_pos, int fetch_count, char fetch_flag)
{
    int err_code;
    
    T_SRV_HANDLE * srv_handle;
    T_QUERY_RESULT *q_result;
    DB_QUERY_RESULT *result;

    char sensitive_flag = fetch_flag & CCI_FETCH_SENSITIVE;

    T_OBJECT tuple_obj;
    int num_tuple_msg_offset;
    int num_tuple;
    int net_buf_size;

    char fetch_end_flag = 0; // output
    DB_OBJECT *db_obj;

    srv_handle = hm_find_srv_handle (srv_h_id);
    if (srv_handle == NULL)
    {
      //ERROR_INFO_SET (CAS_ER_SRV_HANDLE, CAS_ERROR_INDICATOR);
      //return FN_KEEP_CONN;
    }

    q_result = (T_QUERY_RESULT *) (srv_handle->cur_result);
      if (q_result == NULL)
	{
	  //return ERROR_INFO_SET (CAS_ER_NO_MORE_DATA, CAS_ERROR_INDICATOR);
	}

      if ((sensitive_flag) && (q_result->col_updatable == TRUE) && (q_result->col_update_info != NULL))
    {
      sensitive_flag = TRUE;
      db_synchronize_cache ();
    }
  else
    {
      sensitive_flag = FALSE;
    }

    num_tuple = 0;
    while (num_tuple < fetch_count)
    {
        memset ((char *) &tuple_obj, 0, sizeof (T_OBJECT));
        db_obj = NULL;

        if (q_result->include_oid)
        {
            DB_VALUE oid_val;
            er_clear ();

            if (db_query_get_tuple_oid (result, &oid_val) >= 0)
	        {
                if (db_value_type (&oid_val) == DB_TYPE_OBJECT)
                {
                    db_obj = db_get_object (&oid_val);
                    if (db_is_instance (db_obj) > 0)
                        {
                            dbobj_to_casobj (db_obj, &tuple_obj);
                        }
                    else if (db_error_code () == 0 || db_error_code () == -48)
                        {
                            memset ((char *) &tuple_obj, 0xff, sizeof (T_OBJECT));
                            db_obj = NULL;
                        }
                    else
                        {
                            //return ERROR_INFO_SET (db_error_code (), DBMS_ERROR_INDICATOR);
                        }
                }
                db_value_clear (&oid_val);
            }
        }
        //return tuple_obj
        //err_code = cursor_tuple (q_result, srv_handle->max_col_size, sensitive_flag, db_obj);
        if (err_code < 0)
        {
          return err_code;
        }
        num_tuple++;
        cursor_pos++;

        if (srv_handle->max_row > 0 && cursor_pos > srv_handle->max_row)
        {
          //if (check_auto_commit_after_getting_result (srv_handle) == true)
          //  {
              //ux_cursor_close (srv_handle);
              //req_info->need_auto_commit = TRAN_AUTOCOMMIT;
          //  }
          break;
        }

        err_code = db_query_next_tuple (result);
        if (err_code == DB_CURSOR_SUCCESS)
        {
        }
        else if (err_code == DB_CURSOR_END)
        {
          fetch_end_flag = 1;

          if (check_auto_commit_after_getting_result (srv_handle) == true)
            {
              //ux_cursor_close (srv_handle);
              //req_info->need_auto_commit = TRAN_AUTOCOMMIT;
            }
          break;
        }
            else
        {
          //return ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
        }
    }

    /* Be sure that cursor is closed, if query executed with commit and not holdable. */
    assert (!tran_was_latest_query_committed () || srv_handle->is_holdable == true || err_code == DB_CURSOR_END);
    srv_handle->cursor_pos = cursor_pos;
    db_obj = NULL;
}

static int
cursor_tuple (T_QUERY_RESULT * q_result, int max_col_size, char sensitive_flag, DB_OBJECT * tuple_obj, T_NET_BUF * net_buf)
{
  int err_code;
  int ncols;
  DB_VALUE val;
  int i;
  int error;
  int data_size = 0;
  DB_QUERY_RESULT *result = (DB_QUERY_RESULT *) q_result->result;
  T_COL_UPDATE_INFO *col_update_info = q_result->col_update_info;
  char *null_type_column = q_result->null_type_column;

  ncols = db_query_column_count (result);
  for (i = 0; i < ncols; i++)
    {
      if (sensitive_flag == TRUE && col_update_info[i].updatable == TRUE)
      {
        if (tuple_obj == NULL)
          {
            error = db_make_null (&val);
          }
        else
          {
            error = db_get (tuple_obj, col_update_info[i].attr_name, &val);
          }
      }
      else
      {
        error = db_query_get_tuple_value (result, i, &val);
      }

      if (error < 0)
      {
        //err_code = ERROR_INFO_SET (error, DBMS_ERROR_INDICATOR);
        tuple_obj = NULL;
        return err_code;
      }
      data_size += dbval_to_net_buf (&val, net_buf, 1, max_col_size, null_type_column ? null_type_column[i] : 0);
      db_value_clear (&val);
    }
  tuple_obj = NULL;
  return data_size;
}

static void
dbobj_to_casobj (DB_OBJECT * obj, T_OBJECT * cas_obj)
{
  DB_IDENTIFIER *oid;

  oid = db_identifier (obj);

  if (oid == NULL)
    {
      cas_obj->pageid = 0;
      cas_obj->volid = 0;
      cas_obj->slotid = 0;
    }
  else
    {
      cas_obj->pageid = oid->pageid;
      cas_obj->volid = oid->volid;
      cas_obj->slotid = oid->slotid;
    }
}

static int
dbval_to_net_buf (DB_VALUE * val, T_NET_BUF * net_buf, char fetch_flag, int max_col_size, char column_type_flag)
{
  int data_size = 0;
  unsigned char ext_col_type;
  bool client_support_tz = true;

  if (db_value_is_null (val) == true)
    {
      net_buf_cp_int (net_buf, -1, NULL);
      return NET_SIZE_INT;
    }

  if (!DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (net_buf->client_version, PROTOCOL_V7))
    {
      client_support_tz = false;
    }

  /* set extended type for primary types; for collection types this values is set in switch-case code */
  if (column_type_flag && !TP_IS_SET_TYPE (db_value_type (val)))
    {
      ext_col_type = set_extended_cas_type (CCI_U_TYPE_NULL, db_value_type (val));
    }
  else
    {
      ext_col_type = 0;
    }

  switch (db_value_type (val))
    {
    case DB_TYPE_OBJECT:
      {
	DB_OBJECT *obj;
	T_OBJECT cas_obj;

	obj = db_get_object (val);
	dbobj_to_casobj (obj, &cas_obj);
	add_res_data_object (net_buf, &cas_obj, ext_col_type, &data_size);
      }
      break;
    case DB_TYPE_VARBIT:
    case DB_TYPE_BIT:
      {
	int length = 0;

	DB_CONST_C_BIT bit = db_get_bit (val, &length);
	length = (length + 7) / 8;
	if (max_col_size > 0)
	  {
	    length = MIN (length, max_col_size);
	  }
	/* do not append NULL terminator */
	add_res_data_bytes (net_buf, bit, length, ext_col_type, &data_size);
      }
      break;
    case DB_TYPE_VARCHAR:
    case DB_TYPE_CHAR:
      {
	DB_CONST_C_CHAR str;
	int dummy = 0;
	int bytes_size = 0;
	int decomp_size;
	char *decomposed = NULL;
	bool need_decomp = false;

	str = db_get_char (val, &dummy);
	bytes_size = db_get_string_size (val);
	if (max_col_size > 0)
	  {
	    bytes_size = MIN (bytes_size, max_col_size);
	  }

	if (db_get_string_codeset (val) == INTL_CODESET_UTF8)
	  {
	    need_decomp =
	      unicode_string_need_decompose (str, bytes_size, &decomp_size, lang_get_generic_unicode_norm ());
	  }


	if (need_decomp)
	  {
	    decomposed = (char *) MALLOC (decomp_size * sizeof (char));
	    if (decomposed != NULL)
	      {
		unicode_decompose_string (str, bytes_size, decomposed, &decomp_size, lang_get_generic_unicode_norm ());

		str = decomposed;
		bytes_size = decomp_size;
	      }
	    else
	      {
		/* set error indicator and send empty string */
		//ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
		bytes_size = 0;
	      }
	  }

	add_res_data_string (net_buf, str, bytes_size, ext_col_type, db_get_string_codeset (val), &data_size);

	if (decomposed != NULL)
	  {
	    FREE (decomposed);
	    decomposed = NULL;
	  }
      }
      break;
    case DB_TYPE_VARNCHAR:
    case DB_TYPE_NCHAR:
      {
	DB_CONST_C_NCHAR nchar;
	int dummy = 0;
	int bytes_size = 0;
	int decomp_size;
	char *decomposed = NULL;
	bool need_decomp = false;

	nchar = db_get_nchar (val, &dummy);
	bytes_size = db_get_string_size (val);
	if (max_col_size > 0)
	  {
	    bytes_size = MIN (bytes_size, max_col_size);
	  }

	if (db_get_string_codeset (val) == INTL_CODESET_UTF8)
	  {
	    need_decomp =
	      unicode_string_need_decompose (nchar, bytes_size, &decomp_size, lang_get_generic_unicode_norm ());
	  }

	if (need_decomp)
	  {
	    decomposed = (char *) MALLOC (decomp_size * sizeof (char));
	    if (decomposed != NULL)
	      {
		unicode_decompose_string (nchar, bytes_size, decomposed, &decomp_size,
					  lang_get_generic_unicode_norm ());

		nchar = decomposed;
		bytes_size = decomp_size;
	      }
	    else
	      {
		/* set error indicator and send empty string */
		//ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
		bytes_size = 0;
	      }
	  }

	add_res_data_string (net_buf, nchar, bytes_size, ext_col_type, db_get_string_codeset (val), &data_size);

	if (decomposed != NULL)
	  {
	    FREE (decomposed);
	    decomposed = NULL;
	  }
      }
      break;
    case DB_TYPE_ENUMERATION:
      {
	int bytes_size = 0;
	int decomp_size;
	char *decomposed = NULL;
	bool need_decomp = false;

	const char *str = db_get_enum_string (val);
	bytes_size = db_get_enum_string_size (val);
	if (max_col_size > 0)
	  {
	    bytes_size = MIN (bytes_size, max_col_size);
	  }

	if (db_get_enum_codeset (val) == INTL_CODESET_UTF8)
	  {
	    need_decomp =
	      unicode_string_need_decompose (str, bytes_size, &decomp_size, lang_get_generic_unicode_norm ());
	  }

	if (need_decomp)
	  {
	    decomposed = (char *) MALLOC (decomp_size * sizeof (char));
	    if (decomposed != NULL)
	      {
		unicode_decompose_string (str, bytes_size, decomposed, &decomp_size, lang_get_generic_unicode_norm ());

		str = decomposed;
		bytes_size = decomp_size;
	      }
	    else
	      {
		/* set error indicator and send empty string */
		//ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
		bytes_size = 0;
	      }
	  }

	add_res_data_string (net_buf, str, bytes_size, ext_col_type, db_get_enum_codeset (val), &data_size);

	if (decomposed != NULL)
	  {
	    FREE (decomposed);
	    decomposed = NULL;
	  }

	break;
      }
    case DB_TYPE_SMALLINT:
      {
	short smallint;
	smallint = db_get_short (val);
	add_res_data_short (net_buf, smallint, ext_col_type, &data_size);
      }
      break;
    case DB_TYPE_INTEGER:
      {
	int int_val;
	int_val = db_get_int (val);
	add_res_data_int (net_buf, int_val, ext_col_type, &data_size);
      }
      break;
    case DB_TYPE_BIGINT:
      {
	DB_BIGINT bigint_val;
	bigint_val = db_get_bigint (val);
	add_res_data_bigint (net_buf, bigint_val, ext_col_type, &data_size);
      }
      break;
    case DB_TYPE_DOUBLE:
      {
	double d_val;
	d_val = db_get_double (val);
	add_res_data_double (net_buf, d_val, ext_col_type, &data_size);
      }
      break;
    case DB_TYPE_MONETARY:
      {
	double d_val;
	d_val = db_value_get_monetary_amount_as_double (val);
	add_res_data_double (net_buf, d_val, ext_col_type, &data_size);
      }
      break;
    case DB_TYPE_FLOAT:
      {
	float f_val;
	f_val = db_get_float (val);
	add_res_data_float (net_buf, f_val, ext_col_type, &data_size);
      }
      break;
    case DB_TYPE_DATE:
      {
	DB_DATE *db_date;
	int yr, mon, day;
	db_date = db_get_date (val);
	db_date_decode (db_date, &mon, &day, &yr);
	add_res_data_date (net_buf, (short) yr, (short) mon, (short) day, ext_col_type, &data_size);
      }
      break;
    case DB_TYPE_TIME:
      {
	DB_TIME *time;
	int hour, minute, second;
	time = db_get_time (val);
	db_time_decode (time, &hour, &minute, &second);
	add_res_data_time (net_buf, (short) hour, (short) minute, (short) second, ext_col_type, &data_size);
      }
      break;
    case DB_TYPE_TIMESTAMP:
      {
	DB_TIMESTAMP *ts;
	DB_DATE date;
	DB_TIME time;
	int yr, mon, day, hh, mm, ss;
	ts = db_get_timestamp (val);
	(void) db_timestamp_decode_ses (ts, &date, &time);
	db_date_decode (&date, &mon, &day, &yr);
	db_time_decode (&time, &hh, &mm, &ss);
	add_res_data_timestamp (net_buf, (short) yr, (short) mon, (short) day, (short) hh, (short) mm, (short) ss,
				ext_col_type, &data_size);
      }
      break;
    case DB_TYPE_TIMESTAMPLTZ:
    case DB_TYPE_TIMESTAMPTZ:
      {
	DB_TIMESTAMP ts, *ts_p;
	DB_TIMESTAMPTZ *ts_tz;
	DB_DATE date;
	DB_TIME time;
	int err;
	int yr, mon, day, hh, mm, ss;
	TZ_ID tz_id;
	char tz_str[CCI_TZ_SIZE + 1];

	if (db_value_type (val) == DB_TYPE_TIMESTAMPLTZ)
	  {
	    ts_p = db_get_timestamp (val);
	    ts = *ts_p;
	    err = tz_create_session_tzid_for_timestamp (&ts, &tz_id);
	    if (err != NO_ERROR)
	      {
		net_buf_cp_int (net_buf, -1, NULL);
		data_size = NET_SIZE_INT;
		break;
	      }
	  }
	else
	  {
	    ts_tz = db_get_timestamptz (val);
	    ts = ts_tz->timestamp;
	    tz_id = ts_tz->tz_id;
	  }

	err = db_timestamp_decode_w_tz_id (&ts, &tz_id, &date, &time);
	if (err != NO_ERROR)
	  {
	    net_buf_cp_int (net_buf, -1, NULL);
	    data_size = NET_SIZE_INT;
	    break;
	  }

	if (tz_id_to_str (&tz_id, tz_str, CCI_TZ_SIZE) < 0)
	  {
	    net_buf_cp_int (net_buf, -1, NULL);
	    data_size = NET_SIZE_INT;
	    break;
	  }

	db_date_decode (&date, &mon, &day, &yr);
	db_time_decode (&time, &hh, &mm, &ss);
	if (client_support_tz == true)
	  {
	    add_res_data_timestamptz (net_buf, (short) yr, (short) mon, (short) day, (short) hh, (short) mm, (short) ss,
				      tz_str, ext_col_type, &data_size);
	  }
	else
	  {
	    add_res_data_timestamp (net_buf, (short) yr, (short) mon, (short) day, (short) hh, (short) mm, (short) ss,
				    ext_col_type, &data_size);
	  }
      }
      break;
    case DB_TYPE_DATETIME:
      {
	DB_DATETIME *dt;
	int yr, mon, day, hh, mm, ss, ms;
	dt = db_get_datetime (val);
	db_datetime_decode (dt, &mon, &day, &yr, &hh, &mm, &ss, &ms);
	add_res_data_datetime (net_buf, (short) yr, (short) mon, (short) day, (short) hh, (short) mm, (short) ss,
			       (short) ms, ext_col_type, &data_size);
      }
      break;
    case DB_TYPE_DATETIMELTZ:
    case DB_TYPE_DATETIMETZ:
      {
	DB_DATETIME dt_local, dt_utc, *dt_utc_p;
	TZ_ID tz_id;
	DB_DATETIMETZ *dt_tz;
	int err;
	int yr, mon, day, hh, mm, ss, ms;
	char tz_str[CCI_TZ_SIZE + 1];

	if (db_value_type (val) == DB_TYPE_DATETIMELTZ)
	  {
	    dt_utc_p = db_get_datetime (val);
	    dt_utc = *dt_utc_p;
	    err = tz_create_session_tzid_for_datetime (&dt_utc, true, &tz_id);
	    if (err != NO_ERROR)
	      {
		net_buf_cp_int (net_buf, -1, NULL);
		data_size = NET_SIZE_INT;
		break;
	      }
	  }
	else
	  {
	    dt_tz = db_get_datetimetz (val);
	    dt_utc = dt_tz->datetime;
	    tz_id = dt_tz->tz_id;
	  }

	err = tz_utc_datetimetz_to_local (&dt_utc, &tz_id, &dt_local);
	if (err == ER_QPROC_TIME_UNDERFLOW)
	  {
	    db_datetime_encode (&dt_local, 0, 0, 0, 0, 0, 0, 0);
	    err = NO_ERROR;
	    er_clear ();
	  }

	if (err != NO_ERROR)
	  {
	    net_buf_cp_int (net_buf, -1, NULL);
	    data_size = NET_SIZE_INT;
	    break;
	  }

	if (tz_id_to_str (&tz_id, tz_str, CCI_TZ_SIZE) < 0)
	  {
	    net_buf_cp_int (net_buf, -1, NULL);
	    data_size = NET_SIZE_INT;
	    break;
	  }

	db_datetime_decode (&dt_local, &mon, &day, &yr, &hh, &mm, &ss, &ms);
	if (client_support_tz == true)
	  {
	    add_res_data_datetimetz (net_buf, (short) yr, (short) mon, (short) day, (short) hh, (short) mm, (short) ss,
				     (short) ms, tz_str, ext_col_type, &data_size);
	  }
	else
	  {
	    add_res_data_datetime (net_buf, (short) yr, (short) mon, (short) day, (short) hh, (short) mm, (short) ss,
				   (short) ms, ext_col_type, &data_size);
	  }
      }
      break;
    case DB_TYPE_NUMERIC:
      {
	DB_DOMAIN *char_domain;
	DB_VALUE v;
	const char *str;
	int len, err;
	char buf[128];

	char_domain = db_type_to_db_domain (DB_TYPE_VARCHAR);
	err = db_value_coerce (val, &v, char_domain);
	if (err < 0)
	  {
	    net_buf_cp_int (net_buf, -1, NULL);
	    data_size = NET_SIZE_INT;
	  }
	else
	  {
	    str = db_get_char (&v, &len);
	    if (str != NULL)
	      {
		strncpy (buf, str, sizeof (buf) - 1);
		buf[sizeof (buf) - 1] = '\0';
		ut_trim (buf);
		add_res_data_string (net_buf, buf, strlen (buf), ext_col_type, CAS_SCHEMA_DEFAULT_CHARSET, &data_size);
	      }
	    else
	      {
		net_buf_cp_int (net_buf, -1, NULL);
		data_size = NET_SIZE_INT;
	      }
	    db_value_clear (&v);
	  }
      }
      break;
    case DB_TYPE_SET:
    case DB_TYPE_MULTISET:
    case DB_TYPE_SEQUENCE:	/* DB_TYPE_LIST */
      {
	DB_SET *set;
	int i;
	DB_VALUE *element;
	int num_element;
	char cas_type = CCI_U_TYPE_NULL;
	char err_flag = 0;
	char set_dbtype = DB_TYPE_NULL;
	unsigned char charset = CAS_SCHEMA_DEFAULT_CHARSET;

	set = db_get_set (val);

	num_element = db_set_size (set);
	element = (DB_VALUE *) malloc (sizeof (DB_VALUE) * num_element);
	if (element == NULL)
	  {
	    err_flag = 1;
	  }

	if (!err_flag)
	  {
	    if (num_element <= 0)
	      {
		DB_DOMAIN *set_domain;
		char element_type;
		set_domain = db_col_domain (set);
		element_type = get_set_domain (set_domain, NULL, NULL, &set_dbtype, (char *) &charset);
		if (element_type > 0)
		  {
		    cas_type = element_type;
		  }
	      }
	    else
	      {
		for (i = 0; i < num_element; i++)
		  {
		    db_set_get (set, i, &(element[i]));
		    set_dbtype = db_value_type (&(element[i]));
		    charset = db_get_string_codeset (&(element[i]));
		    if (i == 0 || cas_type == CCI_U_TYPE_NULL)
		      {
			cas_type = ux_db_type_to_cas_type (set_dbtype);
		      }
		    else
		      {
			char tmp_type;
			tmp_type = ux_db_type_to_cas_type (set_dbtype);
			if (db_value_is_null (&(element[i])) == false && cas_type != tmp_type)
			  {
			    err_flag = 1;
			    break;
			  }
		      }
		  }		/* end of for */
	      }

	    if ((err_flag) && (element != NULL))
	      {
		for (; i >= 0; i--)
		  {
		    db_value_clear (&(element[i]));
		  }
		FREE_MEM (element);
	      }
	  }

	if (err_flag)
	  {
	    net_buf_cp_int (net_buf, -1, NULL);
	    data_size = NET_SIZE_INT;
	  }
	else
	  {
	    int set_data_size;
	    int set_size_msg_offset;

	    set_data_size = 0;
	    net_buf_cp_int (net_buf, set_data_size, &set_size_msg_offset);

	    if (column_type_flag)
	      {
		ext_col_type = set_extended_cas_type ((T_CCI_U_TYPE) set_dbtype, db_value_type (val));

		net_buf_cp_cas_type_and_charset (net_buf, ext_col_type, charset);
		set_data_size++;
		if (DOES_CLIENT_UNDERSTAND_THE_PROTOCOL (net_buf->client_version, PROTOCOL_V7))
		  {
		    set_data_size++;
		  }
	      }

	    net_buf_cp_byte (net_buf, cas_type);
	    set_data_size++;

	    net_buf_cp_int (net_buf, num_element, NULL);
	    set_data_size += NET_SIZE_INT;

	    for (i = 0; i < num_element; i++)
	      {
		set_data_size += dbval_to_net_buf (&(element[i]), net_buf, 1, max_col_size, 0);
		db_value_clear (&(element[i]));
	      }
	    FREE_MEM (element);

	    net_buf_overwrite_int (net_buf, set_size_msg_offset, set_data_size);
	    data_size = NET_SIZE_INT + set_data_size;
	  }

	if (fetch_flag)
	  db_set_free (set);
      }
      break;

    case DB_TYPE_RESULTSET:
      {
	int h_id;

	h_id = db_get_resultset (val);
	add_res_data_int (net_buf, h_id, ext_col_type, &data_size);
      }
      break;

    case DB_TYPE_BLOB:
    case DB_TYPE_CLOB:
      {
	T_LOB_HANDLE cas_lob;

	dblob_to_caslob (val, &cas_lob);
	add_res_data_lob_handle (net_buf, &cas_lob, ext_col_type, &data_size);
      }
      break;
    case DB_TYPE_JSON:
      {
	char *str;
	int bytes_size = 0;

	str = db_get_json_raw_body (val);
	bytes_size = strlen (str);

	/* no matter which column type is returned to client (JSON or STRING, depending on client version),
	 * the data is always encoded as string */
	add_res_data_string (net_buf, str, bytes_size, 0, INTL_CODESET_UTF8, &data_size);
	db_private_free (NULL, str);
      }
      break;
    default:
      net_buf_cp_int (net_buf, -1, NULL);	/* null */
      data_size = 4;
      break;
    }

  return data_size;
}


static void
add_res_data_bytes (T_NET_BUF * net_buf, const char *str, int size, unsigned char ext_type, int *net_size)
{
  if (ext_type)
    {
      net_buf_cp_int (net_buf, NET_BUF_TYPE_SIZE (net_buf) + size, NULL);	/* type */
      net_buf_cp_cas_type_and_charset (net_buf, ext_type, CAS_SCHEMA_DEFAULT_CHARSET);
    }
  else
    {
      net_buf_cp_int (net_buf, size, NULL);
    }

  /* do not append NULL terminator */
  net_buf_cp_str (net_buf, str, size);

  if (net_size)
    {
      *net_size = NET_SIZE_INT + (ext_type ? NET_BUF_TYPE_SIZE (net_buf) : 0) + size;
    }
}

static void
add_res_data_string (T_NET_BUF * net_buf, const char *str, int size, unsigned char ext_type, unsigned char charset,
		     int *net_size)
{
  if (ext_type)
    {
      net_buf_cp_int (net_buf, NET_BUF_TYPE_SIZE (net_buf) + size + 1, NULL);	/* type, NULL terminator */
      net_buf_cp_cas_type_and_charset (net_buf, ext_type, charset);
    }
  else
    {
      net_buf_cp_int (net_buf, size + 1, NULL);	/* NULL terminator */
    }

  net_buf_cp_str (net_buf, str, size);
  net_buf_cp_byte (net_buf, '\0');

  if (net_size)
    {
      *net_size = NET_SIZE_INT + (ext_type ? NET_BUF_TYPE_SIZE (net_buf) : 0) + size + NET_SIZE_BYTE;
    }
}

static void
add_res_data_string_safe (T_NET_BUF * net_buf, const char *str, unsigned char ext_type, unsigned char charset,
			  int *net_size)
{
  if (str != NULL)
    {
      add_res_data_string (net_buf, str, strlen (str), ext_type, charset, net_size);
    }
  else
    {
      add_res_data_string (net_buf, "", 0, ext_type, charset, net_size);
    }
}

static void
add_res_data_int (T_NET_BUF * net_buf, int value, unsigned char ext_type, int *net_size)
{
  if (ext_type)
    {
      net_buf_cp_int (net_buf, NET_BUF_TYPE_SIZE (net_buf) + NET_SIZE_INT, NULL);
      net_buf_cp_cas_type_and_charset (net_buf, ext_type, CAS_SCHEMA_DEFAULT_CHARSET);
      net_buf_cp_int (net_buf, value, NULL);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_INT, NULL);
      net_buf_cp_int (net_buf, value, NULL);
    }

  if (net_size)
    {
      *net_size = NET_SIZE_INT + (ext_type ? NET_BUF_TYPE_SIZE (net_buf) : 0) + NET_SIZE_INT;
    }
}

static void
add_res_data_bigint (T_NET_BUF * net_buf, DB_BIGINT value, unsigned char ext_type, int *net_size)
{
  if (ext_type)
    {
      net_buf_cp_int (net_buf, NET_BUF_TYPE_SIZE (net_buf) + NET_SIZE_BIGINT, NULL);
      net_buf_cp_cas_type_and_charset (net_buf, ext_type, CAS_SCHEMA_DEFAULT_CHARSET);
      net_buf_cp_bigint (net_buf, value, NULL);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_BIGINT, NULL);
      net_buf_cp_bigint (net_buf, value, NULL);
    }

  if (net_size)
    {
      *net_size = NET_SIZE_INT + (ext_type ? NET_BUF_TYPE_SIZE (net_buf) : 0) + NET_SIZE_BIGINT;
    }
}

static void
add_res_data_short (T_NET_BUF * net_buf, short value, unsigned char ext_type, int *net_size)
{
  if (ext_type)
    {
      net_buf_cp_int (net_buf, NET_BUF_TYPE_SIZE (net_buf) + NET_SIZE_SHORT, NULL);
      net_buf_cp_cas_type_and_charset (net_buf, ext_type, CAS_SCHEMA_DEFAULT_CHARSET);
      net_buf_cp_short (net_buf, value);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_SHORT, NULL);
      net_buf_cp_short (net_buf, value);
    }

  if (net_size)
    {
      *net_size = NET_SIZE_INT + (ext_type ? NET_BUF_TYPE_SIZE (net_buf) : 0) + NET_SIZE_SHORT;
    }
}

static void
add_res_data_float (T_NET_BUF * net_buf, float value, unsigned char ext_type, int *net_size)
{
  if (ext_type)
    {
      net_buf_cp_int (net_buf, NET_BUF_TYPE_SIZE (net_buf) + NET_SIZE_FLOAT, NULL);
      net_buf_cp_cas_type_and_charset (net_buf, ext_type, CAS_SCHEMA_DEFAULT_CHARSET);
      net_buf_cp_float (net_buf, value);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_FLOAT, NULL);
      net_buf_cp_float (net_buf, value);
    }

  if (net_size)
    {
      *net_size = NET_SIZE_INT + (ext_type ? NET_BUF_TYPE_SIZE (net_buf) : 0) + NET_SIZE_FLOAT;
    }
}

static void
add_res_data_double (T_NET_BUF * net_buf, double value, unsigned char ext_type, int *net_size)
{
  if (ext_type)
    {
      net_buf_cp_int (net_buf, NET_BUF_TYPE_SIZE (net_buf) + NET_SIZE_DOUBLE, NULL);
      net_buf_cp_cas_type_and_charset (net_buf, ext_type, CAS_SCHEMA_DEFAULT_CHARSET);
      net_buf_cp_double (net_buf, value);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_DOUBLE, NULL);
      net_buf_cp_double (net_buf, value);
    }

  if (net_size)
    {
      *net_size = NET_SIZE_INT + (ext_type ? NET_BUF_TYPE_SIZE (net_buf) : 0) + NET_SIZE_DOUBLE;
    }
}

static void
add_res_data_timestamp (T_NET_BUF * net_buf, short yr, short mon, short day, short hh, short mm, short ss,
			unsigned char ext_type, int *net_size)
{
  if (ext_type)
    {
      net_buf_cp_int (net_buf, NET_BUF_TYPE_SIZE (net_buf) + NET_SIZE_TIMESTAMP, NULL);
      net_buf_cp_cas_type_and_charset (net_buf, ext_type, CAS_SCHEMA_DEFAULT_CHARSET);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_TIMESTAMP, NULL);
    }

  net_buf_cp_short (net_buf, yr);
  net_buf_cp_short (net_buf, mon);
  net_buf_cp_short (net_buf, day);
  net_buf_cp_short (net_buf, hh);
  net_buf_cp_short (net_buf, mm);
  net_buf_cp_short (net_buf, ss);

  if (net_size)
    {
      *net_size = (NET_SIZE_INT + (ext_type ? NET_BUF_TYPE_SIZE (net_buf) : 0) + NET_SIZE_TIMESTAMP);
    }
}

static void
add_res_data_timestamptz (T_NET_BUF * net_buf, short yr, short mon, short day, short hh, short mm, short ss,
			  char *tz_str, unsigned char ext_type, int *net_size)
{
  int tz_size;

  tz_size = strlen (tz_str);

  if (ext_type)
    {
      net_buf_cp_int (net_buf, (NET_BUF_TYPE_SIZE (net_buf) + NET_SIZE_TIMESTAMP + tz_size + 1), NULL);
      net_buf_cp_cas_type_and_charset (net_buf, ext_type, CAS_SCHEMA_DEFAULT_CHARSET);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_TIMESTAMP + tz_size + 1, NULL);
    }

  net_buf_cp_short (net_buf, yr);
  net_buf_cp_short (net_buf, mon);
  net_buf_cp_short (net_buf, day);
  net_buf_cp_short (net_buf, hh);
  net_buf_cp_short (net_buf, mm);
  net_buf_cp_short (net_buf, ss);

  net_buf_cp_str (net_buf, tz_str, tz_size);
  net_buf_cp_byte (net_buf, '\0');

  if (net_size)
    {
      *net_size = (NET_SIZE_INT + (ext_type ? NET_BUF_TYPE_SIZE (net_buf) : 0) + NET_SIZE_TIMESTAMP + tz_size + 1);
    }
}

static void
add_res_data_datetime (T_NET_BUF * net_buf, short yr, short mon, short day, short hh, short mm, short ss, short ms,
		       unsigned char ext_type, int *net_size)
{
  if (ext_type)
    {
      net_buf_cp_int (net_buf, NET_BUF_TYPE_SIZE (net_buf) + NET_SIZE_DATETIME, NULL);
      net_buf_cp_cas_type_and_charset (net_buf, ext_type, CAS_SCHEMA_DEFAULT_CHARSET);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_DATETIME, NULL);
    }

  net_buf_cp_short (net_buf, yr);
  net_buf_cp_short (net_buf, mon);
  net_buf_cp_short (net_buf, day);
  net_buf_cp_short (net_buf, hh);
  net_buf_cp_short (net_buf, mm);
  net_buf_cp_short (net_buf, ss);
  net_buf_cp_short (net_buf, ms);

  if (net_size)
    {
      *net_size = (NET_SIZE_INT + (ext_type ? NET_BUF_TYPE_SIZE (net_buf) : 0) + NET_SIZE_DATETIME);
    }
}

static void
add_res_data_datetimetz (T_NET_BUF * net_buf, short yr, short mon, short day, short hh, short mm, short ss, short ms,
			 char *tz_str, unsigned char ext_type, int *net_size)
{
  int tz_size;
  int net_buf_type_size = NET_BUF_TYPE_SIZE (net_buf);

  tz_size = strlen (tz_str);


  if (ext_type)
    {
      net_buf_cp_int (net_buf, (net_buf_type_size + NET_SIZE_DATETIME + tz_size + 1), NULL);
      net_buf_cp_cas_type_and_charset (net_buf, ext_type, CAS_SCHEMA_DEFAULT_CHARSET);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_DATETIME + tz_size + 1, NULL);
    }

  net_buf_cp_short (net_buf, yr);
  net_buf_cp_short (net_buf, mon);
  net_buf_cp_short (net_buf, day);
  net_buf_cp_short (net_buf, hh);
  net_buf_cp_short (net_buf, mm);
  net_buf_cp_short (net_buf, ss);
  net_buf_cp_short (net_buf, ms);

  net_buf_cp_str (net_buf, tz_str, tz_size);
  net_buf_cp_byte (net_buf, '\0');

  if (net_size)
    {
      *net_size = (NET_SIZE_INT + (ext_type ? net_buf_type_size : 0) + NET_SIZE_DATETIME + tz_size + 1);
    }
}

static void
add_res_data_time (T_NET_BUF * net_buf, short hh, short mm, short ss, unsigned char ext_type, int *net_size)
{
  if (ext_type)
    {
      net_buf_cp_int (net_buf, NET_BUF_TYPE_SIZE (net_buf) + NET_SIZE_TIME, NULL);
      net_buf_cp_cas_type_and_charset (net_buf, ext_type, CAS_SCHEMA_DEFAULT_CHARSET);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_TIME, NULL);
    }

  net_buf_cp_short (net_buf, hh);
  net_buf_cp_short (net_buf, mm);
  net_buf_cp_short (net_buf, ss);

  if (net_size)
    {
      *net_size = NET_SIZE_INT + (ext_type ? NET_BUF_TYPE_SIZE (net_buf) : 0) + NET_SIZE_TIME;
    }
}

static void
add_res_data_date (T_NET_BUF * net_buf, short yr, short mon, short day, unsigned char ext_type, int *net_size)
{
  if (ext_type)
    {
      net_buf_cp_int (net_buf, NET_BUF_TYPE_SIZE (net_buf) + NET_SIZE_DATE, NULL);
      net_buf_cp_cas_type_and_charset (net_buf, ext_type, CAS_SCHEMA_DEFAULT_CHARSET);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_DATE, NULL);
    }

  net_buf_cp_short (net_buf, yr);
  net_buf_cp_short (net_buf, mon);
  net_buf_cp_short (net_buf, day);

  if (net_size)
    {
      *net_size = NET_SIZE_INT + (ext_type ? NET_BUF_TYPE_SIZE (net_buf) : 0) + NET_SIZE_DATE;
    }
}

static void
add_res_data_object (T_NET_BUF * net_buf, T_OBJECT * obj, unsigned char ext_type, int *net_size)
{
  if (ext_type)
    {
      net_buf_cp_int (net_buf, NET_BUF_TYPE_SIZE (net_buf) + NET_SIZE_OBJECT, NULL);
      net_buf_cp_cas_type_and_charset (net_buf, ext_type, CAS_SCHEMA_DEFAULT_CHARSET);
    }
  else
    {
      net_buf_cp_int (net_buf, NET_SIZE_OBJECT, NULL);
    }

  net_buf_cp_object (net_buf, obj);

  if (net_size)
    {
      *net_size = NET_SIZE_INT + (ext_type ? NET_BUF_TYPE_SIZE (net_buf) : 0) + NET_SIZE_OBJECT;
    }
}

static void
add_res_data_lob_handle (T_NET_BUF * net_buf, T_LOB_HANDLE * lob, unsigned char ext_type, int *net_size)
{
  int lob_handle_size = (NET_SIZE_INT + NET_SIZE_INT64 + NET_SIZE_INT + lob->locator_size);

  /* db_type + lob_size + locator_size + locator including null character */
  if (ext_type)
    {
      net_buf_cp_int (net_buf, NET_BUF_TYPE_SIZE (net_buf) + lob_handle_size, NULL);
      net_buf_cp_cas_type_and_charset (net_buf, ext_type, CAS_SCHEMA_DEFAULT_CHARSET);
    }
  else
    {
      net_buf_cp_int (net_buf, lob_handle_size, NULL);
    }
  net_buf_cp_lob_handle (net_buf, lob);
  if (net_size)
    {
      *net_size = NET_SIZE_INT + (ext_type ? NET_BUF_TYPE_SIZE (net_buf) : 0) + lob_handle_size;
    }
}


static unsigned char
set_extended_cas_type (T_CCI_U_TYPE u_set_type, DB_TYPE db_type)
{
  /* todo: T_CCI_U_TYPE duplicates db types. */
  unsigned char u_set_type_lsb, u_set_type_msb;

  if (TP_IS_SET_TYPE (db_type))
    {
      unsigned char cas_ext_type;

      u_set_type_lsb = u_set_type & 0x1f;
      u_set_type_msb = (u_set_type & 0x20) << 2;

      u_set_type = (T_CCI_U_TYPE) (u_set_type_lsb | u_set_type_msb);

      cas_ext_type = CAS_TYPE_COLLECTION (db_type, u_set_type);
      return cas_ext_type;
    }

  u_set_type = (T_CCI_U_TYPE) ux_db_type_to_cas_type (db_type);

  u_set_type_lsb = u_set_type & 0x1f;
  u_set_type_msb = (u_set_type & 0x20) << 2;

  u_set_type = (T_CCI_U_TYPE) (u_set_type_lsb | u_set_type_msb);

  return u_set_type;
}

char
get_set_domain (DB_DOMAIN * set_domain, int *precision, short *scale, char *db_type, char *charset)
{
  DB_DOMAIN *ele_domain;
  int set_domain_count = 0;
  int set_type = DB_TYPE_NULL;

  if (precision)
    {
      *precision = 0;
    }
  if (scale)
    {
      *scale = 0;
    }
  if (charset)
    {
      *charset = lang_charset ();
    }

  ele_domain = db_domain_set (set_domain);
  for (; ele_domain; ele_domain = db_domain_next (ele_domain))
    {
      set_domain_count++;
      set_type = TP_DOMAIN_TYPE (ele_domain);
      if (precision)
	{
	  *precision = db_domain_precision (ele_domain);
	}

      if (scale)
	{
	  *scale = (short) db_domain_scale (ele_domain);
	}
      if (charset)
	{
	  *charset = db_domain_codeset (ele_domain);
	}
    }

  if (db_type)
    {
      *db_type = (set_domain_count != 1) ? DB_TYPE_NULL : set_type;
    }

  if (set_domain_count != 1)
    {
      return 0;
    }

  return (ux_db_type_to_cas_type (set_type));
}