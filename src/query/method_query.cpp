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

#include "method_query.hpp"

#define MALLOC(SIZE)            malloc(SIZE)
#define REALLOC(PTR, SIZE)      \
        ((PTR == NULL) ? malloc(SIZE) : realloc(PTR, SIZE))
#define FREE(PTR)               free(PTR)

#define FREE_MEM(PTR)		\
	do {			\
	  if (PTR) {		\
	    FREE(PTR);		\
	    PTR = 0;	\
	  }			\
	} while (0)

#if defined (CS_MODE)
#include "db.h"
#include "dbi.h"
#include "dbtype.h"
#include "method_query_util.hpp"
#include "object_primitive.h"
#endif

namespace cubmethod
{
#if defined (CS_MODE)
  int
  prepare_call_info::set_is_first_out (std::string &sql_stmt)
  {
    int error = NO_ERROR;

    char *tmp = sql_stmt.data ();
    if (!sql_stmt.empty() && sql_stmt[0] == '?')
      {
	is_first_out = true;
	int i;
	for (i = 0; i < sql_stmt.size(); i++)
	  {
	    if (sql_stmt[i] == '=')
	      {
		break;
	      }
	  }

	/* '=' is not found */
	if (i == sql_stmt.size())
	  {
	    // TODO: error handling
	    // error = CAS_ER_INVALID_CALL_STMT;
	    error = ER_FAILED;
	    return error;
	  }

	sql_stmt = sql_stmt.substr (i);
      }
  }

  int
  prepare_call_info::set_prepare_call_info (int num_args)
  {
    db_make_null (&dbval_ret);
    
    this->num_args = num_args;
    if (num_args > 0)
      {
	param_mode.resize (num_args);
	for (int i = 0; i < param_mode.size(); i++)
	  {
	    param_mode[i] = 0;
	  }

	dbval_args.resize (num_args + 1);
	for (int i = 0; i < dbval_args.size(); i++)
	  {
	    db_make_null (&dbval_args[i]);
	  }
      }

    return NO_ERROR;
  }

  query_handler::query_handler (error_context &ctx, int id)
    : m_error_ctx (ctx), m_id (id)
  {
    m_use_plan_cache = false;
  }

  prepare_info
  query_handler::prepare (std::string sql, int flag)
  {
    m_sql_stmt = sql;
    m_query_info_flag = (flag & PREPARE_QUERY_INFO) ? true : false;
    m_is_updatable = (flag & PREPARE_UPDATABLE) ? true : false;
    m_schema_type = -1;

    int error = NO_ERROR;
    prepare_info info;
    if (flag & PREPARE_CALL)
      {
	error = prepare_call (info, flag);
      }
    else
      {
	error = prepare_query (info, flag);
      }

    if (error == NO_ERROR)
      {
  query_result& qresult = m_q_result[0]; /* only one result */
  info.stmt_type = qresult.stmt_type;
  
	info.num_markers = m_num_markers;
	error = set_prepare_column_list_info (info.column_infos, qresult);
      }
    else
      {
	close_and_free_session ();
	m_num_errors++;
      }

    return info;
  }

  execute_info
  query_handler::execute (std::vector<DB_VALUE> &bind_values, int flag, int max_col_size, int max_row)
  {
    int error = NO_ERROR;

    // TODO
    execute_info info;
    if (m_prepare_flag & PREPARE_CALL)
      {
	// error = execute_internal_call ();
      }
    else if (flag & EXEC_QUERY_ALL)
      {
        bool is_savepoint = false;
        error = execute_internal_all (info, flag, max_col_size, max_row, bind_values);
        if (error != NO_ERROR)
        {
          if (m_prepare_flag & PREPARE_XASL_CACHE_PINNED)
          {
            db_session_set_xasl_cache_pinned (m_session, false, false);
            m_prepare_flag &= ~PREPARE_XASL_CACHE_PINNED;
          }

          if (is_savepoint)
          {
            // TODO: db_abort_to_savepoint (savepoint);
          }
        }
      }
    else
      {
        error = execute_internal (info, flag, max_col_size, max_row, bind_values);
      }

    if (error != NO_ERROR)
      {

      }

    return info;
  }

