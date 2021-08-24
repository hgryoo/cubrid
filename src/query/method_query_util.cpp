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

#include "method_query_util.hpp"

#include <cstring>

#include "dbtype_def.h"

namespace cubmethod
{
  void
  stmt_trim (std::string &str)
  {
    str.erase (0, str.find_first_not_of ("\t\n\r "));
    str.erase (str.find_last_not_of ("\t\n\r ") + 1);
  }

  char
  get_stmt_type (std::string sql)
  {
    char *stmt = sql.data ();
    if (strncasecmp (stmt, "insert", 6) == 0)
      {
	return CUBRID_STMT_INSERT;
      }
    else if (strncasecmp (stmt, "update", 6) == 0)
      {
	return CUBRID_STMT_UPDATE;
      }
    else if (strncasecmp (stmt, "delete", 6) == 0)
      {
	return CUBRID_STMT_DELETE;
      }
    else if (strncasecmp (stmt, "call", 4) == 0)
      {
	return CUBRID_STMT_CALL;
      }
    else if (strncasecmp (stmt, "evaluate", 8) == 0)
      {
	return CUBRID_STMT_EVALUATE;
      }
    else
      {
	return CUBRID_MAX_STMT_TYPE;
      }
  }

  int
  get_num_markers (std::string sql)
  {
    if (sql.empty())
      {
	return -1;
      }

    int num_markers = 0;
    int sql_len = sql.size ();
    for (int i = 0; i < sql_len; i++)
      {
	if (sql[i] == '?')
	  {
	    num_markers++;
	  }
	else if (sql[i] == '-' && sql[i + 1] == '-')
	  {
	    i = consume_tokens (sql, i + 2, SQL_STYLE_COMMENT);
	  }
	else if (sql[i] == '/' && sql[i + 1] == '*')
	  {
	    i = consume_tokens (sql, i + 2, C_STYLE_COMMENT);
	  }
	else if (sql[i] == '/' && sql[i + 1] == '/')
	  {
	    i = consume_tokens (sql, i + 2, CPP_STYLE_COMMENT);
	  }
	else if (sql[i] == '\'')
	  {
	    i = consume_tokens (sql, i + 1, SINGLE_QUOTED_STRING);
	  }
	else if (/* cas_default_ansi_quotes == false && */ sql[i] == '\"')
	  {
	    i = consume_tokens (sql, i + 1, DOUBLE_QUOTED_STRING);
	  }
      }

    return num_markers;
  }

  int
  consume_tokens (std::string sql, int index, STATEMENT_STATUS stmt_status)
  {
    int sql_len = sql.size ();
    if (stmt_status == SQL_STYLE_COMMENT || stmt_status == CPP_STYLE_COMMENT)
      {
	for (; index < sql_len; index++)
	  {
	    if (sql[index] == '\n')
	      {
		break;
	      }
	  }
      }
    else if (stmt_status == C_STYLE_COMMENT)
      {
	for (; index < sql_len; index++)
	  {
	    if (sql[index] == '*' && sql[index + 1] == '/')
	      {
		index++;
		break;
	      }
	  }
      }
    else if (stmt_status == SINGLE_QUOTED_STRING)
      {
	for (; index < sql_len; index++)
	  {
	    if (sql[index] == '\'' && sql[index + 1] == '\'')
	      {
		index++;
	      }
	    else if (/* cas_default_no_backslash_escapes == false && */ sql[index] == '\\')
	      {
		index++;
	      }
	    else if (sql[index] == '\'')
	      {
		break;
	      }
	  }
      }
    else if (stmt_status == DOUBLE_QUOTED_STRING)
      {
	for (; index < sql_len; index++)
	  {
	    if (sql[index] == '\"' && sql[index + 1] == '\"')
	      {
		index++;
	      }
	    else if (/* cas_default_no_backslash_escapes == false && */ sql[index] == '\\')
	      {
		index++;
	      }
	    else if (sql[index] == '\"')
	      {
		break;
	      }
	  }
      }

    return index;
  }

  
}
