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

#if defined (SERVER_MODE)
#error Does not belong to server module
#endif /* SERVER_MODE */

#include <string>
#include <vector>

#include "dbtype.h"
#include "mem_block.hpp"
#include "method_query_struct.hpp"

namespace cubmethod
{
#define CUBRID_STMT_CALL_SP	0x7e

  /* PREPARE_FLAG, EXEC_FLAG, OID_CMD, SCH_TYPE are ported from cas_cci.h */
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

  enum SCH_TYPE
  {
    SCH_FIRST = 1,
    SCH_CLASS = 1,
    SCH_VCLASS,
    SCH_QUERY_SPEC,
    SCH_ATTRIBUTE,
    SCH_CLASS_ATTRIBUTE,
    SCH_METHOD,
    SCH_CLASS_METHOD,
    SCH_METHOD_FILE,
    SCH_SUPERCLASS,
    SCH_SUBCLASS,
    SCH_CONSTRAINT,
    SCH_TRIGGER,
    SCH_CLASS_PRIVILEGE,
    SCH_ATTR_PRIVILEGE,
    SCH_DIRECT_SUPER_CLASS,
    SCH_PRIMARY_KEY,
    SCH_IMPORTED_KEYS,
    SCH_EXPORTED_KEYS,
    SCH_CROSS_REFERENCE,
    SCH_LAST = SCH_CROSS_REFERENCE
  };

  struct query_result
  {
    DB_QUERY_TYPE *column_info;
    DB_QUERY_RESULT *result;
    // TODO: column info
    std::vector<char> null_type_column;
    // char col_updatable;
    // T_COL_UPDATE_INFO *m_col_update_info;
    // bool is_holdable : not supported
    int stmt_id;
    int stmt_type;
    int num_column;
    int tuple_count;
    bool copied;
    bool include_oid;

    void clear ();
  };

  struct prepare_call_info
  {
    DB_VALUE dbval_ret;
    int num_args;
    std::vector<DB_VALUE> dbval_args; /* # of num_args + 1 */
    std::vector<char> param_mode; /* # of num_args */
    bool is_first_out;

    int set_is_first_out (std::string &sql_stmt);
    int set_prepare_call_info (int num_args);
  };

  struct lob_handle
  {
    int db_type;
    INT64 lob_size;
    int locator_size;
    char *locator;
  };

  class error_context;

  class query_handler
  {
    public:
      query_handler (error_context &ctx, int id);

      /* request */
      prepare_info prepare (std::string sql, int flag);
      execute_info execute (std::vector<DB_VALUE> &bind_values, int flag, int max_col_size, int max_row);
      int get_system_parameter ();

      int collection_cmd (char cmd, OID oid, int seq_index, std::string attr_name);

      int get_generated_keys ();
      int next_result ();

      int col_get (DB_COLLECTION *col, char col_type, char ele_type, DB_DOMAIN *ele_domain);
      int col_size (DB_COLLECTION *col);
      int col_set_drop (DB_COLLECTION *col, DB_VALUE *ele_val);

      int check_object (DB_OBJECT *obj);

      int end_result (bool is_free);

      /* response */
      cubmem::block response_prepare ();

    protected:
      /* prepare */
      int prepare_query (prepare_info &info, int &flag);
      int prepare_call (prepare_info &info, int &flag);

      /* execute */
      int execute_internal (execute_info &info, int flag, int max_col_size, int max_row, std::vector<DB_VALUE> &bind_values);
      int execute_internal_call (execute_info &info, int flag, int max_col_size, int max_row, std::vector<DB_VALUE> &bind_values);
      int execute_internal_all (execute_info &info, int flag, int max_col_size, int max_row, std::vector<DB_VALUE> &bind_values);

      /* qresult */
      int set_qresult_info (std::vector<query_result_info>& qinfo);
      void end_qresult (bool is_self_free);

      /* session */
      void close_and_free_session ();

      /* etc */
      int make_bind_value (int num_bind, DB_VALUE **value_list, DB_TYPE desired_type);
      int set_host_variables (int num_bind, DB_VALUE *value_list);

      bool has_stmt_result_set (char stmt_type);

      void set_dbobj_to_oid (DB_OBJECT *obj, OID *oid);

      int set_prepare_column_list_info (std::vector<column_info> &infos, query_result &result);
      column_info set_column_info (int dbType, int setType, short scale, int prec, char charset, const char *col_name,
				   const char *attr_name,
				   const char *class_name, char is_non_null);

      int get_is_first_out ();

    private:
      int m_id;

      /* stats error, log */
      int m_num_errors;
      error_context &m_error_ctx;

      /* prepare info */
      std::string m_sql_stmt;   /* sql statement literal */
      int m_prepare_flag;
      DB_QUERY_TYPE *columns;	/* columns */
      CUBRID_STMT_TYPE m_stmt_type; /* statement type */
      DB_SESSION *m_session;
      bool m_is_prepared;
      bool m_use_plan_cache;

      /* call */
      prepare_call_info m_prepare_call_info;

      /* execute info */
      int m_num_markers; /* # of input markers */
      int m_max_col_size;
      int m_max_row;

      bool m_has_result_set;
      std::vector<query_result> m_q_result;
      query_result *m_current_result;		/* query : &(q_result[cur_result]) schema info : &(session[cursor_pos]) */
      int m_current_result_index;
      bool m_is_updatable; // TODO: 
      bool m_query_info_flag; // TODO: 

      /* schema info */
      int m_schema_type; // default: -1
  };
}		// namespace cubmethod
#endif				/* _METHOD_QUERY_HPP_ */