  int 
  query_handler::set_qresult_info (std::vector<query_result_info>& qinfo)
  {
    int error = NO_ERROR;
    int num_q_result = m_q_result.size();
    OID ins_oid;
    DB_OBJECT *ins_obj_p;

    qinfo.resize (num_q_result);
    for (int i = 0; i < num_q_result; i++) 
    {
      query_result& qresult = m_q_result[i];
      memset (&ins_oid, 0, sizeof (OID));

      if (qresult.stmt_type == CUBRID_STMT_INSERT && qresult.result != NULL)
      {
        DB_QUERY_RESULT *qres = (DB_QUERY_RESULT *) qresult.result;
        DB_VALUE val;
        if (qres->type != T_SELECT)
        {
          error = db_query_get_tuple_value ((DB_QUERY_RESULT *) qresult.result, 0, &val);
          if (error >= 0)
          {
            if (DB_VALUE_DOMAIN_TYPE (&val) == DB_TYPE_OBJECT)
            {
              ins_obj_p = db_get_object (&val);
              set_dbobj_to_oid (ins_obj_p, &ins_oid);
              db_value_clear (&val);
            }
            else if (DB_VALUE_DOMAIN_TYPE (&val) == DB_TYPE_SEQUENCE)
            {
              /* result of a GET_GENERATED_KEYS request, client insert */
              DB_VALUE value;
              DB_SEQ *seq = db_get_set (&val);
              if (seq != NULL && db_col_size (seq) == 1)
              {
                db_col_get (seq, 0, &value);
                ins_obj_p = db_get_object (&value);
                set_dbobj_to_oid (ins_obj_p, &ins_oid);
                db_value_clear (&value);
              }
              db_value_clear (&val);
            }
          }
        }
      }
      
      query_result_info& result_info = qinfo[i];
      result_info.ins_oid = ins_oid;
      result_info.stmt_type = qresult.stmt_type;
      result_info.tuple_count = qresult.tuple_count;
    }
    return error;
  }

