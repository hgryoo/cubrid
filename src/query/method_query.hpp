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

#ifndef _METHOD_QUERY_HPP_
#define _METHOD_QUERY_HPP_

#ident "$Id$"

#include <string>
#include <vector>

#include "dbtype.h"
#include "mem_block.hpp"

namespace cubmethod
{
    #define CUBRID_STMT_CALL_SP	0x7e

    enum PREPARE_FLAG
    {
        PREPARE_INCLUDE_OID = 0x01,
        PREPARE_UPDATABLE = 0x02,
        PREPARE_QUERY_INFO = 0x04,
        PREPARE_HOLDABLE = 0x08,
        PREPARE_XASL_CACHE_PINNED = 0x10,
        PREPARE_CALL = 0x40
    };

    enum EXEC_FLAG
    {
        EXEC_QUERY_ALL = 0x02,
        EXEC_QUERY_INFO = 0x04,
        EXEC_ONLY_QUERY_PLAN = 0x08,
        EXEC_THREAD = 0x10,
        EXEC_RETURN_GENERATED_KEYS = 0x40
    };

    struct query_result
    {
        DB_QUERY_RESULT* result;
        // TODO: column info
        std::vector<char> null_type_column;
        // char col_updatable;
        // T_COL_UPDATE_INFO *m_col_update_info;
        void *column_info;

        int stmt_id;
        char stmt_type;
        int num_column;
        int tuple_count;
        bool include_oid;

        int copied;

        void clear ();
    };

    struct prepare_call_info
    {
        DB_VALUE dbval_ret;
        std::vector<DB_VALUE> dbval_args; /* # of num_args + 1 */
        std::vector<char> param_mode; /* # of num_args */
        bool is_first_out;

        int set_is_first_out (std::string &sql_stmt);
        int set_prepare_call_info (int num_args);
    };

    class query_handler
    {
        public:
            query_handler () = default;

            /* request */
            int prepare (std::string sql, int flag);
            int execute (int flag, int max_col_size, int max_row);
            int get_system_parameter ();

                int oid_get (OID oid);
                int get_generated_keys ();
                int next_result ();

                int col_get (DB_COLLECTION * col, char col_type, char ele_type, DB_DOMAIN * ele_domain);
                int col_size (DB_COLLECTION * col);
                int col_set_drop (DB_COLLECTION * col, DB_VALUE * ele_val);
                
                //int lob_new (int lob_type);
                //int lob_write (DB_VALUE * lob_dbval, int64_t offset, int size,  char *data);
                //int lob_read (DB_VALUE * lob_dbval, int64_t offset, int size);
                int check_object (DB_OBJECT* obj);

            int end_result (bool is_free);

            /* response */
            cubmem::block response_prepare ();

        protected:
            int prepare_query (int& flag);
            int prepare_call (int& flag);

            void close_and_free_session ();

            int make_bind_value (int num_bind, DB_VALUE* value_list, char desired_type);
            int set_host_variables (int num_bind, DB_VALUE* value_list);

            bool has_stmt_result_set (char stmt_type);

            void clear_qresult ();
            int prepare_column_list_info_set ();

            int get_is_first_out ();

        private:
            int64_t id;

            /* prepare info */
            std::string m_sql_stmt;   /* sql statement literal */
            int m_prepare_flag;
            int m_stmt_no;
            STATEMENT_ID m_stmt_id; /* compiled stmt number */
            DB_QUERY_TYPE *columns;	/* columns */
            CUBRID_STMT_TYPE m_stmt_type; /* statement type */
            DB_SESSION *m_session;

            int m_num_markers; /* # of input markers */
            bool m_is_updatable;
            bool m_is_prepared;
            bool m_query_info_flag;
            int m_schema_type;

            bool m_use_plan_cache;

            void *m_current_result;		/* query : &(q_result[cur_result]) schema info : &(session[cursor_pos]) */
            int m_current_result_index;
            
            int m_num_q_result;
            DB_QUERY_RESULT *m_result; /* result column desriptor */
            query_result m_q_result;
            prepare_call_info m_prepare_call_info;

            /* stats */
            int m_num_errors;
    };

}		// namespace cubmethod
#endif				/* _METHOD_QUERY_HPP_ */
