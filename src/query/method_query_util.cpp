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

#include "dbtype.h"

#if !defined(SERVER_MODE)
#include "dbi.h"
#include "object_domain.h"
#endif

namespace cubmethod
{
  void
  stmt_trim (std::string &str)
  {
    str.erase (0, str.find_first_not_of ("\t\n\r "));
    str.erase (str.find_last_not_of ("\t\n\r ") + 1);
  }

  std::string convert_db_value_to_string (DB_VALUE *value, DB_VALUE *value_string)
  {
    const char *val_str = NULL;
    int err, len;

    DB_TYPE val_type = db_value_type (value);

    if (val_type == DB_TYPE_NCHAR || val_type == DB_TYPE_VARNCHAR)
      {
	err = db_value_coerce (value, value_string, db_type_to_db_domain (DB_TYPE_VARNCHAR));
	if (err >= 0)
	  {
	    val_str = db_get_nchar (value_string, &len);
	  }
      }
    else
      {
	err = db_value_coerce (value, value_string, db_type_to_db_domain (DB_TYPE_VARCHAR));
	if (err >= 0)
	  {
	    val_str = db_get_char (value_string, &len);
	  }
      }

    return std::string (val_str);
  }

#if !defined(SERVER_MODE)
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
  std::string
  get_column_default_as_string (DB_ATTRIBUTE *attr)
  {
    int error = NO_ERROR;

    std::string result_default_value_string;
    char *default_value_string = NULL;

    /* Get default value string */
    DB_VALUE *def = db_attribute_default (attr);
    if (def == NULL)
      {
	return default_value_string;
      }

    const char *default_value_expr_type_string = NULL, *default_expr_format = NULL;
    const char *default_value_expr_op_string = NULL;

    default_value_expr_type_string = db_default_expression_string (attr->default_value.default_expr.default_expr_type);
    if (default_value_expr_type_string != NULL)
      {
	/* default expression case */
	int len;

	if (attr->default_value.default_expr.default_expr_op != NULL_DEFAULT_EXPRESSION_OPERATOR)
	  {
	    /* We now accept only T_TO_CHAR for attr->default_value.default_expr.default_expr_op */
	    default_value_expr_op_string = "TO_CHAR";	/* FIXME - remove this hard code */
	  }

	default_expr_format = attr->default_value.default_expr.default_expr_format;
	if (default_value_expr_op_string != NULL)
	  {
	    result_default_value_string.assign (default_value_expr_op_string);
	    result_default_value_string.append ("(");
	    result_default_value_string.append (default_value_expr_type_string);
	    if (default_expr_format)
	      {
		result_default_value_string.append (", \'");
		result_default_value_string.append (default_expr_format);
		result_default_value_string.append ("\'");
	      }
	    result_default_value_string.append (")");
	  }
	else
	  {
	    result_default_value_string.assign (default_value_expr_type_string);
	  }

	return result_default_value_string;
      }

    if (db_value_is_null (def))
      {
	return "NULL";
      }

    /* default value case */
    switch (db_value_type (def))
      {
      case DB_TYPE_UNKNOWN:
	break;
      case DB_TYPE_SET:
      case DB_TYPE_MULTISET:
      case DB_TYPE_SEQUENCE:	/* DB_TYPE_LIST */
	serialize_collection_as_string (def, result_default_value_string);
	break;

      case DB_TYPE_CHAR:
      case DB_TYPE_NCHAR:
      case DB_TYPE_VARCHAR:
      case DB_TYPE_VARNCHAR:
      {
	int def_size = db_get_string_size (def);
	const char *def_str_p = db_get_string (def);
	if (def_str_p)
	  {
	    result_default_value_string.push_back ('\'');
	    result_default_value_string.append (def_str_p);
	    result_default_value_string.push_back ('\'');
	    result_default_value_string.push_back ('\0');
	  }
      }
      break;

      default:
      {
	DB_VALUE tmp_val;
	error = db_value_coerce (def, &tmp_val, db_type_to_db_domain (DB_TYPE_VARCHAR));
	if (error == NO_ERROR)
	  {
	    int def_size = db_get_string_size (&tmp_val);
	    const char *def_str_p = db_get_string (&tmp_val);
	    result_default_value_string.assign (def_str_p);
	  }
	db_value_clear (&tmp_val);
      }
      break;
      }

    return result_default_value_string;
  }

  void
  serialize_collection_as_string (DB_VALUE *col, std::string &out)
  {
    out.clear ();

    if (!TP_IS_SET_TYPE (db_value_type (col)))
      {
	return;
      }

    DB_COLLECTION *db_set = db_get_collection (col);
    int size = db_set_size (db_set);

    /* first compute the size of the result */
    const char *single_value = NULL;
    DB_VALUE value, value_string;

    out.push_back ('{');
    for (int i = 0; i < size; i++)
      {
	if (db_set_get (db_set, i, &value) != NO_ERROR)
	  {
	    out.clear ();
	    return;
	  }

	std::string single_value = convert_db_value_to_string (&value, &value_string);
	out.append (single_value);
	if (i != size - 1)
	  {
	    out.append (", ");
	  }

	db_value_clear (&value_string);
	db_value_clear (&value);
      }
    out.push_back ('}');
  }
#endif
}