  int
  query_handler::execute_internal_all (execute_info &info, int flag, int max_col_size, int max_row, std::vector<DB_VALUE> &bind_values)
  {
    int error = NO_ERROR;

    bool recompile = false;
    bool is_first_stmt = true;
    bool is_prepare = m_is_prepared;

    // 1) check is prepared
    int stmt_id, stmt_type;
    if (is_prepare == true)
    {
      stmt_id = m_q_result[0].stmt_id;
      stmt_type = m_q_result[0].stmt_type;
    }
    else
    {
      close_and_free_session ();
      m_session = db_open_buffer (m_sql_stmt.c_str ());
      if (!m_session)
      {
        // TODO: error handling
        error = db_error_code ();
        return error;
        //goto execute_error;
      }
    }

    // 2) bind host variables
    DB_VALUE *value_list = NULL;
    int num_bind = m_num_markers;
    if (num_bind > 0)
    {
      error = make_bind_value (num_bind, &value_list, DB_TYPE_NULL);
      if (error < 0)
      {
        // TODO: error handling
        return error;
      }

	    error = set_host_variables (num_bind, value_list);
      if (error != NO_ERROR)
      {
        // TODO: error handling
        //err_code = ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
        //goto execute_error;
      }
    }

    int q_res_idx = 0;
    db_rewind_statement (m_session);

    // always auto_commit_mode == false
    char savepoint[PATH_MAX];
    bool is_savepoint = false;
    if (db_statement_count (m_session) > 1)
    {
      static unsigned long long savepoint_count = 0;

      // snprintf (savepoint, BROKER_PATH_MAX, "__MSS$%20lld__", savepoint_count++);
      db_savepoint_transaction (savepoint);
      is_savepoint = true;
    }

    while (1)
    {
      if (flag & EXEC_RETURN_GENERATED_KEYS)
        {
    db_session_set_return_generated_keys ((DB_SESSION *) m_session, true);
        }
      else
        {
    db_session_set_return_generated_keys ((DB_SESSION *) m_session, false);
        }

      if (is_prepare == false)
      {
        if (m_prepare_flag & PREPARE_XASL_CACHE_PINNED)
        {
          db_session_set_xasl_cache_pinned (m_session, true, recompile);
        }

        int stmt_id = db_compile_statement (m_session);
        if (stmt_id == 0)
        {
          break;
        }
        else if (stmt_id < 0)
        {
          // TODO: error handling
          error = stmt_id;
          return error;
        }

        stmt_type = db_get_statement_type (m_session, stmt_id);
        if (stmt_type < 0)
        {
          // TODO: error handling
          error = stmt_id;
          return error;
        }
      }

      DB_QUERY_RESULT *result = NULL;
      int n = db_execute_and_keep_statement (m_session, stmt_id, &result);
      if (n < 0)
          {
      // TODO: error handling
      error = n;
          }
      else if (result != NULL)
          {
      /* success; peek the values in tuples */
      (void) db_query_set_copy_tplvalue (result, 0 /* peek */ );
          }

      if (max_row > 0 && db_get_statement_type (m_session, stmt_id) == CUBRID_STMT_SELECT)
        {
    // TODO: max_row
        }

      if (is_first_stmt == true)
      {
        info.num_affected = n;
        q_res_idx = 0;
        m_q_result.clear ();
        is_first_stmt = false;
      }

      m_q_result.resize (m_q_result.size() + 1);

      q_res_idx = m_q_result.size() - 1;

      query_result& qresult = m_q_result[q_res_idx];
      if (m_is_prepared == false)
      {
        qresult.clear ();
        qresult.stmt_id = stmt_id;
        qresult.stmt_type = stmt_type;
      }

      qresult.result = result;
      qresult.tuple_count = n;
      is_prepare = false;

      db_get_cacheinfo (m_session, stmt_id, &m_use_plan_cache, NULL);

      if (has_stmt_result_set (stmt_type))
      {
        m_has_result_set = true;
      }
    }

    if (m_prepare_flag & PREPARE_XASL_CACHE_PINNED)
      {
        db_session_set_xasl_cache_pinned (m_session, false, false);
        m_prepare_flag &= ~PREPARE_XASL_CACHE_PINNED;
      }

      m_max_row = max_row;
      m_max_col_size = max_col_size;
      m_current_result_index = 0;
      m_current_result = &m_q_result[m_current_result_index];
    
    if (value_list)
      {
	for (int i = 0; i < num_bind; i++)
	  {
	    db_value_clear (& (value_list[i]));
	  }
	FREE_MEM (value_list);
      } 
      
    error = set_qresult_info (info.qresult_infos);
    if (error != NO_ERROR)
      {
	// TODO : error handling
	// goto execute_error;
      }

    char include_column_info = 0;
    if (db_check_single_query (m_session) != NO_ERROR) /* ER_IT_MULTIPLE_STATEMENT */
      {
	include_column_info = 1;

	error = set_prepare_column_list_info (info.column_infos, *m_current_result);

	if (error != NO_ERROR)
	  {
	    // TODO : error handling
	    return error;
	  }
      }
    }


