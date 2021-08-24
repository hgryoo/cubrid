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

#if defined (CS_MODE)
#include "dbi.h"
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

        char* tmp = sql_stmt.data ();
        if (!sql_stmt.empty() && sql_stmt[0] == '?')
        {
            is_first_out = true;
            int i;
            for(i = 0; i < sql_stmt.size(); i++)
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
        if (num_args)
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

    int
    query_handler::prepare (std::string sql, int flag)
    {
        int error = NO_ERROR;

        m_sql_stmt = sql;
        m_query_info_flag = (flag & PREPARE_QUERY_INFO) ? true : false;
        m_is_updatable = (flag & PREPARE_UPDATABLE) ? true : false;
        m_schema_type = -1;

        if (flag & PREPARE_CALL)
        {
            error = prepare_call (flag);
        }
        else
        {
            error = prepare_query (flag);
        }

        if (error != NO_ERROR)
        {
            close_and_free_session ();
            m_num_errors++;
        }

        return error;
    }

    #if 0
    int
    query_handler::execute (int flag, int max_col_size, int max_row)
    {
        int error = NO_ERROR;

        int stmt_id;
        bool recompile = false;
        // TODO: end_result (false);
        
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
            // m_stmt_id = srv_handle->q_result->stmt_id;?
        }

        // db_session_set_holdable ((DB_SESSION *) srv_handle->session, srv_handle->is_holdable);
        // srv_handle->is_from_current_transaction = true;
        DB_QUERY_RESULT *result = NULL;
        int n = db_execute_and_keep_statement (m_session, stmt_id, &result);\
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

            // TDOO: error handling
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

        // error = execute_info_set (srv_handle, net_buf, client_version, flag);
        if (error != NO_ERROR)
            {
            // TODO : error handling
            // goto execute_error;
            }

        char include_column_info = 0;
        if (db_check_single_query (m_session) != NO_ERROR) /* ER_IT_MULTIPLE_STATEMENT */
        {
            include_column_info = 1;
            error = prepare_column_list_info_set (/* m_session */);
            if (error != NO_ERROR)
            {
                // TODO : error handling
                return error;
            }
        }

        if (value_list)
            {
            for (i = 0; i < num_bind; i++)
            {
                db_value_clear (&(value_list[i]));
            }
            FREE_MEM (value_list);
        }
        return error;
    }
    #endif

    int
    query_handler::oid_get (OID oid)
    {
        int error = NO_ERROR;

        DB_OBJECT obj;

        error = check_object (&obj);
        if (error < 0)
        {
            // TODO : error handling
        }

        // get attr name
        std::string class_name;
        std::vector<std::string> attr_names;

        const char* cname = db_get_class_name (&obj);
        if (cname != NULL)
        {
            class_name.assign (cname);
        }

        // error = oid_attr_info_set (net_buf, obj, attr_num, attr_name);
        if (error < 0)
        {

        }
        /*
        if (oid_data_set (obj, attr_num, attr_name) < 0)
        {
            
        }
        */

       return error;
    }

    int
    query_handler::col_get (DB_COLLECTION * col, char col_type, char ele_type, DB_DOMAIN * ele_domain)
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
    query_handler::col_size (DB_COLLECTION * col)
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
    query_handler::col_set_drop (DB_COLLECTION * col, DB_VALUE * ele_val)
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

    #if 0
    int 
    query_handler::lob_new (DB_TYPE lob_type)
    {
        int error = NO_ERROR;

        DB_VALUE lob_dbval;
        error = db_create_fbo (&lob_dbval, lob_type);
        if (error < 0)
        {
            // TODO : error handling
            return error;
        }


        // TODO
        db_value_clear (&lob_dbval);
        return error;
    }

    int 
    query_handler::lob_write (DB_VALUE * lob_dbval, int64_t offset, int size, char *data)
    {
        int error = NO_ERROR;

        DB_ELO *elo = db_get_elo (lob_dbval);
        DB_BIGINT size_written;
        error = db_elo_write (elo, offset, data, size, &size_written);
        if (error < 0)
        {
            // TODO : error handling
            return error;
        }

        /* set result: on success, bytes written */
        // net_buf_cp_int (net_buf, (int) size_written, NULL);

        return error;
    }

    int 
    query_handler::lob_read (DB_TYPE lob_type)
    {
        int error = NO_ERROR;
        // TODO

        DB_ELO *elo = db_get_elo (lob_dbval);

        DB_BIGINT size_read;
        error = db_elo_read (elo, offset, data, size, &size_read);
        if (error < 0)
        {
            // TODO : error handling
            return error;
        }

        /* set result: on success, bytes read */
        // net_buf_cp_int (net_buf, (int) size_read, NULL);
        // net_buf->data_size += (int) size_read;

        return error;
    }
    #endif

    int
    query_handler::get_generated_keys ()
    {
        // TODO
        int error = NO_ERROR;

        DB_QUERY_RESULT *qres = m_result;
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
    query_handler::check_object (DB_OBJECT * obj)
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
    query_handler::prepare_query (int& flag)
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
        
        int num_markers = 0;
        char stmt_type;

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

        /* prepare result set */
        m_num_markers = num_markers;
        m_prepare_flag = flag;

        clear_qresult ();
        m_q_result.stmt_type = m_stmt_type;
        m_q_result.stmt_id = stmt_id;
        error = prepare_column_list_info_set ();
        if (error < 0)
        {
            // TODO: error handling
            close_and_free_session ();
            return error;
        }

        m_num_q_result = 1;
        m_current_result = NULL;
        m_current_result_index = 0;

        db_get_cacheinfo (m_session, stmt_id, &m_use_plan_cache, NULL);
    }

    void
    query_handler::clear_qresult ()
    {
        // memset (m_q_result, 0, sizeof (query_result));
    }

    int
    query_handler::prepare_call (int& flag)
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

        /* prepare result set */
        m_num_markers = num_markers;
        m_prepare_flag = flag;

        clear_qresult ();
        m_q_result.stmt_type = m_stmt_type;
        m_q_result.stmt_id = stmt_id;
        error = prepare_column_list_info_set ();
        if (error < 0)
        {
            // TODO: error handling
            close_and_free_session ();
            return error;
        }

        m_num_q_result = 1;
        m_current_result = NULL;
        m_current_result_index = 0;

        db_get_cacheinfo (m_session, stmt_id, &m_use_plan_cache, NULL);

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
    query_handler::prepare_column_list_info_set ()
    {
        int error = NO_ERROR;

        m_q_result.include_oid = false;

        /* TODO
        if (m_q_result.null_type_column != NULL)
        {
            free (m_q_result.null_type_column);
            m_q_result.null_type_column = NULL;
        }
        */

        int stmt_id = m_q_result.stmt_id;
        char stmt_type = m_q_result.stmt_type;
        if (stmt_type == CUBRID_STMT_SELECT)
        {
            // TODO: updatable

            if (m_prepare_flag)
            {
                if (db_query_produce_updatable_result (m_session, stmt_id) <= 0)
                {
                    // TODO
                }
                else
                {
                    m_q_result.include_oid = true;
                }
            }

            DB_QUERY_TYPE *column_info = db_get_query_type_list (m_session, stmt_id);
            if (column_info == NULL)
            {
                // TODO: error handling
                error = db_error_code ();
            }

            int num_cols = 0;
            char *col_name = NULL, *class_name = NULL, *attr_name = NULL;

            DB_QUERY_TYPE *col;
            for (col = column_info; col != NULL; col = db_query_format_next (col))
            {
                // TODO: stripped_column_name
                col_name = (char *) db_query_format_original_name (col);
                if (strchr (col_name, '*') != NULL)
                {
                    col_name = (char *) db_query_format_name (col);
                }
                class_name = (char *) db_query_format_class_name (col);
                attr_name = (char *) db_query_format_attr_name (col);

                // TODO: related to updatable flag

                DB_DOMAIN* domain = db_query_format_domain (col);
                DB_TYPE db_type = TP_DOMAIN_TYPE (domain);

                char set_type;
                int precision;
                int scale;
                char charset;

                if (TP_IS_SET_TYPE (db_type))
                {
                    // TODO
                    // set_type = get_set_domain (domain, NULL, NULL, NULL, &charset);
                    precision = 0;
	                scale = 0;
                }
                else
                {
                    precision = db_domain_precision (domain);
                    scale = (short) db_domain_scale (domain);
                    charset = db_domain_codeset (domain);
                }

                if (db_type == DB_TYPE_NULL)
                {
                    m_q_result.null_type_column.push_back (1);
                }
                else
                {
                    m_q_result.null_type_column.push_back (0);
                }

                // TODO: set_column_info

                num_cols++;
            }

            m_q_result.num_column = num_cols;
            //q_result->col_updatable = updatable_flag;
            //q_result->col_update_info = col_update_info;
            if (column_info)
            {
                db_query_format_free (column_info);
            }
        }
        else if (stmt_type == CUBRID_STMT_CALL || stmt_type == CUBRID_STMT_GET_STATS || stmt_type == CUBRID_STMT_EVALUATE)
        {
            // q_result->null_type_column[0] = 1;
            m_q_result.null_type_column.push_back (1);
            // TODO: prepare_column_info_set
        }
        else
        {

        }

        return error;
    }
    #endif
}