  int
  query_handler::execute_internal_call (execute_info &info, int flag, int max_col_size, int max_row, std::vector<DB_VALUE> &bind_values)
  {
    int error = NO_ERROR;

    prepare_call_info call_info = m_prepare_call_info;

    DB_VALUE *value_list = NULL;
    int num_bind = m_num_markers;
    if (num_bind > 0)
      {
	error = make_bind_value (num_bind, &value_list, DB_TYPE_NULL);
	if (error < 0)
	  {
	    // TODO: error handling
	    return error;
	  }

	if (call_info.is_first_out)
	  {
	    error = set_host_variables (num_bind - 1, & (value_list[1]));
	  }
	else
	  {
	    error = set_host_variables (num_bind, value_list);
	  }

	if (error != NO_ERROR)
	  {
	    // TODO: error handling
	    return error;
	  }
      }

    DB_QUERY_RESULT *result = NULL;
    int stmt_id = m_q_result[0].stmt_id;
    int n = db_execute_and_keep_statement (m_session, stmt_id, &result);
    if (n < 0)
      {
	// TODO: error handling
	return n;
      }

    if (result != NULL)
      {
      /* success; copy the values in tuples */
      (void) db_query_set_copy_tplvalue (result, 1 /* copy */ );
      }

      int i = 0;
  if (call_info.is_first_out)
    {
      if (result != NULL)
	{
	  db_query_get_tuple_value (result, 0, &call_info.dbval_args[0]);
	}
      i++;
    }

    DB_VALUE* out_vals = db_get_hostvars (m_session);
      for (int j = 0; i < call_info.num_args; i++, j++)
    {
      call_info.dbval_args[i] = out_vals[j];
    }
    // TODO
  }

  void
  query_handler::end_qresult (bool is_self_free)
  {
    for (int i = 0; i < (int) m_q_result.size(); i++)
      {
	if (m_q_result[i].copied && m_q_result[i].result)
	  {
	    db_query_end (m_q_result[i].result);
	  }
	m_q_result[i].result = NULL;

	if (m_q_result[i].column_info)
	  {
	    db_query_format_free (m_q_result[i].column_info);
	  }
	m_q_result[i].column_info = NULL;
      }

    if (is_self_free)
      {
	m_q_result.clear ();
      }

    m_current_result = NULL;
    m_has_result_set = false;
  }

  int
  query_handler::execute_internal (execute_info &info, int flag, int max_col_size, int max_row, std::vector<DB_VALUE> &bind_values)
  {
    int error = NO_ERROR;

    int stmt_id;
    bool recompile = false;
    // TODO: end_result (false);

    // 1) check is prepared
    if (m_is_prepared == true && m_query_info_flag == false && (flag & EXEC_QUERY_INFO))
      {
	m_is_prepared = false;
	recompile = true;
      }

    if (m_is_prepared == false)
      {
	close_and_free_session ();
	m_session = db_open_buffer (m_sql_stmt.c_str ());
	if (!m_session)
	  {
	    // TODO: error handling
	    error = db_error_code ();
	    return error;
	    //goto execute_error;
	  }
      }

    // 2) bind host variables
    DB_VALUE *value_list = NULL;
    int num_bind = m_num_markers;
    if (num_bind > 0)
      {
	error = make_bind_value (num_bind, &value_list, DB_TYPE_NULL);
	if (error < 0)
	  {
	    // TODO: error handling
	    return error;
	  }

	error = set_host_variables (num_bind, value_list);
	if (error != NO_ERROR)
	  {
	    // TODO: error handling
	    //err_code = ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
	    //goto execute_error;
	  }
      }

    if (flag & EXEC_RETURN_GENERATED_KEYS)
      {
	db_session_set_return_generated_keys ((DB_SESSION *) m_session, true);
      }
    else
      {
	db_session_set_return_generated_keys ((DB_SESSION *) m_session, false);
      }

    if (m_is_prepared == false)
      {
	if (flag & EXEC_QUERY_INFO)
	  {
	    m_query_info_flag = true;
	  }

	if (m_prepare_flag & PREPARE_XASL_CACHE_PINNED)
	  {
	    db_session_set_xasl_cache_pinned (m_session, true, recompile);
	  }

	stmt_id = db_compile_statement (m_session);
	if (stmt_id < 0)
	  {
	    // TODO: error handling
	    error = stmt_id;
	    return error;
	  }
      }
    else
      {
	if (flag & EXEC_ONLY_QUERY_PLAN)
	  {
	    // TODO
	    // set_optimization
	  }
	stmt_id = m_q_result[0].stmt_id;
	// m_stmt_id = srv_handle->q_result->stmt_id;?
      }

    /* no holdable */
    // db_session_set_holdable ((DB_SESSION *) srv_handle->session, srv_handle->is_holdable);
    // srv_handle->is_from_current_transaction = true;

    DB_QUERY_RESULT *result = NULL;
    int n = db_execute_and_keep_statement (m_session, stmt_id, &result);
    if (n < 0)
      {
	// TODO: error handling
	error = n;
      }
    else if (result != NULL)
      {
	/* success; peek the values in tuples */
	(void) db_query_set_copy_tplvalue (result, 0 /* peek */ );
      }

    if (flag & EXEC_QUERY_INFO)
      {
	// TODO: need support?
      }

    if (n < 0)
      {
	// TODO: error handling
      }

    if (max_row > 0 && db_get_statement_type (m_session, stmt_id) == CUBRID_STMT_SELECT)
      {
	// TODO: max_row
      }

    if (m_prepare_flag & PREPARE_XASL_CACHE_PINNED)
      {
	db_session_set_xasl_cache_pinned (m_session, false, false);
	m_prepare_flag &= ~PREPARE_XASL_CACHE_PINNED;
      }

    /*
      if (has_stmt_result_set (srv_handle->q_result->stmt_type) == true)
        {
        srv_handle->has_result_set = true;

        if (srv_handle->is_holdable == true)
        {
        srv_handle->q_result->is_holdable = true;
    #if !defined(LIBCAS_FOR_JSP)
        as_info->num_holdable_results++;
    #endif
        }
        }
    */

    db_get_cacheinfo (m_session, stmt_id, &m_use_plan_cache, NULL);
    error = set_qresult_info (info.qresult_infos);
    if (error != NO_ERROR)
      {
	// TODO : error handling
	// goto execute_error;
      }

    char include_column_info = 0;
    if (db_check_single_query (m_session) != NO_ERROR) /* ER_IT_MULTIPLE_STATEMENT */
      {
	include_column_info = 1;

	error = set_prepare_column_list_info (info.column_infos, *m_current_result);

	if (error != NO_ERROR)
	  {
	    // TODO : error handling
	    return error;
	  }
      }

    if (value_list)
      {
	for (int i = 0; i < num_bind; i++)
	  {
	    db_value_clear (& (value_list[i]));
	  }
	FREE_MEM (value_list);
      }
    return error;
  }

  int
  query_handler::col_get (DB_COLLECTION *col, char col_type, char ele_type, DB_DOMAIN *ele_domain)
  {
    int col_size, i;

    if (col == NULL)
      {
	col_size = -1;
      }
    else
      {
	col_size = db_col_size (col);
      }

    // net_buf_column_info_set

    DB_VALUE ele_val;
    if (col_size > 0)
      {
	for (i = 0; i < col_size; i++)
	  {
	    if (db_col_get (col, i, &ele_val) < 0)
	      {
		db_make_null (&ele_val);
	      }
	    // dbval_to_net_buf (&ele_val, net_buf, 1, 0, 0);
	    // db_value_clear (&ele_val);
	  }
      }
  }

  int
  query_handler::col_size (DB_COLLECTION *col)
  {
    int col_size;
    if (col == NULL)
      {
	col_size = -1;
      }
    else
      {
	col_size = db_col_size (col);
      }

    //net_buf_cp_int (net_buf, 0, NULL);	/* result code */
    //net_buf_cp_int (net_buf, col_size, NULL);	/* result msg */
  }

  int
  query_handler::col_set_drop (DB_COLLECTION *col, DB_VALUE *ele_val)
  {
    if (col != NULL)
      {
	int error = db_set_drop (col, ele_val);
	if (error < 0)
	  {
	    // TODO: error handling
	    return ER_FAILED;
	  }
      }
    return NO_ERROR;
  }

  int
  query_handler::get_generated_keys ()
  {
    // TODO
    int error = NO_ERROR;

    DB_QUERY_RESULT *qres; // TODO = m_result;
    if (qres == NULL)
      {

      }

    if (qres->type == T_SELECT)
      {

      }
    else if (qres->type == T_CALL)
      {

      }

    return error;
  }

  int
  query_handler::check_object (DB_OBJECT *obj)
  {
    // TODO
    int error = NO_ERROR;

    if (obj == NULL)
      {

      }

    er_clear ();
    error = db_is_instance (obj);
    if (error < 0)
      {
	return error;
      }
    else if (error > 0)
      {
	return 0;
      }
    else
      {
	error = db_error_code ();
	if (error < 0)
	  {
	    return error;
	  }

	// return CAS_ER_OBJECT;
      }

    return error;
  }

  bool
  query_handler::has_stmt_result_set (char stmt_type)
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

  int
  query_handler::prepare_query (prepare_info &info, int &flag)
  {
    int error = NO_ERROR;

    m_session = db_open_buffer (m_sql_stmt.c_str());
    if (!m_session)
      {
	// TODO: error handling
	error = db_error_code ();
	return error;
      }

    flag |= (flag & PREPARE_UPDATABLE) ? PREPARE_INCLUDE_OID : 0;
    if (flag & PREPARE_INCLUDE_OID)
      {
	db_include_oid (m_session, DB_ROW_OIDS);
      }

    if (flag & PREPARE_XASL_CACHE_PINNED)
      {
	db_session_set_xasl_cache_pinned (m_session, true, false);
      }

    char stmt_type;
    int num_markers = 0;
    int stmt_id = db_compile_statement (m_session);
    if (stmt_id < 0)
      {
	stmt_type = get_stmt_type (m_sql_stmt);
	if (stmt_id == ER_PT_SEMANTIC && stmt_type != CUBRID_MAX_STMT_TYPE)
	  {
	    close_and_free_session ();
	    num_markers = get_num_markers (m_sql_stmt);
	  }
	else
	  {
	    // TODO: error handling
	    close_and_free_session ();
	    error = stmt_id;
	    return error;
	  }
	m_is_prepared = false;
      }
    else
      {
	num_markers = get_num_markers (m_sql_stmt);
	stmt_type = db_get_statement_type (m_session, stmt_id);
	m_is_prepared = true;
      }

    db_get_cacheinfo (m_session, stmt_id, &m_use_plan_cache, NULL);

    /* prepare result set */
    m_num_markers = num_markers;
    m_prepare_flag = flag;

    query_result q_result;
    q_result.stmt_type = stmt_type;
    q_result.stmt_id = stmt_id;

    m_current_result = NULL;
    m_current_result_index = 0;

    m_q_result.push_back (q_result);

    return error;
  }

  int
  query_handler::prepare_call (prepare_info &info, int &flag)
  {
    int error = NO_ERROR;

    std::string sql_stmt_copy = m_sql_stmt;
    error = m_prepare_call_info.set_is_first_out (sql_stmt_copy);
    if (error != NO_ERROR)
      {
	return error;
      }

    // ut_trim;
    char stmt_type = get_stmt_type (sql_stmt_copy);
    stmt_trim (sql_stmt_copy);
    if (stmt_type != CUBRID_STMT_CALL)
      {
	// TODO: error handling
	error = ER_FAILED;
	return error;
      }

    m_session = db_open_buffer (sql_stmt_copy.c_str());
    if (!m_session)
      {
	// TODO: error handling
	error = ER_FAILED;
	return error;
      }

    int stmt_id = db_compile_statement (m_session);
    if (stmt_id < 0)
      {
	// TODO: error handling
	error = ER_FAILED;
	return error;
      }

    int num_markers = get_num_markers (m_sql_stmt);
    stmt_type = CUBRID_STMT_CALL_SP;
    m_is_prepared = true;
    m_prepare_call_info.set_prepare_call_info (num_markers);

    db_get_cacheinfo (m_session, stmt_id, &m_use_plan_cache, NULL);

    /* prepare result set */
    m_num_markers = num_markers;
    m_prepare_flag = flag;

    query_result q_result;
    q_result.stmt_type = stmt_type;
    q_result.stmt_id = stmt_id;

    m_current_result = NULL;
    m_current_result_index = 0;

    m_q_result.push_back (q_result);

    return error;
  }

  void
  query_handler::close_and_free_session ()
  {
    if (m_session)
      {
	db_close_session ((DB_SESSION *) (m_session));
      }
    m_session = NULL;
  }

  int
  query_handler::set_prepare_column_list_info (std::vector<column_info> &infos, query_result &qresult)
  {
    int error = NO_ERROR;

    qresult.include_oid = false;

    /* TODO
    if (result.null_type_column != NULL)
    {
        free (m_q_result.null_type_column);
        m_q_result.null_type_column = NULL;
    }
    */

    int stmt_id = qresult.stmt_id;
    char stmt_type = qresult.stmt_type;
    if (stmt_type == CUBRID_STMT_SELECT)
      {
	// TODO: updatable
	if (m_prepare_flag)
	  {
	    if (db_query_produce_updatable_result (m_session, stmt_id) <= 0)
	      {
		// TODO: updatable
	      }
	    else
	      {
		qresult.include_oid = true;
	      }
	  }

	DB_QUERY_TYPE *db_column_info = db_get_query_type_list (m_session, stmt_id);
	if (db_column_info == NULL)
	  {
	    // TODO: error handling
	    error = db_error_code ();
	  }

	int num_cols = 0;
	char *col_name = NULL, *class_name = NULL, *attr_name = NULL;


	char set_type;
	int precision = 0;
	short scale = 0;
	char charset;

	DB_QUERY_TYPE *col;
	for (col = db_column_info; col != NULL; col = db_query_format_next (col))
	  {
#if 0
	    // TODO: stripped_column_name
	    if (stripped_column_name)
	      {
		col_name = (char *) db_query_format_name (col);
	      }
	    else
#endif
	      {
		col_name = (char *) db_query_format_original_name (col);
		if (strchr (col_name, '*') != NULL)
		  {
		    col_name = (char *) db_query_format_name (col);
		  }
	      }
	    class_name = (char *) db_query_format_class_name (col);
	    attr_name = (char *) db_query_format_attr_name (col);

	    // TODO: related to updatable flag

	    DB_DOMAIN *domain = db_query_format_domain (col);
	    DB_TYPE db_type = TP_DOMAIN_TYPE (domain);

	    if (TP_IS_SET_TYPE (db_type))
	      {
		// TODO: set type
		set_type = get_set_domain (domain, precision, scale, charset);
	      }
	    else
	      {
		set_type = DB_TYPE_NULL;
		precision = db_domain_precision (domain);
		scale = (short) db_domain_scale (domain);
		charset = db_domain_codeset (domain);
	      }

	    if (db_type == DB_TYPE_NULL)
	      {
		qresult.null_type_column.push_back (1);
	      }
	    else
	      {
		qresult.null_type_column.push_back (0);
	      }

	    column_info info = set_column_info ((int) db_type, set_type, scale, precision, charset, col_name, attr_name, class_name,
						(char) db_query_format_is_non_null (col));
	    infos.push_back (info);
	    num_cols++;
	  }

	qresult.num_column = num_cols;

	// TODO: updatable
	//q_result->col_updatable = updatable_flag;
	//q_result->col_update_info = col_update_info;
	if (db_column_info)
	  {
	    db_query_format_free (db_column_info);
	  }
      }
    else if (stmt_type == CUBRID_STMT_CALL || stmt_type == CUBRID_STMT_GET_STATS || stmt_type == CUBRID_STMT_EVALUATE)
      {
	qresult.null_type_column.push_back (1);
	column_info info; // default constructor
	infos.push_back (info);
      }
    else
      {
	//
      }

    return error;
  }

  column_info
  query_handler::set_column_info (int dbType, int setType, short scale, int prec, char charset, const char *col_name,
				  const char *attr_name,
				  const char *class_name, char is_non_null)
  {
    DB_OBJECT *class_obj = db_find_class (class_name);
    DB_ATTRIBUTE *attr = db_get_attribute (class_obj, col_name);

    char auto_increment = db_attribute_is_auto_increment (attr);
    char unique_key = db_attribute_is_unique (attr);
    char primary_key = db_attribute_is_primary_key (attr);
    char reverse_index = db_attribute_is_reverse_indexed (attr);
    char reverse_unique = db_attribute_is_reverse_unique (attr);
    char foreign_key = db_attribute_is_foreign_key (attr);
    char shared = db_attribute_is_shared (attr);

    std::string col_name_string (col_name);
    std::string attr_name_string (attr_name);
    std::string class_name_string (class_name);

    std::string default_value_string = get_column_default_as_string (attr);

    column_info info (dbType, setType, scale, prec, charset,
		      col_name_string, default_value_string,
		      auto_increment, unique_key, primary_key, reverse_index, reverse_unique, foreign_key, shared,
		      attr_name_string, class_name_string, is_non_null);

    return info;
  }

  int
  query_handler::make_bind_value (int num_bind, DB_VALUE **value_list, DB_TYPE desired_type)
  {
    // value_list = (DB_VALUE *) MALLOC (sizeof (DB_VALUE*) * num_bind);
    // if (value_list == NULL)
    {
      // TODO: error handling
      // return ERROR_INFO_SET (CAS_ER_NO_MORE_MEMORY, CAS_ERROR_INDICATOR);
    }

    return NO_ERROR;
  }

  /*
  * set_host_variables ()
  *
  *   return: error code or NO_ERROR
  *   db_session(in):
  *   num_bind(in):
  *   in_values(in):
  */
  int
  query_handler::set_host_variables (int num_bind, DB_VALUE *in_values)
  {
    int err_code;
    DB_CLASS_MODIFICATION_STATUS cls_status;
    int stmt_id;

    err_code = db_push_values (m_session, num_bind, in_values);
    if (err_code != NO_ERROR)
      {
	int stmt_count = db_statement_count (m_session);
	for (stmt_id = 0; stmt_id < stmt_count; stmt_id++)
	  {
	    cls_status = db_has_modified_class (m_session, stmt_id);
	    if (cls_status == DB_CLASS_MODIFIED)
	      {
		// TODO: error handling
		return err_code;
	      }
	    else if (cls_status == DB_CLASS_ERROR)
	      {
		assert (er_errid () != NO_ERROR);
		err_code = er_errid ();
		if (err_code == NO_ERROR)
		  {
		    err_code = ER_FAILED;
		  }
		// TODO: error handling
		// err_code = ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
		return err_code;
	      }
	  }
	// TODO: error handling
	//err_code = ERROR_INFO_SET (err_code, DBMS_ERROR_INDICATOR);
      }

    return err_code;
  }

  void query_result::clear()
  {
    column_info = NULL;
    result = NULL;
    stmt_id = 0;
    stmt_type = 0;
    num_column = 0;
    tuple_count = 0;

    include_oid = false;
    copied = false;
  }

  void query_handler::set_dbobj_to_oid (DB_OBJECT *obj, OID *oid)
  {
    OID *objs_oid;
    objs_oid = db_identifier (obj);

    assert (oid != NULL);

    if (objs_oid == NULL)
      {
        oid->pageid = 0;
        oid->volid = 0;
        oid->slotid = 0;
      }
    else
      {
        oid->pageid = objs_oid->pageid;
        oid->volid = objs_oid->volid;
        oid->slotid = objs_oid->slotid;
      }
  }

#endif
}