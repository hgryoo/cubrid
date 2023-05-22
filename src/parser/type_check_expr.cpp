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

/*
 * type_check_expr.cpp
 */

#include "type_check_expr.hpp"

#include "type_check.h"
#include "message_catalog.h"
#include "object_primitive.h"
#include "parse_tree.h"
#include "parser.h"
#include "parser_message.h"
#include "dbtype_function.h"
#include "semantic_check.h"

static constexpr COMPARE_BETWEEN_OPERATOR pt_Compare_between_operator_table[] =
{
  {PT_GE, PT_LE, PT_BETWEEN_GE_LE},
  {PT_GE, PT_LT, PT_BETWEEN_GE_LT},
  {PT_GT, PT_LE, PT_BETWEEN_GT_LE},
  {PT_GT, PT_LT, PT_BETWEEN_GT_LT},
  {PT_EQ, PT_EQ, PT_BETWEEN_EQ_NA},
  {PT_GT_INF, PT_LE, PT_BETWEEN_INF_LE},
  {PT_GT_INF, PT_EQ, PT_BETWEEN_INF_LE},
  {PT_GT_INF, PT_LT, PT_BETWEEN_INF_LT},
  {PT_GE, PT_LT_INF, PT_BETWEEN_GE_INF},
  {PT_EQ, PT_LT_INF, PT_BETWEEN_GE_INF},
  {PT_GT, PT_LT_INF, PT_BETWEEN_GT_INF}
};

constexpr int COMPARE_BETWEEN_OPERATOR_COUNT {sizeof (pt_Compare_between_operator_table)/sizeof (COMPARE_BETWEEN_OPERATOR)};

/*
 * pt_get_expression_definition () - get the expression definition for the
 *				     expression op.
 *   return: true if the expression has a definition, false otherwise
 *   op(in)	: the expression operator
 *   def(in/out): the expression definition
 */
bool
pt_get_expression_definition (const PT_OP_TYPE op, expression_definitions &def)
{
  expr_all_signatures sigs;

  // examples
  //                                        return,           arg1,            arg2,            GET_ARG3 ()
  // expression_signature sigs_arg0 = {PT_TYPE_LOGICAL};
  // expression_signature sigs_arg1 = {PT_TYPE_LOGICAL, PT_TYPE_LOGICAL};
  // expression_signature sigs_arg2 = {PT_TYPE_LOGICAL, PT_TYPE_LOGICAL, PT_TYPE_LOGICAL};
  // expression_signature sigs_arg3 = {PT_TYPE_LOGICAL, PT_TYPE_LOGICAL, PT_TYPE_LOGICAL, PT_TYPE_LOGICAL};

  switch (op)
    {
    case PT_AND:
    case PT_OR:
    case PT_XOR:
      sigs =
      {
	{PT_TYPE_LOGICAL, PT_TYPE_LOGICAL, PT_TYPE_LOGICAL},
      };
      break;

    case PT_NOT:
      sigs =
      {
	{PT_TYPE_LOGICAL, PT_TYPE_LOGICAL},
      };
      break;

    case PT_ACOS:
    case PT_ASIN:
    case PT_ATAN:
    case PT_COS:
    case PT_COT:
    case PT_DEGREES:
    case PT_EXP:
    case PT_LN:
    case PT_LOG10:
    case PT_LOG2:
    case PT_SQRT:
    case PT_RADIANS:
    case PT_SIN:
    case PT_TAN:
      sigs =
      {
	{PT_TYPE_DOUBLE, PT_TYPE_DOUBLE},
      };
      break;

    case PT_ABS:
      sigs =
      {
	{PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER},
      };
      break;

    case PT_ATAN2:
    case PT_LOG:
    case PT_POWER:
      sigs =
      {
	{PT_TYPE_DOUBLE, PT_TYPE_DOUBLE, PT_TYPE_DOUBLE},
      };
      break;

    case PT_RAND:
    case PT_RANDOM:
      sigs =
      {
	{PT_TYPE_INTEGER, PT_TYPE_NONE},
	{PT_TYPE_INTEGER, PT_TYPE_INTEGER},
      };
      break;

    case PT_DRAND:
    case PT_DRANDOM:
      sigs =
      {
	{PT_TYPE_DOUBLE, PT_TYPE_NONE},
	{PT_TYPE_DOUBLE, PT_TYPE_INTEGER},
      };
      break;

    case PT_BIT_AND:
    case PT_BIT_XOR:
    case PT_BIT_OR:
    case PT_BITSHIFT_LEFT:
    case PT_BITSHIFT_RIGHT:
      sigs =
      {
	{PT_TYPE_BIGINT, PT_TYPE_BIGINT, PT_TYPE_BIGINT},
      };
      break;

    case PT_BIT_LENGTH:
    case PT_OCTET_LENGTH:
      sigs =
      {
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_BIT},
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_STRING},
      };
      break;

    case PT_BIT_COUNT:
      sigs =
      {
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_NUMBER},
      };
      break;

    case PT_BIT_NOT:
      sigs =
      {
	{PT_TYPE_BIGINT, PT_TYPE_BIGINT},
      };
      break;

    case PT_BETWEEN:
    case PT_NOT_BETWEEN:
      sigs =
      {
	{PT_TYPE_LOGICAL, PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_ANY},
      };
      break;

    case PT_LIKE:
    case PT_NOT_LIKE:
      sigs =
      {
	{PT_TYPE_LOGICAL, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR}, /* BOOL PT_LIKE([VAR]CHAR, [VAR]CHAR); */
	{PT_TYPE_LOGICAL, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR}, /* BOOL PT_LIKE([VAR]NCHAR, [VAR]NCHAR); */
	{PT_TYPE_LOGICAL, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR}, /* BOOL PT_LIKE([VAR]CHAR, [VAR]CHAR, [VAR]CHAR); */
	{PT_TYPE_LOGICAL, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR}, /* BOOL PT_LIKE([VAR]NCHAR, [VAR]NCHAR, [VAR]NCHAR); */
      };
      break;

    case PT_RLIKE:
    case PT_NOT_RLIKE:
    case PT_RLIKE_BINARY:
    case PT_NOT_RLIKE_BINARY:
      sigs =
      {
	{PT_TYPE_LOGICAL, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR, PT_TYPE_INTEGER}, /* BOOL PT_RLIKE([VAR]CHAR, [VAR]CHAR, INT); */
	{PT_TYPE_LOGICAL, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR, PT_TYPE_INTEGER}, /* BOOL PT_RLIKE([VAR]NCHAR, [VAR]NCHAR, INT); */
      };
      break;

    case PT_CEIL:
    case PT_FLOOR:
      sigs =
      {
	{PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER},
      };
      break;

    case PT_CHR:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_NUMBER, PT_TYPE_INTEGER},
      };
      break;

    case PT_CHAR_LENGTH:
      sigs =
      {
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_STRING},
      };
      break;

    case PT_REPEAT:
      sigs =
      {
	{PT_GENERIC_TYPE_STRING_VARYING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER},
      };
      break;

    case PT_ADD_MONTHS:
      sigs =
      {
	{PT_TYPE_DATE, PT_TYPE_DATE, PT_TYPE_INTEGER},
      };
      break;

    case PT_FROMDAYS:
      sigs =
      {
	{PT_TYPE_DATE, PT_GENERIC_TYPE_STRING_VARYING},
	{PT_TYPE_DATE, PT_TYPE_INTEGER},
      };
      break;

    case PT_LOWER:
    case PT_UPPER:
      sigs =
      {
	{PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING},
      };
      break;

    case PT_HEX:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_STRING}, /* HEX (STRING) */
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_NUMBER}, /* HEX (NUMBER) */
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_BIT},    /* HEX (BIT) */
      };
      break;

    case PT_ASCII:
      sigs =
      {
	{PT_TYPE_SMALLINT, PT_GENERIC_TYPE_STRING}, /* ASCII (STRING) */
	{PT_TYPE_SMALLINT, PT_GENERIC_TYPE_BIT}, /* ASCII (BIT) */
      };
      break;

    case PT_CONV:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_NUMBER, PT_TYPE_SMALLINT, PT_TYPE_SMALLINT}, /* CONV(NUMBER, SMALLINT, SMALLINT) */
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_STRING, PT_TYPE_SMALLINT, PT_TYPE_SMALLINT}, /* CONV(VARCHAR, SMALLINT, SMALLINT) */
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_BIT, PT_TYPE_SMALLINT, PT_TYPE_SMALLINT}, /* CONV(BIT, SMALLINT, SMALLINT) */
      };
      break;

    case PT_DATEF:
    case PT_REVERSE:
      sigs =
      {
	{PT_GENERIC_TYPE_STRING_VARYING, PT_GENERIC_TYPE_STRING},
      };
      break;

    case PT_DISK_SIZE:
      sigs =
      {
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_ANY},
      };
      break;

    case PT_LIKE_LOWER_BOUND:
    case PT_LIKE_UPPER_BOUND:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_CHAR},
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR},
      };
      break;

    case PT_BIN:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_TYPE_BIGINT},
      };
      break;

    case PT_ADDTIME:
      sigs =
      {
	{PT_TYPE_DATETIME, PT_TYPE_DATETIME, PT_TYPE_TIME},
	{PT_TYPE_DATETIMELTZ, PT_TYPE_DATETIMELTZ, PT_TYPE_TIME},
	{PT_TYPE_DATETIMETZ, PT_TYPE_DATETIMETZ, PT_TYPE_TIME},
	{PT_TYPE_DATETIME, PT_TYPE_TIMESTAMP, PT_TYPE_TIME},
	{PT_TYPE_DATETIMELTZ, PT_TYPE_TIMESTAMPLTZ, PT_TYPE_TIME},
	{PT_TYPE_DATETIMETZ, PT_TYPE_TIMESTAMPTZ, PT_TYPE_TIME},

	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING},
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_STRING, PT_TYPE_TIME},

	{PT_TYPE_DATETIME, PT_TYPE_DATE, PT_TYPE_TIME},
	{PT_TYPE_TIME, PT_TYPE_TIME, PT_TYPE_TIME},
	{PT_TYPE_TIME, PT_TYPE_TIME, PT_GENERIC_TYPE_STRING},
	{PT_TYPE_MAYBE, PT_TYPE_MAYBE, PT_TYPE_TIME},
      };
      break;

    case PT_TRIM:
    case PT_LTRIM:
    case PT_RTRIM:
      sigs =
      {
	{PT_GENERIC_TYPE_STRING_VARYING, PT_GENERIC_TYPE_STRING},
	{PT_GENERIC_TYPE_STRING_VARYING, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING},
      };
      break;

    case PT_MAKEDATE:
      sigs =
      {
	{PT_TYPE_DATE, PT_GENERIC_TYPE_STRING_VARYING, PT_GENERIC_TYPE_STRING_VARYING},
	{PT_TYPE_DATE, PT_TYPE_INTEGER, PT_TYPE_INTEGER},
      };
      break;

    case PT_MAKETIME:
      sigs =
      {
	{PT_TYPE_TIME, PT_GENERIC_TYPE_STRING_VARYING, PT_GENERIC_TYPE_STRING_VARYING, PT_GENERIC_TYPE_STRING_VARYING},
	{PT_TYPE_TIME, PT_TYPE_INTEGER, PT_TYPE_INTEGER, PT_TYPE_INTEGER},
      };
      break;

    case PT_SECTOTIME:
      sigs =
      {
	{PT_TYPE_TIME, PT_GENERIC_TYPE_STRING_VARYING},
	{PT_TYPE_TIME, PT_TYPE_INTEGER},
      };
      break;

    case PT_YEARF:
    case PT_DAYF:
    case PT_MONTHF:
    case PT_DAYOFMONTH:
    case PT_DAYOFWEEK:
    case PT_DAYOFYEAR:
    case PT_QUARTERF:
    case PT_TODAYS:
    case PT_WEEKDAY:
      sigs =
      {
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_STRING_VARYING},
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_DATE},
      };
      break;

    case PT_LAST_DAY:
      sigs =
      {
	{PT_TYPE_DATE, PT_TYPE_DATE},
      };
      break;

    case PT_CONCAT:
    case PT_SYS_CONNECT_BY_PATH:
      sigs =
      {
	{PT_GENERIC_TYPE_STRING_VARYING, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR},
	{PT_GENERIC_TYPE_STRING_VARYING, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR},
      };
      break;

    case PT_CONCAT_WS:
      sigs =
      {
	{PT_GENERIC_TYPE_STRING_VARYING, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR},
	{PT_GENERIC_TYPE_STRING_VARYING, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR},
      };
      break;

    case PT_DATABASE:
    case PT_SCHEMA:
    case PT_VERSION:
    case PT_CURRENT_USER:
    case PT_LIST_DBS:
    case PT_SYS_GUID:
    case PT_USER:
      sigs =
      {
	{PT_TYPE_VARCHAR},
      };
      break;

    case PT_LOCAL_TRANSACTION_ID:
      sigs =
      {
	{PT_TYPE_INTEGER},
      };
      break;

    case PT_CURRENT_VALUE:
      sigs =
      {
	{PT_TYPE_NUMERIC, PT_GENERIC_TYPE_STRING},
      };
      break;

    case PT_NEXT_VALUE:
      sigs =
      {
	{PT_TYPE_NUMERIC, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER},
      };
      break;

    case PT_DATE_FORMAT:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER},
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_DATETIME, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER},
      };
      break;

    case PT_DIV:
    case PT_MOD:
      sigs =
      {
	{PT_GENERIC_TYPE_DISCRETE_NUMBER, PT_GENERIC_TYPE_DISCRETE_NUMBER, PT_GENERIC_TYPE_DISCRETE_NUMBER},
      };
      break;

    case PT_DIVIDE:
    case PT_MODULUS:
      sigs =
      {
	{PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER},
      };
      break;

    case PT_TIMES:
      sigs =
      {
	{PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER},
	{PT_TYPE_MULTISET, PT_GENERIC_TYPE_SEQUENCE, PT_GENERIC_TYPE_SEQUENCE},
      };
      break;

    case PT_PLUS:
      sigs =
      {
	{PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER}, /* number + number */
	{PT_GENERIC_TYPE_SEQUENCE, PT_GENERIC_TYPE_SEQUENCE, PT_GENERIC_TYPE_SEQUENCE}, /* collection + collection */
      };

      if (prm_get_bool_value (PRM_ID_PLUS_AS_CONCAT))
	{
	  sigs.push_back ({PT_GENERIC_TYPE_STRING_VARYING, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR}); /* char + char */
	  sigs.push_back ({PT_GENERIC_TYPE_STRING_VARYING, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR}); /* nchar + nchar */
	  sigs.push_back ({PT_GENERIC_TYPE_BIT, PT_GENERIC_TYPE_BIT, PT_GENERIC_TYPE_BIT}); /* bit + bit */
	}
      break;

    case PT_MINUS:
      sigs =
      {
	{PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER},
	{PT_TYPE_BIGINT, PT_GENERIC_TYPE_DATE, PT_GENERIC_TYPE_DATE},
	{PT_TYPE_BIGINT, PT_TYPE_TIME, PT_TYPE_TIME},
	{PT_GENERIC_TYPE_SEQUENCE, PT_GENERIC_TYPE_SEQUENCE, PT_GENERIC_TYPE_SEQUENCE},
      };
      break;

    case PT_HOURF:
    case PT_MINUTEF:
    case PT_SECONDF:
    case PT_TIMETOSEC:
      sigs =
      {
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_STRING_VARYING},
	{PT_TYPE_INTEGER, PT_TYPE_TIME},
      };
      break;

    case PT_INSTR:
      sigs =
      {
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR, PT_TYPE_INTEGER},
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR, PT_TYPE_INTEGER},
      };
      break;

    case PT_LEFT:
    case PT_RIGHT:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_CHAR, PT_TYPE_INTEGER},
	{PT_TYPE_VARNCHAR, PT_GENERIC_TYPE_NCHAR, PT_TYPE_INTEGER},
      };
      break;

    case PT_LOCATE:
      sigs =
      {
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR, PT_TYPE_NONE},
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR, PT_TYPE_NONE},
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR, PT_TYPE_INTEGER},
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR, PT_TYPE_INTEGER},
      };
      break;

    case PT_POSITION:
    case PT_STRCMP:
    case PT_FINDINSET:
      sigs =
      {
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR},
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR},
      };
      break;

    case PT_SUBSTRING_INDEX:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR, PT_TYPE_INTEGER},
	{PT_TYPE_VARNCHAR, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR, PT_TYPE_INTEGER},
      };
      break;

    case PT_LPAD:
    case PT_RPAD:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_CHAR, PT_TYPE_INTEGER, PT_GENERIC_TYPE_CHAR},
	{PT_TYPE_VARNCHAR, PT_GENERIC_TYPE_NCHAR, PT_TYPE_INTEGER, PT_GENERIC_TYPE_NCHAR},
      };
      break;

    case PT_MD5:
    case PT_SHA_ONE:
      sigs =
      {
	{PT_TYPE_CHAR, PT_GENERIC_TYPE_STRING},
      };
      break;

    case PT_SHA_TWO:
      sigs =
      {
	{PT_TYPE_CHAR, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_DISCRETE_NUMBER},
      };
      break;

    case PT_AES_ENCRYPT:
    case PT_AES_DECRYPT:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING},
      };
      break;

    case PT_TO_BASE64:
    case PT_FROM_BASE64:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_STRING},
      };
      break;

    case PT_MID:
      sigs =
      {
	{PT_GENERIC_TYPE_STRING_VARYING, PT_GENERIC_TYPE_STRING,PT_TYPE_INTEGER,PT_TYPE_INTEGER},
      };
      break;

    case PT_SUBSTRING:
      sigs =
      {
	{PT_GENERIC_TYPE_STRING_VARYING, PT_GENERIC_TYPE_STRING,PT_TYPE_INTEGER,PT_TYPE_NONE},  /* SUBSTRING (string, int) */
	{PT_GENERIC_TYPE_STRING_VARYING, PT_GENERIC_TYPE_STRING,PT_TYPE_INTEGER,PT_TYPE_INTEGER},  /* SUBSTRING (string, int, int) */
      };
      break;

    case PT_MONTHS_BETWEEN:
      sigs =
      {
	{PT_TYPE_DOUBLE, PT_TYPE_DATE,PT_TYPE_DATE},
      };
      break;

    case PT_PI:
      sigs =
      {
	{PT_TYPE_DOUBLE},
      };
      break;

    case PT_REPLACE:
    case PT_TRANSLATE:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR,PT_GENERIC_TYPE_CHAR},
	{PT_TYPE_VARNCHAR, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR,PT_GENERIC_TYPE_NCHAR},
      };
      break;

    case PT_SPACE:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_DISCRETE_NUMBER},
      };
      break;

    case PT_STRCAT:
      sigs =
      {
	{PT_GENERIC_TYPE_STRING_VARYING, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR},
	{PT_GENERIC_TYPE_STRING_VARYING, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR},
      };
      break;

    case PT_UTC_DATE:
    case PT_SYS_DATE:
    case PT_CURRENT_DATE:
      sigs =
      {
	{PT_TYPE_DATE},
      };
      break;

    case PT_SYS_DATETIME:
    case PT_CURRENT_DATETIME:
      sigs =
      {
	{PT_TYPE_DATETIME},
      };
      break;

    case PT_UTC_TIME:
    case PT_SYS_TIME:
    case PT_CURRENT_TIME:
      sigs =
      {
	{PT_TYPE_TIME},
      };
      break;

    case PT_SYS_TIMESTAMP:
    case PT_UTC_TIMESTAMP:
    case PT_CURRENT_TIMESTAMP:
      sigs =
      {
	{PT_TYPE_TIMESTAMP},
      };
      break;

    case PT_TIME_FORMAT:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_DATETIME, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER},
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER},
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER},
      };
      break;

    case PT_TIMEF:
      sigs =
      {
	{PT_GENERIC_TYPE_STRING_VARYING, PT_GENERIC_TYPE_STRING},
	{PT_TYPE_VARCHAR, PT_TYPE_TIME},
	{PT_TYPE_VARCHAR, PT_TYPE_DATETIME},
	{PT_TYPE_VARCHAR, PT_TYPE_TIMESTAMP},
      };
      break;

    case PT_TO_DATE:
      sigs =
      {
	{PT_TYPE_DATE, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER},
      };
      break;

    case PT_TO_DATETIME:
      sigs =
      {
	{PT_TYPE_DATETIME, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR, PT_TYPE_INTEGER},
	{PT_TYPE_DATETIME, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR, PT_TYPE_INTEGER},
      };
      break;

    case PT_TO_TIME:
      sigs =
      {
	{PT_TYPE_TIME, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR, PT_TYPE_INTEGER},
	{PT_TYPE_TIME, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR, PT_TYPE_INTEGER},
      };
      break;

    case PT_TO_TIMESTAMP:
      sigs =
      {
	{PT_TYPE_TIMESTAMP, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR, PT_TYPE_INTEGER},
	{PT_TYPE_TIMESTAMP, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR, PT_TYPE_INTEGER},
      };
      break;

    case PT_TO_NUMBER:
      sigs =
      {
	{PT_TYPE_NUMERIC, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER},
      };
      break;

    case PT_WEEKF:
      sigs =
      {
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_STRING_VARYING, PT_GENERIC_TYPE_STRING_VARYING},
	{PT_TYPE_INTEGER, PT_TYPE_DATE, PT_TYPE_INTEGER},
      };
      break;

    case PT_CLOB_LENGTH:
      sigs =
      {
	{PT_TYPE_BIGINT, PT_TYPE_CLOB},
      };
      break;

    case PT_BLOB_LENGTH:
      sigs =
      {
	{PT_TYPE_BIGINT, PT_TYPE_BLOB},
      };
      break;

    case PT_BIT_TO_BLOB:
      sigs =
      {
	{PT_TYPE_BLOB, PT_GENERIC_TYPE_BIT},
      };
      break;

    case PT_CHAR_TO_CLOB:
      sigs =
      {
	{PT_TYPE_CLOB, PT_GENERIC_TYPE_STRING},
      };
      break;

    case PT_CHAR_TO_BLOB:
    case PT_BLOB_FROM_FILE:
      sigs =
      {
	{PT_TYPE_BLOB, PT_GENERIC_TYPE_STRING},
      };
      break;

    case PT_CLOB_FROM_FILE:
      sigs =
      {
	{PT_TYPE_CLOB, PT_GENERIC_TYPE_STRING},
      };
      break;

    case PT_BLOB_TO_BIT:
      sigs =
      {
	{PT_TYPE_VARBIT, PT_TYPE_BLOB},
      };
      break;

    case PT_CLOB_TO_CHAR:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_TYPE_CLOB, PT_TYPE_INTEGER},
      };
      break;

    case PT_INST_NUM:
    case PT_ROWNUM:
    case PT_ORDERBY_NUM:
      sigs =
      {
	{PT_TYPE_BIGINT},
      };
      break;

    case PT_LEVEL:
    case PT_CONNECT_BY_ISCYCLE:
    case PT_CONNECT_BY_ISLEAF:
    case PT_ROW_COUNT:
      sigs =
      {
	{PT_TYPE_INTEGER},
      };
      break;

    case PT_LAST_INSERT_ID:
      sigs =
      {
	{PT_TYPE_NUMERIC},
      };
      break;

    case PT_DATEDIFF:
      sigs =
      {
	{PT_TYPE_INTEGER, PT_TYPE_DATE, PT_TYPE_DATE},
      };
      break;

    case PT_TIMEDIFF:
      sigs =
      {
	{PT_TYPE_TIME, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING},
	{PT_TYPE_TIME, PT_GENERIC_TYPE_DATE, PT_GENERIC_TYPE_DATE},
	{PT_TYPE_TIME, PT_TYPE_TIME, PT_TYPE_TIME},
      };
      break;

    case PT_INCR:
    case PT_DECR:
      sigs =
      {
	{PT_GENERIC_TYPE_DISCRETE_NUMBER, PT_GENERIC_TYPE_DISCRETE_NUMBER},
	{PT_GENERIC_TYPE_DISCRETE_NUMBER, PT_GENERIC_TYPE_DISCRETE_NUMBER, PT_TYPE_OBJECT, PT_GENERIC_TYPE_DISCRETE_NUMBER},
      };
      break;

    case PT_FORMAT:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_DISCRETE_NUMBER, PT_TYPE_INTEGER},
      };
      break;

    case PT_ROUND:
      sigs =
      {
	{PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER}, /* first overload for number: */
	{PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_STRING}, /* overload for round('123', '1') */
	{PT_TYPE_DATE, PT_TYPE_DATE, PT_GENERIC_TYPE_STRING}, /* overload for round(date, 'year|month|day') */
	{PT_TYPE_DATE, PT_TYPE_DATETIME, PT_GENERIC_TYPE_STRING}, /* overload for round(datetime, 'year|month|day') */
	{PT_TYPE_DATE, PT_TYPE_TIMESTAMP, PT_GENERIC_TYPE_STRING}, /* overload for round(timestamp, 'year|month|day') */
	{PT_TYPE_DATE, PT_TYPE_TIMESTAMPTZ, PT_GENERIC_TYPE_STRING},  /* overload for round(timestamptz, 'year|month|day') */
	{PT_TYPE_DATE, PT_TYPE_TIMESTAMPLTZ, PT_GENERIC_TYPE_STRING}, /* overload for round(timestampltz, 'year|month|day') */
	{PT_TYPE_DATE, PT_TYPE_DATETIMETZ, PT_GENERIC_TYPE_STRING}, /* overload for round(datetimetz, 'year|month|day') */
	{PT_TYPE_DATE, PT_TYPE_DATETIMELTZ, PT_GENERIC_TYPE_STRING}, /* overload for round(datetimeltz, 'year|month|day') */
      };
      break;

    case PT_TRUNC:
      sigs =
      {
	{PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER}, /* number types */
	{PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_STRING}, /* number types 2 */
	{PT_TYPE_DATE, PT_TYPE_DATE, PT_GENERIC_TYPE_STRING}, /* date */
	{PT_TYPE_DATE, PT_TYPE_DATETIME, PT_GENERIC_TYPE_STRING}, /* datetime */
	{PT_TYPE_DATE, PT_TYPE_TIMESTAMP, PT_GENERIC_TYPE_STRING}, /* timestamp */
	{PT_TYPE_DATE, PT_TYPE_DATETIMELTZ, PT_GENERIC_TYPE_STRING},  /* datetimeltz */
	{PT_TYPE_DATE, PT_TYPE_DATETIMETZ, PT_GENERIC_TYPE_STRING}, /* datetimetz */
	{PT_TYPE_DATE, PT_TYPE_TIMESTAMPLTZ, PT_GENERIC_TYPE_STRING}, /* timestamplltz */
	{PT_TYPE_DATE, PT_TYPE_TIMESTAMPTZ, PT_GENERIC_TYPE_STRING},/* timestampltz */
      };
      break;

    case PT_DEFAULTF:
      sigs =
      {
	{PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_ANY},
      };
      break;

    case PT_INDEX_CARDINALITY:
      sigs =
      {
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR, PT_TYPE_INTEGER},
      };
      break;

    case PT_SIGN:
      sigs =
      {
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_NUMBER},
      };
      break;

    case PT_SETEQ:
    case PT_SETNEQ:
    case PT_SUBSET:
    case PT_SUBSETEQ:
    case PT_SUPERSET:
    case PT_SUPERSETEQ:
      sigs =
      {
	{PT_TYPE_LOGICAL, PT_GENERIC_TYPE_SEQUENCE, PT_GENERIC_TYPE_SEQUENCE},
      };
      break;

    case PT_GE:
    case PT_GT:
    case PT_LT:
    case PT_LE:
      sigs =
      {
	{PT_TYPE_LOGICAL, PT_GENERIC_TYPE_PRIMITIVE, PT_GENERIC_TYPE_PRIMITIVE},
	{PT_TYPE_LOGICAL, PT_GENERIC_TYPE_LOB, PT_GENERIC_TYPE_LOB},
      };
      break;

    case PT_EQ:
    case PT_NE:
    case PT_NULLSAFE_EQ:
      sigs =
      {
	{PT_TYPE_LOGICAL, PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_ANY},
      };
      break;

    case PT_NVL:
    case PT_IFNULL:
    case PT_COALESCE:
      sigs =
      {
	{PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING},
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_ANY},
	{PT_GENERIC_TYPE_BIT, PT_GENERIC_TYPE_BIT, PT_GENERIC_TYPE_BIT},
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_BIT, PT_GENERIC_TYPE_ANY},
	{PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER},
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_ANY},
	{PT_GENERIC_TYPE_DATE, PT_GENERIC_TYPE_DATE, PT_GENERIC_TYPE_DATE},
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_DATE, PT_GENERIC_TYPE_ANY},
	{PT_TYPE_TIME, PT_TYPE_TIME, PT_TYPE_TIME},

	{PT_TYPE_VARCHAR, PT_TYPE_TIME, PT_GENERIC_TYPE_ANY},
	{PT_GENERIC_TYPE_SEQUENCE, PT_GENERIC_TYPE_SEQUENCE, PT_GENERIC_TYPE_SEQUENCE},
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_SEQUENCE, PT_GENERIC_TYPE_ANY},
	{PT_GENERIC_TYPE_LOB, PT_GENERIC_TYPE_LOB, PT_GENERIC_TYPE_LOB},
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_LOB, PT_GENERIC_TYPE_ANY},
	{PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_ANY},
      };
      break;

    case PT_NULLIF:
    case PT_LEAST:
    case PT_GREATEST:
      sigs =
      {
	{PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_ANY},
      };
      break;

    case PT_NVL2:
      sigs =
      {
	{PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING},
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_ANY},
	{PT_GENERIC_TYPE_BIT, PT_GENERIC_TYPE_BIT, PT_GENERIC_TYPE_BIT, PT_GENERIC_TYPE_BIT},
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_BIT, PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_ANY},
	{PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER},
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_ANY},
	{PT_GENERIC_TYPE_DATE, PT_GENERIC_TYPE_DATE, PT_GENERIC_TYPE_DATE, PT_GENERIC_TYPE_DATE},
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_DATE, PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_ANY},
	{PT_TYPE_TIME, PT_TYPE_TIME, PT_TYPE_TIME, PT_TYPE_TIME},
	{PT_TYPE_VARCHAR, PT_TYPE_TIME, PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_ANY},
	{PT_GENERIC_TYPE_SEQUENCE, PT_GENERIC_TYPE_SEQUENCE, PT_GENERIC_TYPE_SEQUENCE, PT_GENERIC_TYPE_SEQUENCE},
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_SEQUENCE, PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_ANY},
	{PT_GENERIC_TYPE_LOB, PT_GENERIC_TYPE_LOB, PT_GENERIC_TYPE_LOB, PT_GENERIC_TYPE_LOB},
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_LOB, PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_ANY},
	{PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_ANY},
      };
      break;

    case PT_CONNECT_BY_ROOT:
    case PT_PRIOR:
    case PT_QPRIOR:
      sigs =
      {
	{PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_ANY},
      };
      break;

    case PT_UNARY_MINUS:
      sigs =
      {
	{PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER},
      };
      break;

    case PT_TO_CHAR:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER},
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_DATETIME, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER},
      };
      break;

    case PT_ISNULL:
    case PT_IS_NULL:
    case PT_IS_NOT_NULL:
      sigs =
      {
	{PT_TYPE_LOGICAL, PT_GENERIC_TYPE_ANY},
      };
      break;
    case PT_IS:
    case PT_IS_NOT:
      sigs =
      {
	{PT_TYPE_LOGICAL, PT_TYPE_LOGICAL, PT_TYPE_LOGICAL},
      };
      break;
    case PT_SUBDATE:
    case PT_ADDDATE:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_CHAR, PT_TYPE_INTEGER},
	{PT_TYPE_DATE, PT_TYPE_DATE, PT_TYPE_INTEGER},
	{PT_TYPE_DATETIME, PT_TYPE_DATETIME, PT_TYPE_INTEGER},
	{PT_TYPE_DATETIME, PT_TYPE_TIMESTAMP, PT_TYPE_INTEGER},
	{PT_TYPE_DATETIMELTZ, PT_TYPE_DATETIMELTZ, PT_TYPE_INTEGER},
	{PT_TYPE_DATETIMETZ, PT_TYPE_DATETIMETZ, PT_TYPE_INTEGER},
	{PT_TYPE_DATETIMELTZ, PT_TYPE_TIMESTAMPLTZ, PT_TYPE_INTEGER},
	{PT_TYPE_DATETIMETZ, PT_TYPE_TIMESTAMPTZ, PT_TYPE_INTEGER},
      };
      break;

    case PT_GE_SOME:
    case PT_GT_SOME:
    case PT_LT_SOME:
    case PT_LE_SOME:
    case PT_GE_ALL:
    case PT_GT_ALL:
    case PT_LT_ALL:
    case PT_LE_ALL:
      sigs =
      {
	{PT_TYPE_LOGICAL, PT_GENERIC_TYPE_PRIMITIVE, PT_TYPE_SET},
	{PT_TYPE_LOGICAL, PT_GENERIC_TYPE_PRIMITIVE, PT_GENERIC_TYPE_QUERY},
      };
      break;

    case PT_EQ_SOME:
    case PT_NE_SOME:
    case PT_EQ_ALL:
    case PT_NE_ALL:
    case PT_IS_IN:
    case PT_IS_NOT_IN:
      sigs =
      {
	{PT_TYPE_LOGICAL, PT_GENERIC_TYPE_ANY, PT_TYPE_SET},
	{PT_TYPE_LOGICAL, PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_QUERY},
      };
      break;

    case PT_BETWEEN_EQ_NA:
    case PT_BETWEEN_GE_INF:
    case PT_BETWEEN_GT_INF:
    case PT_BETWEEN_INF_LE:
    case PT_BETWEEN_INF_LT:
    case PT_LT_INF:
    case PT_GT_INF:
      /* these expressions are introduced during query rewriting and are used in the range operator. we should set the
       * return type to the type of the argument */
      sigs =
      {
	{PT_GENERIC_TYPE_ANY, PT_GENERIC_TYPE_ANY},
      };
      break;

    case PT_UNIX_TIMESTAMP:
      sigs =
      {
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_STRING},
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_DATE},
	{PT_TYPE_INTEGER, PT_TYPE_NONE},
      };
      break;

    case PT_FROM_UNIXTIME:
      sigs =
      {
	{PT_TYPE_TIMESTAMP, PT_TYPE_INTEGER, PT_TYPE_NONE, PT_TYPE_INTEGER},
	{PT_TYPE_VARCHAR, PT_TYPE_INTEGER, PT_GENERIC_TYPE_STRING, PT_TYPE_INTEGER},
      };
      break;

    case PT_TIMESTAMP:
      sigs =
      {
	{PT_TYPE_DATETIME, PT_GENERIC_TYPE_STRING}, /* TIMESTAMP(STRING) */
	{PT_TYPE_DATETIME, PT_GENERIC_TYPE_DATE}, /* TIMESTAMP(DATETIME) */
	{PT_TYPE_DATETIME, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING}, /* TIMESTAMP(STRING,STRING) */
	{PT_TYPE_DATETIME, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_NUMBER}, /* TIMESTAMP(STRING,NUMBER) */
	{PT_TYPE_DATETIME, PT_GENERIC_TYPE_STRING, PT_TYPE_TIME}, /* TIMESTAMP(STRING,TIME) */
	{PT_TYPE_DATETIME, PT_GENERIC_TYPE_DATE, PT_GENERIC_TYPE_STRING}, /* TIMESTAMP(DATETIME,STRING) */
	{PT_TYPE_DATETIME, PT_GENERIC_TYPE_DATE, PT_TYPE_TIME}, /* TIMESTAMP(DATETIME,TIME) */
	{PT_TYPE_DATETIME, PT_GENERIC_TYPE_DATE, PT_GENERIC_TYPE_NUMBER}, /* TIMESTAMP(DATETIME,NUMBER) */
      };
      break;

    case PT_TYPEOF:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_ANY},
      };
      break;

    case PT_EXTRACT:
      sigs =
      {
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_ANY},
	{PT_TYPE_INTEGER, PT_TYPE_TIME},
	{PT_TYPE_INTEGER, PT_TYPE_DATE},
	{PT_TYPE_INTEGER, PT_TYPE_TIMESTAMP},
	{PT_TYPE_INTEGER, PT_TYPE_DATETIME},
      };
      break;

    case PT_EVALUATE_VARIABLE:
      sigs =
      {
	{PT_TYPE_MAYBE, PT_TYPE_CHAR},
      };
      break;

    case PT_DEFINE_VARIABLE:

      sigs =
      {
	{PT_TYPE_MAYBE, PT_TYPE_CHAR, PT_GENERIC_TYPE_STRING},
	{PT_TYPE_MAYBE, PT_TYPE_CHAR, PT_GENERIC_TYPE_NUMBER},
      };
      break;

    case PT_EXEC_STATS:
      sigs =
      {
	{PT_TYPE_BIGINT, PT_GENERIC_TYPE_CHAR},
      };
      break;

    case PT_TO_ENUMERATION_VALUE:
      sigs =
      {
	{PT_TYPE_ENUMERATION, PT_GENERIC_TYPE_ANY},
      };
      break;

    case PT_INET_ATON:
      sigs =
      {
	{PT_TYPE_BIGINT, PT_GENERIC_TYPE_CHAR},
      };
      break;

    case PT_INET_NTOA:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_TYPE_BIGINT},
      };
      break;

    case PT_COERCIBILITY:
      sigs =
      {
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_ANY},
      };
      break;

    case PT_CHARSET:
    case PT_COLLATION:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_ANY},
      };
      break;

    case PT_WIDTH_BUCKET:

      sigs =
      {
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_NUMBER, PT_GENERIC_TYPE_NUMBER, PT_TYPE_DOUBLE}, /* generic number */
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_STRING, PT_GENERIC_TYPE_STRING, PT_TYPE_DOUBLE}, /* generic string */
	{PT_TYPE_INTEGER, PT_TYPE_DATE, PT_TYPE_DATE, PT_TYPE_DOUBLE}, /* date */
	{PT_TYPE_INTEGER, PT_TYPE_DATETIME, PT_TYPE_DATETIME, PT_TYPE_DOUBLE}, /* datetime */
	{PT_TYPE_INTEGER, PT_TYPE_TIMESTAMP, PT_TYPE_TIMESTAMP, PT_TYPE_DOUBLE}, /* timestamp */
	{PT_TYPE_INTEGER, PT_TYPE_TIME, PT_TYPE_TIME, PT_TYPE_DOUBLE}, /* time */
	{PT_TYPE_INTEGER, PT_TYPE_DATETIMELTZ, PT_TYPE_DATETIMELTZ, PT_TYPE_DOUBLE}, /* datetime with local timezone */
	{PT_TYPE_INTEGER, PT_TYPE_DATETIMETZ, PT_TYPE_DATETIMETZ, PT_TYPE_DOUBLE}, /* datetime with timezone */
	{PT_TYPE_INTEGER, PT_TYPE_TIMESTAMPLTZ, PT_TYPE_TIMESTAMPLTZ, PT_TYPE_DOUBLE}, /* timestamp with local timezone */
	{PT_TYPE_INTEGER, PT_TYPE_TIMESTAMPTZ, PT_TYPE_TIMESTAMPTZ, PT_TYPE_DOUBLE}, /* timestamp with timezone */
      };
      break;

    case PT_TRACE_STATS:
      sigs =
      {
	{PT_TYPE_VARCHAR},
      };
      break;

    case PT_INDEX_PREFIX:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR},
	{PT_TYPE_VARNCHAR, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_CHAR},
	{PT_TYPE_VARBIT, PT_GENERIC_TYPE_BIT, PT_GENERIC_TYPE_BIT, PT_GENERIC_TYPE_CHAR},
      };
      break;

    case PT_SLEEP:
      sigs =
      {
	{PT_TYPE_INTEGER, PT_TYPE_DOUBLE},
      };
      break;

    case PT_DBTIMEZONE:
    case PT_SESSIONTIMEZONE:
      sigs =
      {
	{PT_TYPE_VARCHAR},
      };
      break;

    case PT_TZ_OFFSET:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_TYPE_VARCHAR},
      };
      break;

    case PT_NEW_TIME:

      sigs =
      {
	{PT_GENERIC_TYPE_DATETIME, PT_GENERIC_TYPE_DATETIME, PT_TYPE_VARCHAR, PT_TYPE_VARCHAR},
	{PT_TYPE_DATETIME, PT_TYPE_DATETIME, PT_TYPE_VARCHAR, PT_TYPE_VARCHAR},
	{PT_TYPE_TIME, PT_TYPE_TIME, PT_TYPE_VARCHAR, PT_TYPE_VARCHAR},
      };
      break;

    case PT_FROM_TZ:
      sigs =
      {
	{PT_GENERIC_TYPE_DATETIME, PT_GENERIC_TYPE_DATETIME, PT_TYPE_VARCHAR},
	{PT_TYPE_DATETIMETZ, PT_TYPE_DATETIME, PT_TYPE_VARCHAR},
      };
      break;

    case PT_CONV_TZ:
      sigs =
      {
	{PT_TYPE_DATETIMETZ, PT_TYPE_DATETIMETZ},
	{PT_TYPE_DATETIMELTZ, PT_TYPE_DATETIMELTZ},
	{PT_TYPE_TIMESTAMPTZ, PT_TYPE_TIMESTAMPTZ},
	{PT_TYPE_TIMESTAMPLTZ, PT_TYPE_TIMESTAMPLTZ},
      };
      break;

    case PT_TO_DATETIME_TZ:
      sigs =
      {
	{PT_TYPE_DATETIMETZ, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR, PT_TYPE_INTEGER},
	{PT_TYPE_DATETIMETZ, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR, PT_TYPE_INTEGER},
      };
      break;
    case PT_TO_TIMESTAMP_TZ:
      sigs =
      {
	{PT_TYPE_TIMESTAMPTZ, PT_GENERIC_TYPE_CHAR, PT_GENERIC_TYPE_CHAR, PT_TYPE_INTEGER},
	{PT_TYPE_TIMESTAMPTZ, PT_GENERIC_TYPE_NCHAR, PT_GENERIC_TYPE_NCHAR, PT_TYPE_INTEGER},
      };
      break;

    case PT_CRC32:
      sigs =
      {
	{PT_TYPE_INTEGER, PT_GENERIC_TYPE_STRING},
      };
      break;

    case PT_SCHEMA_DEF:
      sigs =
      {
	{PT_TYPE_VARCHAR, PT_GENERIC_TYPE_STRING},
      };
      break;

    // return false, if no expression definition
    // case 1) the expression definition is not needed for the op
    case PT_CAST:
    case PT_RANGE:
    case PT_IF:
    case PT_ASSIGN:
    case PT_BETWEEN_AND:
    case PT_BETWEEN_GE_LE:
    case PT_BETWEEN_GE_LT:
    case PT_BETWEEN_GT_LE:
    case PT_BETWEEN_GT_LT:
    case PT_LIKE_ESCAPE:
    case PT_CASE:
    case PT_PATH_EXPR_SET:
    case PT_ENCRYPT:
    case PT_DECRYPT:
    case PT_DECODE:
    case PT_FIELD:
    case PT_DATE_ADD:
    case PT_DATE_SUB:
    case PT_STR_TO_DATE:
    case PT_OID_OF_DUPLICATE_KEY:
    case PT_FUNCTION_HOLDER:
    case PT_LAST_OPCODE:
    case PT_EXISTS:
      return false;

      // default:
      // case 2) maybe a new op is defined and it omitted
      // TODO: need assertion?
      assert (false);
      return false;
    }

  def.op = op;
  def.sigs = sigs;

  return true;
}

/*
 * pt_comp_to_between_op () -
 *   return:
 *   left(in):
 *   right(in):
 *   type(in):
 *   between(out):
 */
int
pt_comp_to_between_op (PT_OP_TYPE left, PT_OP_TYPE right, PT_COMP_TO_BETWEEN_OP_CODE_TYPE type, PT_OP_TYPE *between)
{
  size_t i;

  for (i = 0; i < COMPARE_BETWEEN_OPERATOR_COUNT; i++)
    {
      if (left == pt_Compare_between_operator_table[i].left && right == pt_Compare_between_operator_table[i].right)
	{
	  *between = pt_Compare_between_operator_table[i].between;

	  return 0;
	}
    }

  if (type == PT_RANGE_INTERSECTION)
    {
      /* range intersection */
      if ((left == PT_GE && right == PT_EQ) || (left == PT_EQ && right == PT_LE))
	{
	  *between = PT_BETWEEN_EQ_NA;
	  return 0;
	}
    }

  return -1;
}


/*
 * pt_between_to_comp_op () -
 *   return:
 *   between(in):
 *   left(out):
 *   right(out):
 */
int
pt_between_to_comp_op (PT_OP_TYPE between, PT_OP_TYPE *left, PT_OP_TYPE *right)
{
  size_t i;

  for (i = 0; i < COMPARE_BETWEEN_OPERATOR_COUNT; i++)
    if (between == pt_Compare_between_operator_table[i].between)
      {
	*left = pt_Compare_between_operator_table[i].left;
	*right = pt_Compare_between_operator_table[i].right;

	return 0;
      }

  return -1;
}

/*
 * pt_is_range_comp_op () - return true if the op is related to range
 *  return  : true if the operatior is of range
 *  op (in) : operator
 */
bool
pt_is_range_comp_op (PT_OP_TYPE op)
{
  switch (op)
    {
    case PT_GE:
    case PT_GT:
    case PT_LT:
    case PT_LE:
    case PT_GT_INF:
    case PT_LT_INF:
    case PT_BETWEEN_GE_LE:
    case PT_BETWEEN_GE_LT:
    case PT_BETWEEN_GT_LE:
    case PT_BETWEEN_GT_LT:
    case PT_BETWEEN_EQ_NA:
    case PT_BETWEEN_INF_LE:
    case PT_BETWEEN_INF_LT:
    case PT_BETWEEN_GE_INF:
    case PT_BETWEEN_GT_INF:
    case PT_RANGE:
      return 1;
    default:
      return 0;
    }
}


/*
 * pt_is_range_expression () - return true if the expression is evaluated
 *				 as a logical expression
 *  return  : true if the expression is of type logical
 *  op (in) : the expression
 */
bool
pt_is_range_expression (const PT_OP_TYPE op)
{
  switch (op)
    {
    case PT_GE_SOME:
    case PT_GT_SOME:
    case PT_LT_SOME:
    case PT_LE_SOME:
    case PT_GE_ALL:
    case PT_GT_ALL:
    case PT_LT_ALL:
    case PT_LE_ALL:
    case PT_EQ_SOME:
    case PT_NE_SOME:
    case PT_EQ_ALL:
    case PT_NE_ALL:
    case PT_IS_IN:
    case PT_IS_NOT_IN:
      return true;
    default:
      return false;
    }
  return false;
}

/*
 * pt_is_able_to_determine_return_type () -
 *   return: true if the type of the return value can be determined
 *             regardless of its arguments, otherwise false.
 *   op(in):
 */
bool
pt_is_able_to_determine_return_type (const PT_OP_TYPE op)
{
  switch (op)
    {
    case PT_CAST:
    case PT_TO_NUMBER:
    case PT_TO_CHAR:
    case PT_TO_DATE:
    case PT_TO_TIME:
    case PT_TO_TIMESTAMP:
    case PT_TO_DATETIME:
    case PT_POSITION:
    case PT_FINDINSET:
    case PT_OCTET_LENGTH:
    case PT_BIT_LENGTH:
    case PT_CHAR_LENGTH:
    case PT_TIME_FORMAT:
    case PT_TIMESTAMP:
    case PT_UNIX_TIMESTAMP:
    case PT_SIGN:
    case PT_CHR:
    case PT_ADD_MONTHS:
    case PT_LAST_DAY:
    case PT_MONTHS_BETWEEN:
    case PT_DATE_ADD:
    case PT_ADDDATE:
    case PT_FORMAT:
    case PT_DATE_SUB:
    case PT_SUBDATE:
    case PT_DATE_FORMAT:
    case PT_STR_TO_DATE:
    case PT_SIN:
    case PT_COS:
    case PT_TAN:
    case PT_ASIN:
    case PT_ACOS:
    case PT_ATAN:
    case PT_ATAN2:
    case PT_COT:
    case PT_LOG:
    case PT_EXP:
    case PT_SQRT:
    case PT_DEGREES:
    case PT_LN:
    case PT_LOG2:
    case PT_LOG10:
    case PT_POWER:
    case PT_FIELD:
    case PT_LOCATE:
    case PT_STRCMP:
    case PT_RADIANS:
    case PT_BIT_AND:
    case PT_BIT_OR:
    case PT_BIT_XOR:
    case PT_BIT_NOT:
    case PT_BITSHIFT_LEFT:
    case PT_BITSHIFT_RIGHT:
    case PT_BIT_COUNT:
    case PT_DATEDIFF:
    case PT_TIMEDIFF:
    case PT_DATEF:
    case PT_TIMEF:
    case PT_ISNULL:
    case PT_RAND:
    case PT_DRAND:
    case PT_RANDOM:
    case PT_DRANDOM:
    case PT_BIT_TO_BLOB:
    case PT_BLOB_FROM_FILE:
    case PT_BLOB_LENGTH:
    case PT_BLOB_TO_BIT:
    case PT_CHAR_TO_BLOB:
    case PT_CHAR_TO_CLOB:
    case PT_CLOB_FROM_FILE:
    case PT_CLOB_LENGTH:
    case PT_CLOB_TO_CHAR:
    case PT_TYPEOF:
    case PT_YEARF:
    case PT_MONTHF:
    case PT_DAYF:
    case PT_DAYOFMONTH:
    case PT_HOURF:
    case PT_MINUTEF:
    case PT_SECONDF:
    case PT_QUARTERF:
    case PT_WEEKDAY:
    case PT_DAYOFWEEK:
    case PT_DAYOFYEAR:
    case PT_TODAYS:
    case PT_FROMDAYS:
    case PT_TIMETOSEC:
    case PT_SECTOTIME:
    case PT_WEEKF:
    case PT_MAKEDATE:
    case PT_MAKETIME:
    case PT_BIN:
    case PT_CASE:
    case PT_DECODE:
    case PT_LIKE:
    case PT_NOT_LIKE:
    case PT_RLIKE:
    case PT_NOT_RLIKE:
    case PT_RLIKE_BINARY:
    case PT_NOT_RLIKE_BINARY:
    case PT_EVALUATE_VARIABLE:
    case PT_DEFINE_VARIABLE:
    case PT_HEX:
    case PT_ASCII:
    case PT_CONV:
    case PT_TO_ENUMERATION_VALUE:
    case PT_INET_ATON:
    case PT_INET_NTOA:
    case PT_CHARSET:
    case PT_COERCIBILITY:
    case PT_COLLATION:
    case PT_WIDTH_BUCKET:
    case PT_AES_ENCRYPT:
    case PT_AES_DECRYPT:
    case PT_SHA_ONE:
    case PT_SHA_TWO:
    case PT_SLEEP:
    case PT_TO_DATETIME_TZ:
    case PT_TO_TIMESTAMP_TZ:
    case PT_CRC32:
    case PT_DISK_SIZE:
    case PT_SCHEMA_DEF:
      return true;

    default:
      return false;
    }
}

/*
 * pt_is_op_with_forced_common_type () - checks if the operator is in the list
 *			      of operators that should force its arguments to
 *			      the same type if none of the arguments has a
 *			      determined type
 *
 *   return: true if arguments types should be mirrored
 *   op(in): operator type
 *
 *  Note: this functions is used by type inference algorithm
 */
bool
pt_is_op_with_forced_common_type (PT_OP_TYPE op)
{
  switch (op)
    {
    case PT_IFNULL:
    case PT_COALESCE:
    case PT_NVL:
    case PT_NVL2:
    case PT_NULLIF:
    case PT_LEAST:
    case PT_GREATEST:
    case PT_BETWEEN:
      return true;
    default:
      return false;
    }
  return false;
}

/*
 * pt_is_range_or_comp () - return true if the operator is range or comparison
 *  return:
 *  op(in):
 */
bool
pt_is_range_or_comp (PT_OP_TYPE op)
{
  switch (op)
    {
    case PT_EQ:
    case PT_NE:
    case PT_GE:
    case PT_GT:
    case PT_LT:
    case PT_LE:
    case PT_NULLSAFE_EQ:
    case PT_GT_INF:
    case PT_LT_INF:
    case PT_BETWEEN:
    case PT_BETWEEN_GE_LE:
    case PT_BETWEEN_GE_LT:
    case PT_BETWEEN_GT_LE:
    case PT_BETWEEN_GT_LT:
    case PT_BETWEEN_INF_LE:
    case PT_BETWEEN_INF_LT:
    case PT_BETWEEN_GE_INF:
    case PT_BETWEEN_GT_INF:
      return true;
    default:
      return false;
    }
}

/*
 * pt_is_op_w_collation () - check if is required to check collation or
 *			     codeset of this operator
 *
 *   return:
 *   node(in): a parse tree node
 *
 */
bool
pt_is_op_w_collation (const PT_OP_TYPE op)
{
  switch (op)
    {
    case PT_EQ:
    case PT_NE:
    case PT_GE:
    case PT_GT:
    case PT_LT:
    case PT_LE:
    case PT_NULLSAFE_EQ:
    case PT_BETWEEN:
    case PT_NOT_BETWEEN:
    case PT_BETWEEN_AND:
    case PT_BETWEEN_GE_LE:
    case PT_BETWEEN_GE_LT:
    case PT_BETWEEN_GT_LE:
    case PT_BETWEEN_GT_LT:
    case PT_CONCAT:
    case PT_CONCAT_WS:
    case PT_PLUS:
    case PT_LIKE:
    case PT_NOT_LIKE:
    case PT_SUBSTRING_INDEX:
    case PT_RPAD:
    case PT_LPAD:
    case PT_MID:
    case PT_SUBSTRING:
    case PT_REPLACE:
    case PT_TRANSLATE:
    case PT_COALESCE:
    case PT_STRCAT:
    case PT_TIME_FORMAT:
    case PT_DATE_FORMAT:
    case PT_TIMEF:
    case PT_DATEF:
    case PT_SETEQ:
    case PT_SETNEQ:
    case PT_SUBSET:
    case PT_SUBSETEQ:
    case PT_SUPERSET:
    case PT_SUPERSETEQ:
    case PT_GREATEST:
    case PT_LEAST:
    case PT_NULLIF:
    case PT_LOWER:
    case PT_UPPER:
    case PT_RTRIM:
    case PT_LTRIM:
    case PT_TRIM:
    case PT_LEFT:
    case PT_RIGHT:
    case PT_NVL:
    case PT_NVL2:
    case PT_IFNULL:
    case PT_IS_IN:
    case PT_IS_NOT_IN:
    case PT_EQ_SOME:
    case PT_NE_SOME:
    case PT_GE_SOME:
    case PT_GT_SOME:
    case PT_LT_SOME:
    case PT_LE_SOME:
    case PT_EQ_ALL:
    case PT_NE_ALL:
    case PT_GE_ALL:
    case PT_GT_ALL:
    case PT_LT_ALL:
    case PT_LE_ALL:
    case PT_FINDINSET:
    case PT_INSTR:
    case PT_LOCATE:
    case PT_POSITION:
    case PT_STRCMP:
    case PT_IF:
    case PT_FIELD:
    case PT_REVERSE:
    case PT_CONNECT_BY_ROOT:
    case PT_PRIOR:
    case PT_QPRIOR:
    case PT_INDEX_PREFIX:
    case PT_MINUS:
      return true;
    default:
      return false;
    }
  return false;
}

/*
 * pt_is_symmetric_op () - In the context of type inference, symmetric operators can be relevant when determining the types of operands and the resulting type of an operation.
      When encountering a symmetric operator during type inference, the types of the operands can provide valuable information for inferring the resulting type.
      If the types of the operands are known, the resulting type can often be inferred.
      For example, a symmetric operator "+"(PT_PLUS) that performs addition.
      If the type of one operand is known to be an integer and the type of the other operand is known to be a floating-point number,
      the resulting type can be inferred to be a floating-point number.
      This inference is possible because addition is a symmetric operation, and the order of the operands does not affect the result.
 *   return:
 *   op(in):
 */
bool
pt_is_symmetric_op (const PT_OP_TYPE op)
{
  switch (op)
    {
    case PT_FUNCTION_HOLDER:
    case PT_ASSIGN:
    case PT_GE_SOME:
    case PT_GT_SOME:
    case PT_LT_SOME:
    case PT_LE_SOME:
    case PT_GE_ALL:
    case PT_GT_ALL:
    case PT_LT_ALL:
    case PT_LE_ALL:
    case PT_EQ_SOME:
    case PT_NE_SOME:
    case PT_EQ_ALL:
    case PT_NE_ALL:
    case PT_IS_IN:
    case PT_IS_NOT_IN:
    case PT_IS_NULL:
    case PT_IS_NOT_NULL:
    case PT_POSITION:
    case PT_FINDINSET:
    case PT_SUBSTRING:
    case PT_SUBSTRING_INDEX:
    case PT_OCTET_LENGTH:
    case PT_BIT_LENGTH:
    case PT_CHAR_LENGTH:
    case PT_BIN:
    case PT_TRIM:
    case PT_LTRIM:
    case PT_RTRIM:
    case PT_LIKE_LOWER_BOUND:
    case PT_LIKE_UPPER_BOUND:
    case PT_LPAD:
    case PT_RPAD:
    case PT_REPEAT:
    case PT_REPLACE:
    case PT_TRANSLATE:
    case PT_ADD_MONTHS:
    case PT_LAST_DAY:
    case PT_MONTHS_BETWEEN:
    case PT_SYS_DATE:
    case PT_CURRENT_DATE:
    case PT_SYS_TIME:
    case PT_CURRENT_TIME:
    case PT_SYS_TIMESTAMP:
    case PT_CURRENT_TIMESTAMP:
    case PT_SYS_DATETIME:
    case PT_CURRENT_DATETIME:
    case PT_UTC_TIME:
    case PT_UTC_DATE:
    case PT_TO_CHAR:
    case PT_TO_DATE:
    case PT_TO_TIME:
    case PT_TO_TIMESTAMP:
    case PT_TO_DATETIME:
    case PT_TO_NUMBER:
    case PT_CURRENT_VALUE:
    case PT_NEXT_VALUE:
    case PT_CAST:
    case PT_EXTRACT:
    case PT_INST_NUM:
    case PT_ROWNUM:
    case PT_ORDERBY_NUM:
    case PT_CONNECT_BY_ISCYCLE:
    case PT_CONNECT_BY_ISLEAF:
    case PT_LEVEL:
    case PT_CONNECT_BY_ROOT:
    case PT_SYS_CONNECT_BY_PATH:
    case PT_QPRIOR:
    case PT_CURRENT_USER:
    case PT_LOCAL_TRANSACTION_ID:
    case PT_CHR:
    case PT_ROUND:
    case PT_TRUNC:
    case PT_INSTR:
    case PT_TIME_FORMAT:
    case PT_TIMESTAMP:
    case PT_TIMEF:
    case PT_YEARF:
    case PT_MONTHF:
    case PT_DAYF:
    case PT_DAYOFMONTH:
    case PT_HOURF:
    case PT_MINUTEF:
    case PT_SECONDF:
    case PT_QUARTERF:
    case PT_WEEKDAY:
    case PT_DAYOFWEEK:
    case PT_DAYOFYEAR:
    case PT_TODAYS:
    case PT_FROMDAYS:
    case PT_TIMETOSEC:
    case PT_SECTOTIME:
    case PT_WEEKF:
    case PT_MAKETIME:
    case PT_MAKEDATE:
    case PT_ADDTIME:
    case PT_SCHEMA:
    case PT_DATABASE:
    case PT_VERSION:
    case PT_UNIX_TIMESTAMP:
    case PT_FROM_UNIXTIME:
    case PT_IS:
    case PT_IS_NOT:
    case PT_CONCAT:
    case PT_CONCAT_WS:
    case PT_FIELD:
    case PT_LEFT:
    case PT_RIGHT:
    case PT_LOCATE:
    case PT_MID:
    case PT_REVERSE:
    case PT_DISK_SIZE:
    case PT_ADDDATE:
    case PT_DATE_ADD:
    case PT_SUBDATE:
    case PT_DATE_SUB:
    case PT_FORMAT:
    case PT_ATAN2:
    case PT_DATE_FORMAT:
    case PT_USER:
    case PT_STR_TO_DATE:
    case PT_LIST_DBS:
    case PT_SYS_GUID:
    case PT_IF:
    case PT_POWER:
    case PT_BIT_TO_BLOB:
    case PT_BLOB_FROM_FILE:
    case PT_BLOB_LENGTH:
    case PT_BLOB_TO_BIT:
    case PT_CHAR_TO_BLOB:
    case PT_CHAR_TO_CLOB:
    case PT_CLOB_FROM_FILE:
    case PT_CLOB_LENGTH:
    case PT_CLOB_TO_CHAR:
    case PT_TYPEOF:
    case PT_INDEX_CARDINALITY:
    case PT_INCR:
    case PT_DECR:
    case PT_RAND:
    case PT_RANDOM:
    case PT_DRAND:
    case PT_DRANDOM:
    case PT_PI:
    case PT_ROW_COUNT:
    case PT_LAST_INSERT_ID:
    case PT_ABS:
    case PT_BETWEEN_EQ_NA:
    case PT_BETWEEN_GE_INF:
    case PT_BETWEEN_GT_INF:
    case PT_BETWEEN_INF_LE:
    case PT_BETWEEN_INF_LT:
    case PT_LT_INF:
    case PT_GT_INF:
    case PT_CASE:
    case PT_DECODE:
    case PT_LIKE_ESCAPE:
    case PT_RLIKE:
    case PT_NOT_RLIKE:
    case PT_RLIKE_BINARY:
    case PT_NOT_RLIKE_BINARY:
    case PT_EVALUATE_VARIABLE:
    case PT_DEFINE_VARIABLE:
    case PT_EXEC_STATS:
    case PT_CONV:
    case PT_IFNULL:
    case PT_NVL:
    case PT_NVL2:
    case PT_COALESCE:
    case PT_TO_ENUMERATION_VALUE:
    case PT_CHARSET:
    case PT_COERCIBILITY:
    case PT_COLLATION:
    case PT_WIDTH_BUCKET:
    case PT_TRACE_STATS:
    case PT_SHA_ONE:
    case PT_SHA_TWO:
    case PT_AES_ENCRYPT:
    case PT_AES_DECRYPT:
    case PT_INDEX_PREFIX:
    case PT_SLEEP:
    case PT_DBTIMEZONE:
    case PT_SESSIONTIMEZONE:
    case PT_TZ_OFFSET:
    case PT_NEW_TIME:
    case PT_FROM_TZ:
    case PT_TO_DATETIME_TZ:
    case PT_TO_TIMESTAMP_TZ:
    case PT_UTC_TIMESTAMP:
    case PT_CRC32:
    case PT_SCHEMA_DEF:
    case PT_CONV_TZ:
      return false;

    default:
      return true;
    }
}

bool pt_is_op_unary_special_operator_on_hv (const PT_OP_TYPE op)
{
  switch (op)
    {
    case PT_UNARY_MINUS:
    case PT_PRIOR:
    case PT_CONNECT_BY_ROOT:
    case PT_QPRIOR:
    case PT_BIT_NOT:
    case PT_BIT_COUNT:
      return true;

    default:
      return false;
    }
}

/*
* pt_is_enumeration_special_comparison () - Special case handling for unary operators on host variables
  * (-?)
  * (prior ?)
  * (connect_by_root ?)
  * ...
* return : true if special case handling is required or false otherwise.
* node (in) : node
*/
bool pt_is_node_unary_special_operator_on_hv (PT_NODE *node)
{
  return (node->node_type == PT_EXPR
	  && node->type_enum == PT_TYPE_MAYBE
	  && pt_is_op_unary_special_operator_on_hv (node->info.expr.op)
	  && pt_is_host_var_with_maybe_type (node->info.expr.arg1)
	 );
}

bool pt_is_host_var_with_maybe_type (PT_NODE *node)
{
  return node->type_enum == PT_TYPE_MAYBE && node->node_type == PT_HOST_VAR;
}

/*
* pt_is_enumeration_special_comparison () - check if the comparison is a
*     '=' comparison that involves ENUM types and constants or if it's a IN
*     'IN' comparison in which the left operator is an ENUM.
* return : true if it is a special ENUM comparison or false otherwise.
* arg1 (in) : left argument
* op (in)   : expression operator
* arg2 (in) : right argument
*/
bool
pt_is_enumeration_special_comparison (PT_NODE *arg1, PT_OP_TYPE op, PT_NODE *arg2)
{
  PT_NODE *arg_tmp = NULL;

  if (arg1 == NULL || arg2 == NULL)
    {
      return false;
    }

  switch (op)
    {
    case PT_EQ:
    case PT_NE:
    case PT_NULLSAFE_EQ:
      if (arg1->type_enum != PT_TYPE_ENUMERATION)
	{
	  if (arg2->type_enum != PT_TYPE_ENUMERATION)
	    {
	      return false;
	    }

	  arg_tmp = arg1;
	  arg1 = arg2;
	  arg2 = arg_tmp;
	}
      else if (arg2->type_enum == PT_TYPE_ENUMERATION && arg1->data_type != NULL && arg2->data_type != NULL)
	{
	  if (pt_is_same_enum_data_type (arg1->data_type, arg2->data_type))
	    {
	      return true;
	    }
	}
      if (arg2->node_type == PT_EXPR)
	{
	  if (arg2->info.expr.op != PT_TO_ENUMERATION_VALUE)
	    {
	      return false;
	    }
	}
      else
	{
	  if (!PT_IS_CONST (arg2))
	    {
	      return false;
	    }
	}
      return true;
    case PT_IS_IN:
    case PT_IS_NOT_IN:
    case PT_EQ_SOME:
    case PT_NE_SOME:
    case PT_EQ_ALL:
    case PT_NE_ALL:
      return (arg1->type_enum == PT_TYPE_ENUMERATION);
    default:
      return false;
    }
}

//
//
//

int
type_check_expr_helper::do_type_checking ()
{
  int code = NO_ERROR;
  if (m_is_finished)
    {
      goto exit;
    }

  /* handle special cases for the enumeration type */
  if (pt_is_enumeration_special_comparison (GET_ARG1 (), GET_OP(), GET_ARG2()))
    {
      if ((code = handle_special_enumeration_op ()) != NO_ERROR || m_is_finished)
	{
	  goto exit;
	}
    }

  if (GET_OP() == PT_FUNCTION_HOLDER)
    {
      if ((code = eval_function_holder ()) != NO_ERROR || m_is_finished)
	{
	  goto exit;
	}
    }

  //
  preprocess ();

  /*
   * At this point, arg1_hv is non-NULL (and equal to arg1) if it represents
   * a dynamic host variable, i.e., a host var parameter that hasn't had
   * a value supplied at compile time.  Same for arg2_hv and arg3_hv...
   */
  m_common_type = m_arg1_type;
  m_expr = m_node;

  /* adjust expression definition to fit the signature implementation */
  if ((code = adjust_expr_def ()) != NO_ERROR || m_is_finished)
    {
      goto exit;
    }

  if ((code = pt_apply_expressions_definition (m_parser, &m_expr)) != NO_ERROR || m_is_finished)
    {
      m_node->type_enum = PT_TYPE_NONE;
      goto exit;
    }

  // TODO: old type evaluation routine shoule be performed
  // remove me if the old type evaluation routine is migrated to here
  m_is_finished = false;
  return code;

exit:
  return code;
}

int
type_check_expr_helper::preprocess ()
{
  if (GET_ARG1 ())
    {
      m_arg1_type = GET_ARG1 ()->type_enum;

      if (pt_is_host_var_with_maybe_type (GET_ARG1 ()))
	{
	  m_arg1_hv = GET_ARG1 ();
	}

      /* Special case handling for unary operators on host variables (-?) or (prior ?) or (connect_by_root ?) */
      if (pt_is_node_unary_special_operator_on_hv (GET_ARG1 ()))
	{
	  m_arg1_hv = GET_ARG1 ()->info.expr.arg1;
	}
    }

  if (GET_ARG2 ())
    {
      if (GET_ARG2 ()->or_next == NULL)
	{
	  m_arg2_type = GET_ARG2 ()->type_enum;
	}
      else
	{
	  PT_NODE *temp;
	  PT_TYPE_ENUM temp_type;

	  m_common_type = PT_TYPE_NONE;
	  /* do traverse multi-args in RANGE operator */
	  for (temp = GET_ARG2 (); temp; temp = temp->or_next)
	    {
	      temp_type = pt_common_type (m_arg1_type, temp->type_enum);
	      if (temp_type != PT_TYPE_NONE)
		{
		  m_common_type = (m_common_type == PT_TYPE_NONE) ? temp_type : pt_common_type (m_common_type, temp_type);
		}
	    }
	  m_arg2_type = m_common_type;
	}

      if (pt_is_host_var_with_maybe_type (GET_ARG2 ()))
	{
	  m_arg2_hv = GET_ARG2 ();
	}

      /* Special case handling for unary operators on host variables (-?) or (prior ?) or (connect_by_root ?) */
      if (pt_is_node_unary_special_operator_on_hv (GET_ARG2 ()))
	{
	  m_arg2_hv = GET_ARG2 ()->info.expr.arg1;
	}
    }

  if (GET_ARG3 ())
    {
      m_arg3_type = GET_ARG3 ()->type_enum;
      if (pt_is_host_var_with_maybe_type (GET_ARG3 ()))
	{
	  m_arg3_hv = GET_ARG3 ();
	}
    }

  return NO_ERROR;
}

int
type_check_expr_helper::adjust_expr_def ()
{
  PT_OP_TYPE op = GET_OP ();
  PT_TYPE_ENUM arg1_type = m_arg1_type;
  PT_TYPE_ENUM arg2_type = m_arg2_type;
  PT_TYPE_ENUM arg3_type = m_arg3_type;

  PT_NODE *arg1 = GET_ARG1 ();
  PT_NODE *arg2 = GET_ARG2 ();
  PT_NODE *arg3 = GET_ARG3 ();

  PT_NODE *expr = NULL;

  switch (op)
    {
    case PT_PLUS:
      if (arg1_type == PT_TYPE_NULL || arg2_type == PT_TYPE_NULL)
	{
	  if (prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING) == false
	      || (!PT_IS_STRING_TYPE (arg1_type) && !PT_IS_STRING_TYPE (arg2_type)))
	    {
	      m_node->type_enum = PT_TYPE_NULL;
	      return ER_FAILED;
	    }
	}
      if (arg1_type == PT_TYPE_MAYBE || arg2_type == PT_TYPE_MAYBE)
	{
	  m_node->type_enum = PT_TYPE_MAYBE;

	  // TODO: previously, goto cannot_use_signature
	  if (expr != NULL)
	    {
	      expr = pt_wrap_expr_w_exp_dom_cast (m_parser, expr);
	      m_node = expr;
	      expr = NULL;
	    }
	  return ER_FAILED;
	}
      if (prm_get_integer_value (PRM_ID_COMPAT_MODE) == COMPAT_MYSQL)
	{
	  /* in mysql mode, PT_PLUS is not defined on date and number */
	  break;
	}
      /* PT_PLUS has four overloads for which we cannot apply symmetric rule 1. DATE/TIME type + NUMBER 2. NUMBER +
       * DATE/TIME type 3. DATE/TIME type + STRING 4. STRING + DATE/TIME type STRING/NUMBER operand involved is
       * coerced to BIGINT. For these overloads, PT_PLUS is a syntactic sugar for the ADD_DATE expression. Even
       * though both PLUS and MINUS have this behavior, we cannot treat them in the same place because, for this
       * case, PT_PLUS is commutative and PT_MINUS isn't */
      if (PT_IS_DATE_TIME_TYPE (arg1_type)
	  && (PT_IS_NUMERIC_TYPE (arg2_type) || PT_IS_CHAR_STRING_TYPE (arg2_type) || arg2_type == PT_TYPE_ENUMERATION
	      || arg2_type == PT_TYPE_MAYBE))
	{
	  if (!PT_IS_DISCRETE_NUMBER_TYPE (arg2_type))
	    {
	      /* coerce first argument to BIGINT */
	      int err = pt_coerce_expression_argument (m_parser, m_node, &arg2, PT_TYPE_BIGINT, NULL);
	      if (err != NO_ERROR)
		{
		  m_node->type_enum = PT_TYPE_NONE;
		  return ER_FAILED;
		}
	    }
	  m_node->info.expr.arg2 = arg2;
	  m_node->type_enum = arg1_type;
	  return ER_FAILED;
	}
      if (PT_IS_DATE_TIME_TYPE (arg2_type)
	  && (PT_IS_NUMERIC_TYPE (arg1_type) || PT_IS_CHAR_STRING_TYPE (arg1_type) || arg1_type == PT_TYPE_ENUMERATION
	      || arg1_type == PT_TYPE_MAYBE))
	{
	  if (!PT_IS_DISCRETE_NUMBER_TYPE (arg1_type))
	    {
	      int err = pt_coerce_expression_argument (m_parser, m_node, &arg1, PT_TYPE_BIGINT, NULL);
	      if (err != NO_ERROR)
		{
		  m_node->type_enum = PT_TYPE_NONE;
		  return ER_FAILED;
		}
	    }
	  m_node->info.expr.arg1 = arg1;
	  m_node->type_enum = arg2_type;
	  return ER_FAILED;
	}
      break;
    case PT_MINUS:
      if (arg1_type == PT_TYPE_NULL || arg2_type == PT_TYPE_NULL)
	{
	  m_node->type_enum = PT_TYPE_NULL;
	  return ER_FAILED;
	}
      if (arg1_type == PT_TYPE_MAYBE || arg2_type == PT_TYPE_MAYBE)
	{
	  m_node->type_enum = PT_TYPE_MAYBE;
	  // TODO: previously, goto cannot_use_signature
	  if (expr != NULL)
	    {
	      expr = pt_wrap_expr_w_exp_dom_cast (m_parser, expr);
	      m_node = expr;
	      expr = NULL;
	    }
	  return ER_FAILED;
	}

      if (prm_get_integer_value (PRM_ID_COMPAT_MODE) == COMPAT_MYSQL)
	{
	  /* in mysql mode - does is not defined on date and number */
	  break;
	}
      if (PT_IS_DATE_TIME_TYPE (arg1_type) && (PT_IS_NUMERIC_TYPE (arg2_type) || arg2_type == PT_TYPE_ENUMERATION))
	{
	  if (!PT_IS_DISCRETE_NUMBER_TYPE (arg2_type))
	    {
	      /* coerce arg2 to bigint */
	      int err = pt_coerce_expression_argument (m_parser, expr, &arg2, PT_TYPE_BIGINT, NULL);
	      if (err != NO_ERROR)
		{
		  m_node->type_enum = PT_TYPE_NONE;
		  return ER_FAILED;
		}
	      m_node->info.expr.arg2 = arg2;
	    }
	  m_node->type_enum = arg1_type;
	  return ER_FAILED;
	}
      break;
    case PT_BETWEEN_AND:
    case PT_BETWEEN_GE_LE:
    case PT_BETWEEN_GE_LT:
    case PT_BETWEEN_GT_LE:
    case PT_BETWEEN_GT_LT:
      /* these expressions will be handled by PT_BETWEEN */
      m_node->type_enum = pt_common_type (arg1_type, arg2_type);
      return ER_FAILED;
      break;
    case PT_BETWEEN:
    case PT_NOT_BETWEEN:
      /* between and range operators are written like: PT_BETWEEN(arg1, PT_BETWEEN_AND(arg2,arg3)) We convert it to
       * PT_BETWEEN(arg1, arg2, arg2) to be able to decide the correct common type of all arguments and we will
       * convert it back once we apply the correct casts */
      if (arg2->node_type == PT_EXPR && pt_is_between_range_op (arg2->info.expr.op))
	{
	  arg2 = m_node->info.expr.arg2;
	  m_node->info.expr.arg2 = arg2->info.expr.arg1;
	  m_node->info.expr.arg3 = arg2->info.expr.arg2;
	}
      break;
    case PT_LIKE:
    case PT_NOT_LIKE:
      /* [NOT] LIKE operators with an escape clause are parsed like PT_LIKE(arg1, PT_LIKE_ESCAPE(arg2, arg3)). We
       * convert it to PT_LIKE(arg1, arg2, arg3) to be able to decide the correct common type of all arguments and we
       * will convert it back once we apply the correct casts.
       *
       * A better approach would be to modify the m_parser to output PT_LIKE(arg1, arg2, arg3) directly. */

      if (arg2->node_type == PT_EXPR && arg2->info.expr.op == PT_LIKE_ESCAPE)
	{
	  arg2 = m_node->info.expr.arg2;
	  m_node->info.expr.arg2 = arg2->info.expr.arg1;
	  m_node->info.expr.arg3 = arg2->info.expr.arg2;
	}
      break;

    case PT_LIKE_LOWER_BOUND:
    case PT_LIKE_UPPER_BOUND:
      /* Check if arguments have been handled by PT_LIKE and only the result type needs to be set */
      if (arg1->type_enum == PT_TYPE_MAYBE && arg1->expected_domain)
	{
	  m_node->type_enum = pt_db_to_type_enum (TP_DOMAIN_TYPE (arg1->expected_domain));
	  return ER_FAILED;
	}
      break;

    case PT_IS_IN:
    case PT_IS_NOT_IN:
      if (arg2->node_type == PT_VALUE)
	{
	  if (PT_IS_COLLECTION_TYPE (arg2->type_enum) && arg2->info.value.data_value.set
	      && arg2->info.value.data_value.set->next == NULL)
	    {
	      /* only one element in set. convert expr as EQ/NE expr. */
	      PT_NODE *new_arg2;

	      new_arg2 = arg2->info.value.data_value.set;

	      /* free arg2 */
	      arg2->info.value.data_value.set = NULL;
	      parser_free_tree (m_parser, m_node->info.expr.arg2);

	      /* rewrite arg2 */
	      m_node->info.expr.arg2 = new_arg2;
	      m_node->info.expr.op = (op == PT_IS_IN) ? PT_EQ : PT_NE;
	    }
	  else if (PT_IS_NULL_NODE (arg2))
	    {
	      m_is_finished = true;
	      return NO_ERROR;
	    }
	}
      break;

    case PT_TO_CHAR:
      if (PT_IS_CHAR_STRING_TYPE (arg1_type) && PT_IS_NULL_NODE (arg2))
	{
	  arg1->line_number = m_node->line_number;
	  arg1->column_number = m_node->column_number;
	  arg1->alias_print = m_node->alias_print;
	  m_node->alias_print = NULL;
	  arg1->next = m_node->next;
	  m_node->next = NULL;
	  if (arg1->node_type == PT_EXPR)
	    {
	      arg1->info.expr.location = m_node->info.expr.location;
	    }
	  else if (arg1->node_type == PT_VALUE)
	    {
	      arg1->info.value.location = m_node->info.expr.location;
	    }
	  m_node->info.expr.arg1 = NULL;
	  parser_free_node (m_parser, m_node);

	  m_node = parser_copy_tree_list (m_parser, arg1);
	  parser_free_node (m_parser, arg1);

	  m_is_finished = true;
	  return NO_ERROR;
	}
      else if (PT_IS_NUMERIC_TYPE (arg1_type))
	{
	  bool has_user_format = false;
	  bool has_user_lang = false;
	  const char *lang_str;

	  assert (arg3 != NULL && arg3->node_type == PT_VALUE && arg3_type == PT_TYPE_INTEGER);
	  /* change locale from date_lang (set by grammar) to number_lang */
	  (void) lang_get_lang_id_from_flag (arg3->info.value.data_value.i, &has_user_format, &has_user_lang);
	  if (!has_user_lang)
	    {
	      int lang_flag;
	      lang_str = prm_get_string_value (PRM_ID_INTL_NUMBER_LANG);
	      (void) lang_set_flag_from_lang (lang_str, has_user_format, has_user_lang, &lang_flag);
	      arg3->info.value.data_value.i = lang_flag;
	      arg3->info.value.db_value_is_initialized = 0;
	      pt_value_to_db (m_parser, arg3);
	    }
	}

      break;

    case PT_FROM_TZ:
    case PT_NEW_TIME:
    {
      if (arg1_type != PT_TYPE_DATETIME && arg1_type != PT_TYPE_TIME && arg1_type != PT_TYPE_MAYBE)
	{
	  m_node->type_enum = PT_TYPE_NULL;
	  return ER_FAILED;
	}
    }
    break;

    default:
      break;
    }

  m_expr = expr;

  return NO_ERROR;
}

int
type_check_expr_helper::handle_special_enumeration_op ()
{
  /* handle special cases for the enumeration type */
  m_node = pt_fix_enumeration_comparison (m_parser, m_node);
  if (m_node == NULL)
    {
      m_is_finished = true;
      return NO_ERROR;
    }

  if (pt_has_error (m_parser))
    {
      // TODO: error handling?
      m_is_finished = true;
      return ER_FAILED;
    }

  m_is_finished = false;
  return NO_ERROR;
}

int
type_check_expr_helper::eval_function_holder ()
{
  PT_NODE *func = NULL;
  /* this may be a 2nd pass, tree may be already const folded */
  if (m_node->info.expr.arg1->node_type == PT_FUNCTION)
    {
      func = pt_eval_function_type (m_parser, m_node->info.expr.arg1);
      m_node->type_enum = func->type_enum;
      if (m_node->data_type == NULL && func->data_type != NULL)
	{
	  m_node->data_type = parser_copy_tree (m_parser, func->data_type);
	}
    }
  else
    {
      assert (m_node->info.expr.arg1->node_type == PT_VALUE);
    }

  m_is_finished = true;
  return NO_ERROR;
}

PT_TYPE_ENUM
type_check_expr_helper::traverse_multiargs_range_op (PT_NODE *arg2)
{
  PT_TYPE_ENUM type = PT_TYPE_NONE, common_type;
  if (arg2->or_next == NULL)
    {
      type = arg2->type_enum;
    }
  else
    {
      PT_NODE *temp;
      PT_TYPE_ENUM temp_type;

      common_type = PT_TYPE_NONE;
      /* do traverse multi-args in RANGE operator */
      for (temp = arg2; temp; temp = temp->or_next)
	{
	  temp_type = pt_common_type (type, temp->type_enum);
	  if (temp_type != PT_TYPE_NONE)
	    {
	      common_type = (common_type == PT_TYPE_NONE) ? temp_type : pt_common_type (common_type, temp_type);
	    }
	}
      type = common_type;
    }

  return type;
}

/*
* pt_fix_enumeration_comparison () - fix comparisons for enumeration type
* return : modified node or NULL
* parser (in) :
* expr (in) :
*/
PT_NODE *
pt_fix_enumeration_comparison (PARSER_CONTEXT *parser, PT_NODE *expr)
{
  PT_NODE *arg1 = NULL, *arg2 = NULL;
  PT_NODE *node = NULL, *save_next = NULL;
  PT_NODE *list = NULL, *list_prev = NULL, **list_start = NULL;
  PT_OP_TYPE op;
  if (expr == NULL || expr->node_type != PT_EXPR)
    {
      return expr;
    }
  op = expr->info.expr.op;
  arg1 = expr->info.expr.arg1;
  arg2 = expr->info.expr.arg2;
  if (arg1->type_enum == PT_TYPE_NULL || arg2->type_enum == PT_TYPE_NULL)
    {
      return expr;
    }

  switch (op)
    {
    case PT_EQ:
    case PT_NE:
    case PT_NULLSAFE_EQ:
      if (PT_IS_CONST (arg1))
	{
	  if (PT_IS_CONST (arg2))
	    {
	      /* const op const does not need special handling */
	      return expr;
	    }
	  /* switch arg1 with arg2 so that we have non cost operand on the left side */
	  node = arg1;
	  arg1 = arg2;
	  arg2 = node;
	}

      if (arg1->type_enum != PT_TYPE_ENUMERATION || !PT_IS_CONST (arg2))
	{
	  /* we're only handling enumeration comp const */
	  return expr;
	}
      if (pt_is_same_enum_data_type (arg1->data_type, arg2->data_type))
	{
	  return expr;
	}

      if (arg2->type_enum == PT_TYPE_ENUMERATION && arg2->data_type != NULL)
	{
	  TP_DOMAIN *domain = pt_data_type_to_db_domain (parser, arg2->data_type, NULL);
	  DB_VALUE *dbval = pt_value_to_db (parser, arg2);

	  if (domain == NULL)
	    {
	      return NULL;
	    }
	  if (dbval != NULL
	      && ((db_get_enum_string (dbval) == NULL && db_get_enum_short (dbval) == 0)
		  || ((db_get_enum_string (dbval) != NULL && db_get_enum_short (dbval) > 0)
		      && tp_domain_select (domain, dbval, 0, TP_EXACT_MATCH) != NULL)))
	    {
	      return expr;
	    }
	}
      break;
    case PT_IS_IN:
    case PT_IS_NOT_IN:
    case PT_EQ_SOME:
    case PT_NE_SOME:
    case PT_EQ_ALL:
    case PT_NE_ALL:
      break;
    default:
      return expr;
    }

  if (arg1->data_type == NULL || arg1->data_type->info.data_type.enumeration == NULL)
    {
      /* we don't know the actual enumeration type */
      return expr;
    }

  switch (op)
    {
    case PT_EQ:
    case PT_NE:
    case PT_NULLSAFE_EQ:
      node = pt_node_to_enumeration_expr (parser, arg1->data_type, arg2);
      if (node == NULL)
	{
	  return NULL;
	}
      arg2 = node;
      break;
    case PT_IS_IN:
    case PT_IS_NOT_IN:
    case PT_EQ_SOME:
    case PT_NE_SOME:
    case PT_EQ_ALL:
    case PT_NE_ALL:
      if (PT_IS_QUERY_NODE_TYPE (arg2->node_type))
	{
	  node = pt_select_list_to_enumeration_expr (parser, arg1->data_type, arg2);
	  if (node == NULL)
	    {
	      return NULL;
	    }
	  arg2 = node;
	  break;
	}
      /* not a subquery */
      switch (arg2->node_type)
	{
	case PT_VALUE:
	  assert (PT_IS_COLLECTION_TYPE (arg2->type_enum) || arg2->type_enum == PT_TYPE_EXPR_SET);
	  /* convert this value to a multiset */
	  node = parser_new_node (parser, PT_FUNCTION);
	  if (node == NULL)
	    {
	      PT_ERRORm (parser, expr, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
	      return NULL;
	    }
	  node->info.function.function_type = F_SET;
	  node->info.function.arg_list = arg2->info.value.data_value.set;
	  node->type_enum = arg2->type_enum;

	  arg2->info.value.data_value.set = NULL;
	  parser_free_tree (parser, arg2);
	  arg2 = node;

	/* fall through */

	case PT_FUNCTION:
	  list = arg2->info.function.arg_list;
	  list_start = &arg2->info.function.arg_list;
	  break;

	default:
	  return expr;
	}

      while (list != NULL)
	{
	  /* Skip nodes that already have been wrapped with PT_TO_ENUMERATION_VALUE expression or have the correct type
	   */
	  if ((list->node_type == PT_EXPR && list->info.expr.op == PT_TO_ENUMERATION_VALUE)
	      || (list->type_enum == PT_TYPE_ENUMERATION
		  && pt_is_same_enum_data_type (arg1->data_type, list->data_type)))
	    {
	      list_prev = list;
	      list = list->next;
	      continue;
	    }

	  save_next = list->next;
	  list->next = NULL;
	  node = pt_node_to_enumeration_expr (parser, arg1->data_type, list);
	  if (node == NULL)
	    {
	      return NULL;
	    }

	  node->next = save_next;
	  if (list_prev == NULL)
	    {
	      *list_start = node;
	    }
	  else
	    {
	      list_prev->next = node;
	    }
	  list_prev = node;
	  list = node->next;
	}
      if (arg2->data_type != NULL)
	{
	  parser_free_tree (parser, arg2->data_type);
	  arg2->data_type = NULL;
	}
      (void) pt_add_type_to_set (parser, arg2->info.function.arg_list, &arg2->data_type);
      break;

    default:
      break;
    }

  expr->info.expr.arg1 = arg1;
  expr->info.expr.arg2 = arg2;

  return expr;
}


/*
* pt_select_list_to_enumeration_expr () - wrap select list with
*					  PT_TO_ENUMERATION_VALUE expression
* return : new node or null
* parser (in) :
* data_type (in) :
* node (in) :
*/
PT_NODE *
pt_select_list_to_enumeration_expr (PARSER_CONTEXT *parser, PT_NODE *data_type, PT_NODE *node)
{
  PT_NODE *new_node = NULL;

  if (node == NULL || data_type == NULL)
    {
      return node;
    }

  if (!PT_IS_QUERY_NODE_TYPE (node->node_type))
    {
      return node;
    }
  switch (node->node_type)
    {
    case PT_SELECT:
    {
      PT_NODE *item = NULL;
      PT_NODE *prev = NULL;
      PT_NODE *select_list = node->info.query.q.select.list;
      for (item = select_list; item != NULL; prev = item, item = item->next)
	{
	  if (item->type_enum == PT_TYPE_ENUMERATION)
	    {
	      /* nothing to do here */
	      continue;
	    }
	  new_node = pt_node_to_enumeration_expr (parser, data_type, item);
	  if (new_node == NULL)
	    {
	      return NULL;
	    }
	  new_node->next = item->next;
	  item->next = NULL;
	  item = new_node;
	  /* first node in the list */
	  if (prev == NULL)
	    {
	      node->info.query.q.select.list = item;
	    }
	  else
	    {
	      prev->next = item;
	    }
	}
      break;
    }
    case PT_DIFFERENCE:
    case PT_INTERSECTION:
    case PT_UNION:
      new_node = pt_select_list_to_enumeration_expr (parser, data_type, node->info.query.q.union_.arg1);
      if (new_node == NULL)
	{
	  return NULL;
	}
      node->info.query.q.union_.arg1 = new_node;
      new_node = pt_select_list_to_enumeration_expr (parser, data_type, node->info.query.q.union_.arg2);
      if (new_node == NULL)
	{
	  return NULL;
	}
      node->info.query.q.union_.arg2 = new_node;
      break;
    default:
      break;
    }
  return node;
}

/*
* pt_node_to_enumeration_expr () - wrap node with PT_TO_ENUMERATION_VALUE
*				    expression
* return : new node or null
* parser (in) :
* data_type (in) :
* node (in) :
*/
PT_NODE *
pt_node_to_enumeration_expr (PARSER_CONTEXT *parser, PT_NODE *data_type, PT_NODE *node)
{
  PT_NODE *expr = NULL;
  if (parser == NULL || data_type == NULL || node == NULL)
    {
      assert (false);
      return NULL;
    }

  if (PT_HAS_COLLATION (node->type_enum) && node->data_type != NULL)
    {
      if (!INTL_CAN_COERCE_CS (node->data_type->info.data_type.units, data_type->info.data_type.units))
	{
	  PT_ERRORmf2 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_COERCE_UNSUPPORTED,
		       pt_short_print (parser, node), pt_show_type_enum (PT_TYPE_ENUMERATION));
	  return node;
	}
    }

  expr = parser_new_node (parser, PT_EXPR);
  if (expr == NULL)
    {
      PT_ERRORm (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OUT_OF_MEMORY);
      return NULL;
    }

  expr->info.expr.arg1 = node;
  expr->type_enum = PT_TYPE_ENUMERATION;
  expr->data_type = parser_copy_tree (parser, data_type);
  expr->info.expr.op = PT_TO_ENUMERATION_VALUE;
  return expr;
}

/*
 * pt_coerce_expression_argument () - check if arg has the correct type and
 *				      wrap arg with a cast node to def_type
 *				      if needed.
 *   return	  : NO_ERROR on success, ER_FAILED on error
 *   parser(in)	  : the parser context
 *   expr(in)	  : the expression to which arg belongs to
 *   arg (in/out) : the expression argument that will be checked
 *   def_type(in) : the type that was evaluated from the expression definition
 *   data_type(in): precision and scale information for arg
 */
int
pt_coerce_expression_argument (PARSER_CONTEXT *parser, PT_NODE *expr, PT_NODE **arg, const PT_TYPE_ENUM def_type,
			       PT_NODE *data_type)
{
  PT_NODE *node = *arg;
  PT_NODE *new_node = NULL, *new_dt = NULL;
  TP_DOMAIN *d;
  int scale = DB_DEFAULT_SCALE, precision = DB_DEFAULT_PRECISION;

  if (node == NULL)
    {
      if (def_type != PT_TYPE_NONE)
	{
	  return ER_FAILED;
	}
      return NO_ERROR;
    }

  if (node->type_enum == PT_TYPE_NULL)
    {
      /* no coercion needed for NULL arguments */
      return NO_ERROR;
    }

  if (def_type == node->type_enum)
    {
      return NO_ERROR;
    }

  if (def_type == PT_TYPE_LOGICAL)
    {
      /* no cast for type logical. this is an error and we should report it */
      return ER_FAILED;
    }

  /* set default scale and precision for parametrized types */
  switch (def_type)
    {
    case PT_TYPE_BIGINT:
      precision = DB_DEFAULT_PRECISION;
      scale = DB_DEFAULT_SCALE;
      break;

    case PT_TYPE_NUMERIC:
      if (PT_IS_DISCRETE_NUMBER_TYPE (node->type_enum))
	{
	  precision = DB_DEFAULT_NUMERIC_PRECISION;
	  scale = 0;
	}
      else
	{
	  precision = DB_DEFAULT_NUMERIC_PRECISION;
	  scale = DB_DEFAULT_NUMERIC_DIVISION_SCALE;
	}
      break;

    case PT_TYPE_VARCHAR:
      precision = TP_FLOATING_PRECISION_VALUE;
      scale = 0;
      break;

    case PT_TYPE_VARNCHAR:
      precision = TP_FLOATING_PRECISION_VALUE;
      scale = 0;
      break;
    case PT_TYPE_ENUMERATION:
    {
      /* Because enumerations should always be casted to a fully specified type, we only accept casting to an
       * enumeration type if we have a symmetrical expression (meaning that the arguments of the expression should
       * have the same type) and one of the arguments is already an enumeration. In this case, we will cast the
       * argument that is not an enumeration to the enumeration type of the other argument */
      PT_NODE *arg1, *arg2, *arg3;
      if (expr == NULL)
	{
	  assert (false);
	  return ER_FAILED;
	}
      if (!pt_is_symmetric_op (expr->info.expr.op))
	{
	  assert (false);
	  return ER_FAILED;
	}

      arg1 = expr->info.expr.arg1;
      arg2 = expr->info.expr.arg2;
      arg3 = expr->info.expr.arg3;
      /* we already know that arg is not an enumeration so we have to look for an enumeration between the other
       * arguments of expr */
      if (arg1 != NULL && arg1->type_enum == PT_TYPE_ENUMERATION)
	{
	  new_dt = arg1->data_type;
	}
      else if (arg2 != NULL && arg2->type_enum == PT_TYPE_ENUMERATION)
	{
	  new_dt = arg2->data_type;
	}
      else if (arg3 != NULL && arg3->type_enum == PT_TYPE_ENUMERATION)
	{
	  new_dt = arg3->data_type;
	}
      if (new_dt == NULL)
	{
	  assert (false);
	  PT_ERRORmf2 (parser, node, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_OP_NOT_DEFINED_ON_1,
		       pt_show_binopcode (expr->info.expr.op), pt_show_type_enum (PT_TYPE_ENUMERATION));
	  return ER_FAILED;
	}
      break;
    }
    default:
      precision = DB_DEFAULT_PRECISION;
      scale = DB_DEFAULT_SCALE;
      break;
    }

  if (node->type_enum == PT_TYPE_MAYBE)
    {
      if ((node->node_type == PT_EXPR && pt_is_op_hv_late_bind (node->info.expr.op))
	  || (node->node_type == PT_SELECT && node->info.query.is_subquery == PT_IS_SUBQUERY))
	{
	  /* wrap with cast, instead of setting expected domain */
	  new_node = pt_wrap_with_cast_op (parser, node, def_type, precision, scale, new_dt);

	  if (new_node == NULL)
	    {
	      return ER_FAILED;
	    }
	  /* reset expected domain of wrapped argument to NULL: it will be replaced at XASL generation with
	   * DB_TYPE_VARIABLE domain */
	  node->expected_domain = NULL;

	  *arg = new_node;
	}
      else
	{
	  if (new_dt != NULL)
	    {
	      d = pt_data_type_to_db_domain (parser, new_dt, NULL);
	    }
	  else
	    {
	      d =
		      tp_domain_resolve_default_w_coll (pt_type_enum_to_db (def_type), LANG_SYS_COLLATION,
			  TP_DOMAIN_COLL_LEAVE);
	    }
	  if (d == NULL)
	    {
	      return ER_FAILED;
	    }
	  /* make sure the returned domain is cached */
	  d = tp_domain_cache (d);
	  pt_set_expected_domain (node, d);
	}

      if (node->node_type == PT_HOST_VAR)
	{
	  pt_preset_hostvar (parser, node);
	}
    }
  else
    {
      if (PT_IS_COLLECTION_TYPE (def_type))
	{
	  if (data_type == NULL)
	    {
	      if (PT_IS_COLLECTION_TYPE (node->type_enum))
		{
		  data_type = parser_copy_tree_list (parser, node->data_type);
		}
	      else
		{
		  /* this is might not be an error and we might do more damage if we cast it */
		  return NO_ERROR;
		}
	    }
	  new_node = pt_wrap_collection_with_cast_op (parser, node, def_type, data_type, false);
	}
      else
	{
	  new_node = pt_wrap_with_cast_op (parser, node, def_type, precision, scale, new_dt);
	}
      if (new_node == NULL)
	{
	  return ER_FAILED;
	}
      if (prm_get_bool_value (PRM_ID_RETURN_NULL_ON_FUNCTION_ERRORS))
	{
	  assert (new_node->node_type == PT_EXPR);
	  PT_EXPR_INFO_SET_FLAG (new_node, PT_EXPR_INFO_CAST_NOFAIL);
	}

      *arg = new_node;
    }

  return NO_ERROR;
}

/*
 * pt_wrap_expr_w_exp_dom_cast () - checks if the expression requires wrapping
 *	      with a cast to the type set in expected domain and performs the
 *	      wrap with cast if needed.
 *
 *   return: new node (if wrap is performed), or unaltered node, if wrap is
 *	     not needed
 *   parser(in): parser context
 *   expr(in): expression node to be checked and wrapped
 */
PT_NODE *
pt_wrap_expr_w_exp_dom_cast (PARSER_CONTEXT *parser, PT_NODE *expr)
{
  /* expressions returning MAYBE, but with an expected domain are wrapped with cast */
  if (expr != NULL && expr->type_enum == PT_TYPE_MAYBE && pt_is_op_hv_late_bind (expr->info.expr.op)
      && expr->expected_domain != NULL)
    {
      PT_NODE *new_expr = NULL;

      if (expr->type_enum == PT_TYPE_ENUMERATION)
	{
	  /* expressions should not return PT_TYPE_ENUMERATION */
	  assert (false);
	  PT_INTERNAL_ERROR (parser, "INVALID expected domain (PT_TYPE_ENUMERATION)");
	  return NULL;
	}

      new_expr =
	      pt_wrap_with_cast_op (parser, expr, pt_db_to_type_enum (expr->expected_domain->type->id),
				    expr->expected_domain->precision, expr->expected_domain->scale, NULL);

      if (new_expr != NULL)
	{
	  /* reset expected domain of wrapped expression to NULL: it will be replaced at XASL generation with
	   * DB_TYPE_VARIABLE domain */
	  expr->expected_domain = NULL;
	  expr = new_expr;
	}
    }

  return expr;
}


/*
 * pt_coerce_expr_arguments - apply signature sig to the arguments of the
 *			      expression expr
 *  return	: the (possibly modified) expr or NULL on error
 *  parser(in)	: the parser context
 *  expr(in)	: the SQL expression
 *  arg1(in)	: first argument of the expression
 *  arg2(in)	: second argument of the expression
 *  arg3(in)	: third argument of the expression
 *  sig(in)	: the expression signature
 */
PT_NODE *
pt_coerce_expr_arguments (PARSER_CONTEXT *parser, PT_NODE *expr, PT_NODE *arg1, PT_NODE *arg2, PT_NODE *arg3,
			  EXPRESSION_SIGNATURE sig)
{
  PT_TYPE_ENUM arg1_type = PT_TYPE_NONE, arg2_type = PT_TYPE_NONE;
  PT_TYPE_ENUM arg3_type = PT_TYPE_NONE;
  PT_TYPE_ENUM arg1_eq_type = PT_TYPE_NONE, arg2_eq_type = PT_TYPE_NONE;
  PT_TYPE_ENUM arg3_eq_type = PT_TYPE_NONE;
  PT_TYPE_ENUM common_type = PT_TYPE_NONE;
  PT_NODE *arg1_dt = NULL;
  PT_NODE *arg2_dt = NULL;
  PT_NODE *arg3_dt = NULL;
  PT_OP_TYPE op;
  int error = NO_ERROR;
  PT_NODE *between = NULL;
  PT_NODE *between_ge_lt = NULL;
  PT_NODE *b1 = NULL;
  PT_NODE *b2 = NULL;

  op = expr->info.expr.op;

  arg1 = expr->info.expr.arg1;
  if (arg1)
    {
      arg1_type = arg1->type_enum;
    }

  arg2 = expr->info.expr.arg2;
  if (arg2)
    {
      arg2_type = arg2->type_enum;

      if (op == PT_WIDTH_BUCKET)
	{
	  arg2_type = pt_get_common_arg_type_of_width_bucket (parser, expr);
	}
    }

  arg3 = expr->info.expr.arg3;
  if (arg3)
    {
      arg3_type = arg3->type_enum;
    }

  arg1_eq_type = pt_get_equivalent_type_with_op (sig.arg1_type, arg1_type, op);
  arg2_eq_type = pt_get_equivalent_type_with_op (sig.arg2_type, arg2_type, op);
  arg3_eq_type = pt_get_equivalent_type_with_op (sig.arg3_type, arg3_type, op);

  if (pt_is_symmetric_op (op))
    {
      if (pt_is_enumeration_special_comparison (arg1, op, arg2))
	{
	  /* In case of 'ENUM = const' we need to convert the right argument to the ENUM type in order to preserve an
	   * eventual index scan on left argument */
	  common_type = PT_TYPE_ENUMERATION;
	}
      else
	{
	  /* We should make sure that, for symmetric operators, all arguments are of the same type. */
	  common_type = pt_infer_common_type (op, &arg1_eq_type, &arg2_eq_type, &arg3_eq_type, expr->expected_domain);
	}

      if (common_type == PT_TYPE_NONE)
	{
	  /* this is an error */
	  return NULL;
	}

      if (pt_is_range_or_comp (op)
	  && (!PT_IS_NUMERIC_TYPE (arg1_type) || !PT_IS_NUMERIC_TYPE (arg2_type)
	      || (arg3_type != PT_TYPE_NONE && !PT_IS_NUMERIC_TYPE (arg3_type))))
	{
	  /* cast value when comparing column with value */
	  if (PT_IS_NAME_NODE (arg1) && PT_IS_VALUE_NODE (arg2)
	      && (arg3_type == PT_TYPE_NONE || PT_IS_VALUE_NODE (arg3)) && arg1_type != PT_TYPE_ENUMERATION)
	    {
	      arg1_eq_type = arg2_eq_type = arg1_type;
	      if (arg3_type != PT_TYPE_NONE)
		{
		  arg3_eq_type = arg1_type;
		}

	      /* if column type is number (except NUMERIC) and operator is not EQ cast const node to DOUBLE to enhance
	       * correctness of results */
	      if (PT_IS_NUMERIC_TYPE (arg1_type) && arg1_type != PT_TYPE_NUMERIC && op != PT_EQ && op != PT_EQ_SOME
		  && op != PT_EQ_ALL)
		{
		  if (arg2_type != arg1_type)
		    {
		      arg2_eq_type = PT_TYPE_DOUBLE;
		    }
		  if (arg3_type != PT_TYPE_NONE && arg3_type != arg1_type)
		    {
		      arg3_eq_type = PT_TYPE_DOUBLE;
		    }
		}
	    }
	  else if (PT_IS_NAME_NODE (arg2) && PT_IS_VALUE_NODE (arg1) && arg3_type == PT_TYPE_NONE
		   && arg2_type != PT_TYPE_ENUMERATION)
	    {
	      arg1_eq_type = arg2_eq_type = arg2_type;
	      if (arg1_type != arg2_type && PT_IS_NUMERIC_TYPE (arg2_type) && arg2_type != PT_TYPE_NUMERIC
		  && op != PT_EQ && op != PT_EQ_SOME && op != PT_EQ_ALL)
		{
		  arg1_eq_type = PT_TYPE_DOUBLE;
		}
	    }
	  else if (PT_ARE_COMPARABLE_CHAR_TYPE (arg1_type, arg2_type))
	    {
	      if (PT_IS_NAME_NODE (arg1) && !PT_IS_NAME_NODE (arg2))
		{
		  arg1_eq_type = arg2_eq_type = arg1_type;
		  if (arg3_type != PT_TYPE_NONE)
		    {
		      arg3_eq_type = arg1_type;
		    }
		}
	      if (PT_IS_NAME_NODE (arg2) && !PT_IS_NAME_NODE (arg1))
		{
		  arg1_eq_type = arg2_eq_type = arg2_type;
		  if (arg3_type != PT_TYPE_NONE)
		    {
		      arg3_eq_type = arg2_type;
		    }
		}
	    }
	}

      if (pt_is_range_comp_op (op))
	{
	  if (PT_ARE_COMPARABLE_NO_CHAR (arg1_eq_type, arg1_type))
	    {
	      arg1_eq_type = arg1_type;
	    }
	  if (PT_ARE_COMPARABLE_NO_CHAR (arg2_eq_type, arg2_type))
	    {
	      arg2_eq_type = arg2_type;
	    }
	  if (PT_ARE_COMPARABLE_NO_CHAR (arg3_eq_type, arg3_type))
	    {
	      arg3_eq_type = arg3_type;
	    }
	}
      else if (pt_is_comp_op (op))
	{
	  /* do not cast between numeric types or char types for comparison operators */
	  if (PT_ARE_COMPARABLE (arg1_eq_type, arg1_type))
	    {
	      arg1_eq_type = arg1_type;
	    }
	  if (PT_ARE_COMPARABLE (arg2_eq_type, arg2_type))
	    {
	      arg2_eq_type = arg2_type;
	    }
	  if (PT_ARE_COMPARABLE (arg3_eq_type, arg3_type))
	    {
	      arg3_eq_type = arg3_type;
	    }
	}

      if (PT_IS_COLLECTION_TYPE (common_type))
	{
	  /* We do not perform implicit casts on collection types. The following code will only handle constant
	   * arguments. */
	  if ((arg1_eq_type != PT_TYPE_NONE && !PT_IS_COLLECTION_TYPE (arg1_eq_type))
	      || (arg2_eq_type != PT_TYPE_NONE && !PT_IS_COLLECTION_TYPE (arg2_eq_type))
	      || (arg3_eq_type != PT_TYPE_NONE && !PT_IS_COLLECTION_TYPE (arg3_eq_type)))
	    {
	      return NULL;
	    }
	  else
	    {
	      if (pt_is_symmetric_type (common_type))
		{
		  if (arg1_eq_type != common_type && arg1_eq_type != PT_TYPE_NONE)
		    {
		      pt_coerce_value (parser, arg1, arg1, common_type, PT_NODE_DATA_TYPE (arg1));
		      arg1_type = common_type;
		    }
		  if (arg2_type != common_type && arg2_eq_type != PT_TYPE_NONE)
		    {
		      pt_coerce_value (parser, arg2, arg2, common_type, PT_NODE_DATA_TYPE (arg2));
		      arg2_type = common_type;
		    }
		}
	      /* we're not casting collection types in this context but we should "propagate" types */
	      if (arg2_type != PT_TYPE_NONE)
		{
		  pt_propagate_types (parser, expr, PT_NODE_DATA_TYPE (arg1), PT_NODE_DATA_TYPE (arg2));
		}
	      if (arg3_type != PT_TYPE_NONE)
		{
		  pt_propagate_types (parser, expr, PT_NODE_DATA_TYPE (arg2), PT_NODE_DATA_TYPE (arg3));
		}
	      expr->info.expr.arg1 = arg1;
	      expr->info.expr.arg2 = arg2;
	      expr->info.expr.arg3 = arg3;
	      return expr;
	    }
	}
    }

  /* We might have decided a new type for arg1 based on the common_type but, if the signature defines an exact type, we
   * should keep it. For example, + is a symmetric operator but also defines date + bigint which is not symmetric and
   * we have to keep the bigint type even if the common type is date. This is why, before coercing expression
   * arguments, we check the signature that we decided to apply */
  if (sig.arg1_type.type == pt_arg_type::NORMAL)
    {
      arg1_eq_type = sig.arg1_type.val.type;
    }
  if (PT_IS_RECURSIVE_EXPRESSION (expr) && expr->info.expr.recursive_type != PT_TYPE_NONE)
    {
      /* In case of recursive expression (PT_GREATEST, PT_LEAST, ...) the common type is stored in recursive_type */
      arg1_eq_type = expr->info.expr.recursive_type;
    }
  error = pt_coerce_expression_argument (parser, expr, &arg1, arg1_eq_type, arg1_dt);
  if (error != NO_ERROR)
    {
      return NULL;
    }
  else
    {
      expr->info.expr.arg1 = arg1;
    }

  if (sig.arg2_type.type == pt_arg_type::NORMAL)
    {
      arg2_eq_type = sig.arg2_type.val.type;
    }
  if (PT_IS_RECURSIVE_EXPRESSION (expr) && expr->info.expr.recursive_type != PT_TYPE_NONE)
    {
      /* In case of recursive expression (PT_GREATEST, PT_LEAST, ...) the common type is stored in recursive_type */
      arg2_eq_type = expr->info.expr.recursive_type;
    }

  if (op != PT_WIDTH_BUCKET)
    {
      error = pt_coerce_expression_argument (parser, expr, &arg2, arg2_eq_type, arg2_dt);
      if (error != NO_ERROR)
	{
	  return NULL;
	}
      else
	{
	  expr->info.expr.arg2 = arg2;
	}
    }
  else
    {
      /* width_bucket is a special case. It has 4 params the 2nd and 3rd args are coerced here */
      between = expr->info.expr.arg2;
      if (between == NULL || between->node_type != PT_EXPR || between->info.expr.op != PT_BETWEEN)
	{
	  return NULL;
	}

      between_ge_lt = between->info.expr.arg2;
      if (between_ge_lt == NULL || between_ge_lt->node_type != PT_EXPR
	  || between_ge_lt->info.expr.op != PT_BETWEEN_GE_LT)
	{
	  return NULL;
	}

      /* 2nd, 3rd param of width_bucket */
      b1 = between_ge_lt->info.expr.arg1;
      b2 = between_ge_lt->info.expr.arg2;
      assert (b1 != NULL && b2 != NULL);

      error = pt_coerce_expression_argument (parser, between_ge_lt, &b1, arg2_eq_type, arg1_dt);
      if (error != NO_ERROR)
	{
	  return NULL;
	}
      else
	{
	  between_ge_lt->info.expr.arg1 = b1;
	}

      error = pt_coerce_expression_argument (parser, between_ge_lt, &b2, arg2_eq_type, arg2_dt);
      if (error != NO_ERROR)
	{
	  return NULL;
	}
      else
	{
	  between_ge_lt->info.expr.arg2 = b2;
	}
    }

  if (sig.arg3_type.type == pt_arg_type::NORMAL)
    {
      arg3_eq_type = sig.arg3_type.val.type;
    }
  error = pt_coerce_expression_argument (parser, expr, &arg3, arg3_eq_type, arg3_dt);
  if (error != NO_ERROR)
    {
      return NULL;
    }
  else
    {
      expr->info.expr.arg3 = arg3;
    }

  return expr;
}

static bool
does_op_specially_treat_null_arg (PT_OP_TYPE op)
{
  if (pt_is_operator_logical (op))
    {
      return true;
    }

  switch (op)
    {
    case PT_NVL:
    case PT_IFNULL:
    case PT_ISNULL:
    case PT_NVL2:
    case PT_COALESCE:
    case PT_NULLIF:
    case PT_TRANSLATE:
    case PT_RAND:
    case PT_DRAND:
    case PT_RANDOM:
    case PT_DRANDOM:
    case PT_CONCAT:
    case PT_CONCAT_WS:
    case PT_TO_CHAR:
      return true;
    case PT_REPLACE:
      return prm_get_bool_value (PRM_ID_ORACLE_STYLE_EMPTY_STRING);
    default:
      return false;
    }
}


/*
 * pt_infer_common_type - get the common type of the three arguments
 *
 *  return	  : the common type
 *  op(in)	  : expression identifier
 *  arg1(in/out)  : type of the first argument
 *  arg2(in/out)  : type of the second argument
 *  arg3(in/out)  : type of the third argument
 *
 * Notes: Unlike pt_common_type_op, this function infers a type for host
 *	  variables also. We can do this here because this function is called
 *	  after the expression signature has been applied. This means that
 *	  a type is left as PT_TYPE_MAYBE because the expression is symmetric
 *	  and the signature defines PT_GENERIC_TYPE_ANY or
 *	  PT_GENERIC_TYPE_PRIMITIVE for this argument.
 *	  There are some issues with collection types that this function does
 *	  not address. Collections are composed types so it's not enough to
 *	  decide that the common type is a collection type. We should also
 *	  infer the common type of the collection elements. This should be
 *	  done in the calling function because we have access to the actual
 *	  arguments there.
 */
PT_TYPE_ENUM
pt_infer_common_type (const PT_OP_TYPE op, PT_TYPE_ENUM *arg1, PT_TYPE_ENUM *arg2, PT_TYPE_ENUM *arg3,
		      const TP_DOMAIN *expected_domain)
{
  PT_TYPE_ENUM common_type = PT_TYPE_NONE;
  PT_TYPE_ENUM arg1_eq_type = *arg1;
  PT_TYPE_ENUM arg2_eq_type = *arg2;
  PT_TYPE_ENUM arg3_eq_type = *arg3;
  PT_TYPE_ENUM expected_type = PT_TYPE_NONE;
  assert (pt_is_symmetric_op (op));

  /* We ignore PT_TYPE_NONE arguments because, in the context of this function, if an argument is of type PT_TYPE_NONE
   * then it is not defined in the signature that we have decided to use. */

  if (expected_domain != NULL)
    {
      expected_type = pt_db_to_type_enum (TP_DOMAIN_TYPE (expected_domain));
    }

  common_type = arg1_eq_type;
  if (arg1_eq_type != PT_TYPE_NONE)
    {
      /* arg1 is defined */
      if (arg2_eq_type != PT_TYPE_NONE)
	{
	  /* arg2 is defined */
	  common_type = pt_common_type_op (arg1_eq_type, op, arg2_eq_type);
	  if (common_type == PT_TYPE_MAYBE && op != PT_COALESCE)
	    {
	      /* "mirror" the known argument type to the other argument if the later is PT_TYPE_MAYBE */
	      if (!pt_is_op_hv_late_bind (op))
		{
		  if (arg1_eq_type != PT_TYPE_MAYBE)
		    {
		      /* then arg2_eq_type is PT_TYPE_MAYBE */
		      arg2_eq_type = arg1_eq_type;
		      common_type = arg1_eq_type;
		    }
		  else if (arg2_eq_type != PT_TYPE_MAYBE)
		    {
		      arg1_eq_type = arg2_eq_type;
		      common_type = arg2_eq_type;
		    }
		}
	    }
	}
    }
  if (arg3_eq_type != PT_TYPE_NONE)
    {
      /* at this point either all arg1_eq_type, arg2_eq_type and common_type are PT_TYPE_MAYBE, or are already set to a
       * PT_TYPE_ENUM */
      common_type = pt_common_type_op (common_type, op, arg3_eq_type);
      if (common_type == PT_TYPE_MAYBE)
	{
	  /* either arg3_eq_type is PT_TYPE_MABYE or arg1, arg2 and common_type were PT_TYPE_MABYE */
	  if (arg3_eq_type == PT_TYPE_MAYBE)
	    {
	      if (arg1_eq_type != PT_TYPE_MAYBE)
		{
		  common_type = arg1_eq_type;
		  arg3_eq_type = common_type;
		}
	    }
	  else
	    {
	      arg1_eq_type = arg3_eq_type;
	      arg2_eq_type = arg3_eq_type;
	      common_type = arg3_eq_type;
	    }
	}
    }
  if (common_type == PT_TYPE_MAYBE && expected_type != PT_TYPE_NONE && !pt_is_op_hv_late_bind (op))
    {
      /* if expected type if not PT_TYPE_NONE then a expression higher up in the parser tree has set an expected domain
       * for this node and we can use it to set the expected domain of the arguments */
      common_type = expected_type;
    }

  /* final check : common_type should be PT_TYPE_MAYBE at this stage only for a small number of operators (PLUS,
   * MINUS,..) and only when at least one of the arguments is PT_TYPE_MAYBE; -when common type is PT_TYPE_MAYBE, then
   * leave all arguments as they are: either TYPE_MAYBE, either a concrete TYPE - without cast. The operator's
   * arguments will be resolved at execution in this case -if common type is a concrete type, then force all other
   * arguments to the common type (this should apply for most operators) */
  if (common_type != PT_TYPE_MAYBE)
    {
      *arg1 = (arg1_eq_type == PT_TYPE_NONE) ? PT_TYPE_NONE : common_type;
      *arg2 = (arg2_eq_type == PT_TYPE_NONE) ? PT_TYPE_NONE : common_type;
      *arg3 = (arg3_eq_type == PT_TYPE_NONE) ? PT_TYPE_NONE : common_type;
    }
  return common_type;
}


/*
 *  pt_apply_expressions_definition () - evaluate which expression signature
 *					 best matches the received arguments
 *					 and cast arguments to the types
 *					 described in the signature
 *  return	: NO_ERROR or error code
 *  parser(in)	: the parser context
 *  node(in/out): an SQL expression
 */
int
pt_apply_expressions_definition (PARSER_CONTEXT *parser, PT_NODE **node)
{
  PT_OP_TYPE op;
  PT_NODE *arg1 = NULL, *arg2 = NULL, *arg3 = NULL;
  PT_TYPE_ENUM arg1_type = PT_TYPE_NONE, arg2_type = PT_TYPE_NONE;
  PT_TYPE_ENUM arg3_type = PT_TYPE_NONE;
  PT_NODE *expr = *node;
  int matches = 0, best_match = -1, i = 0;
  EXPRESSION_SIGNATURE sig;

  if (expr->node_type != PT_EXPR)
    {
      return NO_ERROR;
    }
  op = expr->info.expr.op;

  EXPRESSION_DEFINITION def;
  if (pt_get_expression_definition (op, def) == false)
    {
      *node = NULL;
      return NO_ERROR;
    }

  if (PT_IS_RECURSIVE_EXPRESSION (expr) && expr->info.expr.recursive_type != PT_TYPE_NONE)
    {
      arg1_type = arg2_type = expr->info.expr.recursive_type;
    }
  else
    {
      arg1 = expr->info.expr.arg1;
      if (arg1)
	{
	  arg1_type = arg1->type_enum;
	}

      arg2 = expr->info.expr.arg2;
      if (arg2)
	{
	  arg2_type = arg2->type_enum;

	  if (op == PT_WIDTH_BUCKET)
	    {
	      arg2_type = pt_get_common_arg_type_of_width_bucket (parser, expr);
	    }
	}
    }

  arg3 = expr->info.expr.arg3;
  if (arg3)
    {
      arg3_type = arg3->type_enum;
    }

  /* check the expression contains NULL argument. If the op does not specially treat NULL args, for instance, NVL,
   * NVL2, IS [NOT] NULL and so on, just decide the retun type as NULL. */
  if (!does_op_specially_treat_null_arg (op)
      && ((arg1 && !arg1->flag.is_added_by_parser && arg1_type == PT_TYPE_NULL)
	  || (arg2 && !arg2->flag.is_added_by_parser && arg2_type == PT_TYPE_NULL)
	  || (arg3 && !arg3->flag.is_added_by_parser && arg3_type == PT_TYPE_NULL)))
    {
      expr->type_enum = PT_TYPE_NULL;
      return NO_ERROR;
    }

  matches = -1;
  best_match = 0;
  for (i = 0; i < (int) def.sigs.size (); i++)
    {
      int match_cnt = 0;
      if (pt_are_unmatchable_types (def.sigs[i].arg1_type, arg1_type))
	{
	  match_cnt = -1;
	  continue;
	}
      if (pt_are_equivalent_types (def.sigs[i].arg1_type, arg1_type))
	{
	  match_cnt++;
	}
      if (pt_are_unmatchable_types (def.sigs[i].arg2_type, arg2_type))
	{
	  match_cnt = -1;
	  continue;
	}
      if (pt_are_equivalent_types (def.sigs[i].arg2_type, arg2_type))
	{
	  match_cnt++;
	}
      if (pt_are_unmatchable_types (def.sigs[i].arg3_type, arg3_type))
	{
	  match_cnt = -1;
	  continue;
	}
      if (pt_are_equivalent_types (def.sigs[i].arg3_type, arg3_type))
	{
	  match_cnt++;
	}
      if (match_cnt == 3)
	{
	  best_match = i;
	  break;
	}
      else if (match_cnt > matches)
	{
	  matches = match_cnt;
	  best_match = i;
	}
    }

  if (best_match == -1)
    {
      /* if best_match is -1 then we have an expression definition but it cannot be applied on this arguments. */
      expr->node_type = PT_NODE_NONE;
      return ER_FAILED;
    }

  sig = def.sigs[best_match];
  if (pt_is_range_expression (op))
    {
      /* Range expressions are expressions that compare an argument with a subquery or a collection. We handle these
       * expressions separately */
      expr = pt_coerce_range_expr_arguments (parser, expr, arg1, arg2, arg3, sig);
    }
  else
    {
      expr = pt_coerce_expr_arguments (parser, expr, arg1, arg2, arg3, sig);
    }
  if (expr == NULL)
    {
      return ER_FAILED;
    }

  if (pt_is_op_hv_late_bind (op)
      && (arg1_type == PT_TYPE_MAYBE || arg2_type == PT_TYPE_MAYBE || arg3_type == PT_TYPE_MAYBE))
    {
      expr->type_enum = PT_TYPE_MAYBE;
    }
  else
    {
      expr->type_enum = pt_expr_get_return_type (expr, sig);
    }

  /* re-read arguments to include the wrapped-cast */
  arg1 = expr->info.expr.arg1;
  arg2 = expr->info.expr.arg2;

  if (PT_IS_PARAMETERIZED_TYPE (expr->type_enum)
      && pt_upd_domain_info (parser, arg1, arg2, op, expr->type_enum, expr) != NO_ERROR)
    {
      expr = NULL;
      return ER_FAILED;
    }
  *node = expr;
  return NO_ERROR;
}

/*
 * pt_are_unmatchable_types () - check if the two types cannot be matched
 *   return	  : true if the two types cannot be matched
 *   def_type(in) : an expression definition type
 *   op_type(in)  : an argument type
 */
bool
pt_are_unmatchable_types (const PT_ARG_TYPE def_type, const PT_TYPE_ENUM op_type)
{
  /* PT_TYPE_NONE does not match anything */
  if (op_type == PT_TYPE_NONE && ! (def_type.type == pt_arg_type::NORMAL && def_type.val.type == PT_TYPE_NONE))
    {
      return true;
    }

  if (op_type != PT_TYPE_NONE && def_type.type == pt_arg_type::NORMAL && def_type.val.type == PT_TYPE_NONE)
    {
      return true;
    }

  return false;
}


/*
 * pt_coerce_range_expr_arguments - apply signature sig to the arguments of the
 *				 logical expression expr
 *  return	: the (possibly modified) expr or NULL on error
 *  parser(in)	: the parser context
 *  expr(in)	: the SQL expression
 *  arg1(in)	: first argument of the expression
 *  arg2(in)	: second argument of the expression
 *  arg3(in)	: third argument of the expression
 *  sig(in)	: the expression signature
 *
 */
PT_NODE *
pt_coerce_range_expr_arguments (PARSER_CONTEXT *parser, PT_NODE *expr, PT_NODE *arg1, PT_NODE *arg2, PT_NODE *arg3,
				EXPRESSION_SIGNATURE sig)
{
  PT_TYPE_ENUM arg1_type = PT_TYPE_NONE, arg2_type = PT_TYPE_NONE;
  PT_TYPE_ENUM arg3_type = PT_TYPE_NONE;
  PT_TYPE_ENUM arg1_eq_type = PT_TYPE_NONE, arg2_eq_type = PT_TYPE_NONE;
  PT_TYPE_ENUM arg3_eq_type = PT_TYPE_NONE;
  PT_TYPE_ENUM common_type = PT_TYPE_NONE;
  PT_OP_TYPE op;
  int error = NO_ERROR;

  op = expr->info.expr.op;

  arg1 = expr->info.expr.arg1;
  if (arg1)
    {
      arg1_type = arg1->type_enum;
    }

  arg2 = expr->info.expr.arg2;
  if (arg2)
    {
      arg2_type = arg2->type_enum;
    }

  arg3 = expr->info.expr.arg3;
  if (arg3)
    {
      arg3_type = arg3->type_enum;
    }

  arg1_eq_type = pt_get_equivalent_type (sig.arg1_type, arg1_type);
  arg2_eq_type = pt_get_equivalent_type (sig.arg2_type, arg2_type);
  arg3_eq_type = pt_get_equivalent_type (sig.arg3_type, arg3_type);

  /* for range expressions the second argument may be a collection or a query. */
  if (PT_IS_QUERY_NODE_TYPE (arg2->node_type))
    {
      /* the select list must have only one element and the first argument has to be of the same type as the argument
       * from the select list */
      PT_NODE *arg2_list = NULL;

      /* duplicates are not relevant; order by is not relevant; */
      expr->info.expr.arg2->info.query.all_distinct = PT_DISTINCT;
      pt_try_remove_order_by (parser, arg2);

      arg2_list = pt_get_select_list (parser, arg2);
      if (arg2_list == NULL)
	{
	  return NULL;
	}

      if (PT_IS_COLLECTION_TYPE (arg2_list->type_enum) && arg2_list->node_type == PT_FUNCTION)
	{
	  expr->type_enum = PT_TYPE_LOGICAL;
	  return expr;
	}
      if (pt_length_of_select_list (arg2_list, EXCLUDE_HIDDEN_COLUMNS) != 1)
	{
	  PT_ERRORm (parser, arg2, MSGCAT_SET_PARSER_SEMANTIC, MSGCAT_SEMANTIC_NOT_SINGLE_COL);
	  return NULL;
	}
      arg2_type = arg2_list->type_enum;
      if (pt_is_enumeration_special_comparison (arg1, op, arg2))
	{
	  /* In case of 'ENUM IN [query]' we need to convert all elements of right argument to the ENUM type in order
	   * to preserve an eventual index scan on left argument */
	  common_type = PT_TYPE_ENUMERATION;
	}
      else
	{
	  common_type = pt_common_type_op (arg1_eq_type, op, arg2_type);
	}
      if (PT_IS_COLLECTION_TYPE (common_type))
	{
	  /* we cannot make a decision during type checking in this case */
	  return expr;
	}
      if ((PT_IS_NUMERIC_TYPE (arg1_type) && PT_IS_NUMERIC_TYPE (common_type)) || common_type == PT_TYPE_MAYBE)
	{
	  /* do not cast between numeric types */
	  arg1_eq_type = arg1_type;
	}
      else
	{
	  arg1_eq_type = common_type;
	}
      if ((PT_IS_NUMERIC_TYPE (arg2_type) && PT_IS_NUMERIC_TYPE (common_type)) || common_type == PT_TYPE_MAYBE)
	{
	  arg2_eq_type = arg2_type;
	}
      else
	{
	  arg2_eq_type = common_type;
	}

      error = pt_coerce_expression_argument (parser, expr, &arg1, arg1_eq_type, NULL);
      if (error != NO_ERROR)
	{
	  return NULL;
	}
      else
	{
	  expr->info.expr.arg1 = arg1;
	}

      if (pt_wrap_select_list_with_cast_op (parser, arg2, arg2_eq_type, 0, 0, NULL, false) != NO_ERROR)
	{
	  return NULL;
	}
    }
  else if (PT_IS_COLLECTION_TYPE (arg2_type))
    {
      /* Because we're using collections, semantically, all three cases below are valid: 1. SELECT * FROM tbl WHERE
       * int_col in {integer, set, object, date} 2. SELECT * FROM tbl WHERE int_col in {str, str, str} 3. SELECT * FROM
       * tbl WHERE int_col in {integer, integer, integer} We will only coerce arg2 if there is a common type between
       * arg1 and all elements from the collection arg2. We do not consider the case in which we cannot discern a
       * common type to be a semantic error and we rely on the functionality of the comparison operators to be applied
       * correctly on the expression during expression evaluation. This means that we consider that the user knew what
       * he was doing in the first case but wanted to write something else in the second case. */
      PT_TYPE_ENUM collection_type = PT_TYPE_NONE;
      PT_TYPE_ENUM arg1_type = arg1->type_enum;
      bool should_cast = false;
      PT_NODE *data_type = NULL;

      if (pt_is_enumeration_special_comparison (arg1, op, arg2))
	{
	  /* In case of 'ENUM IN (...)' we need to convert all elements of right argument to the ENUM type in order to
	   * preserve an eventual index scan on left argument */
	  common_type = arg1_eq_type = collection_type = PT_TYPE_ENUMERATION;
	}
      else
	{
	  common_type = collection_type = pt_get_common_collection_type (arg2, &should_cast);
	  if (common_type != PT_TYPE_NONE)
	    {
	      common_type = pt_common_type_op (arg1_eq_type, op, common_type);
	      if (common_type != PT_TYPE_NONE)
		{
		  /* collection's common type is STRING type, casting may not be needed */
		  if (!PT_IS_CHAR_STRING_TYPE (common_type) || !PT_IS_CHAR_STRING_TYPE (arg1_eq_type))
		    {
		      arg1_eq_type = common_type;
		    }
		}
	    }
	}
      if (common_type == PT_TYPE_OBJECT || PT_IS_COLLECTION_TYPE (common_type))
	{
	  /* we cannot make a cast decision here. to keep backwards compatibility we will call pt_coerce_value which
	   * will only work on constants */
	  PT_NODE *temp = NULL, *msg_temp = NULL, *temp2 = NULL, *elem = NULL;
	  int idx;

	  if (pt_coerce_value (parser, arg1, arg1, common_type, NULL) != NO_ERROR)
	    {
	      expr->type_enum = PT_TYPE_NONE;
	    }

	  /* case of "(col1,col2) in (('a','a'),('a ','a '))" */
	  /* In this case, Reset ('a','a') type from CHAR to VARCHAR based on (col1,col2) type. */
	  /* TO_DO: A set of set type evaluation routine should be added. */
	  if (pt_is_set_type (arg1) && PT_IS_FUNCTION (arg1) && pt_is_set_type (arg2) && PT_IS_VALUE_NODE (arg2))
	    {
	      for (temp = arg1->info.function.arg_list, idx = 0; temp; temp = temp->next, idx++)
		{
		  if (temp->type_enum == PT_TYPE_VARCHAR || temp->type_enum == PT_TYPE_VARNCHAR)
		    {
		      for (temp2 = arg2->info.value.data_value.set; temp2; temp2 = temp2->next)
			{
			  if (pt_is_set_type (temp2) && PT_IS_VALUE_NODE (temp2))
			    {
			      elem = temp2->info.value.data_value.set;
			      for (int i = 0; i < idx && elem; i++)
				{
				  elem = elem->next;
				}
			      if (elem && (elem->type_enum == PT_TYPE_CHAR || elem->type_enum == PT_TYPE_NCHAR))
				{
				  (void) pt_coerce_value (parser, elem, elem, temp->type_enum, elem->data_type);
				}
			    }
			}
		    }
		}
	      /*
	         (void) pt_coerce_value (parser, arg2, arg2, PT_TYPE_SET, arg2->data_type);
	         This routine is disabled.
	         Because arg2->data_type(set of set) might not be evaluated correctly, the corresponding routine is disabled.
	         Enable it after the data type of set of set is evaluated correctly.
	         The same routine is performed in eliminate_duplicated_keys().
	         Here, the type is kept to MULTISET"
	       */
	    }
	  else
	    {
	      msg_temp = parser->error_msgs;
	      (void) pt_coerce_value (parser, arg2, arg2, PT_TYPE_SET, arg2->data_type);
	      if (arg2->node_type == PT_VALUE)
		{
		  for (temp = arg2->info.value.data_value.set; temp; temp = temp->next)
		    {
		      if (common_type != temp->type_enum)
			{
			  msg_temp = parser->error_msgs;
			  parser->error_msgs = NULL;
			  (void) pt_coerce_value (parser, temp, temp, common_type, NULL);
			  if (pt_has_error (parser))
			    {
			      parser_free_tree (parser, parser->error_msgs);
			    }
			  parser->error_msgs = msg_temp;
			}
		    }
		}
	    }

	  return expr;
	}

      if (PT_IS_NUMERIC_TYPE (arg1_type) && PT_IS_NUMERIC_TYPE (arg1_eq_type))
	{
	  /* do not cast between numeric types */
	  arg1_eq_type = arg1_type;
	}
      else
	{
	  error = pt_coerce_expression_argument (parser, expr, &arg1, arg1_eq_type, NULL);
	  if (arg1 == NULL)
	    {
	      return NULL;
	    }
	  expr->info.expr.arg1 = arg1;
	}

      /* verify if we should cast arg2 to appropriate type */
      if (common_type == PT_TYPE_NONE	/* check if there is a valid common type between members of arg2 and arg1. */
	  || (!should_cast	/* check if there are at least two different types in arg2 */
	      && (collection_type == common_type
		  || (PT_IS_NUMERIC_TYPE (collection_type) && PT_IS_NUMERIC_TYPE (common_type)))))
	{
	  return expr;
	}

      /* we can perform an implicit cast here */
      data_type = parser_new_node (parser, PT_DATA_TYPE);
      if (data_type == NULL)
	{
	  return NULL;
	}
      data_type->info.data_type.dec_precision = 0;
      data_type->info.data_type.precision = 0;
      data_type->type_enum = common_type;
      if (PT_IS_PARAMETERIZED_TYPE (common_type))
	{
	  PT_NODE *temp = NULL;
	  int precision = 0, scale = 0;
	  int units = LANG_SYS_CODESET;	/* code set */
	  int collation_id = LANG_SYS_COLLATION;	/* collation_id */
	  bool keep_searching = true;
	  for (temp = arg2->data_type; temp != NULL && keep_searching; temp = temp->next)
	    {
	      if (temp->type_enum == PT_TYPE_NULL)
		{
		  continue;
		}

	      switch (common_type)
		{
		case PT_TYPE_CHAR:
		case PT_TYPE_NCHAR:
		case PT_TYPE_BIT:
		  /* CHAR, NCHAR types can be common type for one of all arguments is string type */
		  if (precision < temp->info.data_type.precision)
		    {
		      precision = temp->info.data_type.precision;
		    }
		  break;
		case PT_TYPE_VARCHAR:
		case PT_TYPE_VARNCHAR:
		  /* either all elements are already of string types or we set maximum precision */
		  if (!PT_IS_CHAR_STRING_TYPE (temp->type_enum))
		    {
		      precision = TP_FLOATING_PRECISION_VALUE;
		      /* no need to look any further, we've already found a type for which we have to set maximum
		       * precision */
		      keep_searching = false;
		      break;
		    }
		  if (precision < temp->info.data_type.precision)
		    {
		      precision = temp->info.data_type.precision;
		    }
		  break;
		case PT_TYPE_VARBIT:
		  /* either all elements are already of bit types or we set maximum precision */
		  if (!PT_IS_BIT_STRING_TYPE (temp->type_enum))
		    {
		      precision = TP_FLOATING_PRECISION_VALUE;
		      /* no need to look any further, we've already found a type for which we have to set maximum
		       * precision */
		      keep_searching = false;
		      break;
		    }
		  if (precision < temp->info.data_type.precision)
		    {
		      precision = temp->info.data_type.precision;
		    }
		  break;
		case PT_TYPE_NUMERIC:
		  /* either all elements are numeric or all are discrete numbers */
		  if (temp->type_enum == PT_TYPE_NUMERIC)
		    {
		      if (precision < temp->info.data_type.precision)
			{
			  precision = temp->info.data_type.precision;
			}
		      if (scale < temp->info.data_type.dec_precision)
			{
			  scale = temp->info.data_type.dec_precision;
			}
		    }
		  else if (PT_IS_DISCRETE_NUMBER_TYPE (temp->type_enum))
		    {
		      if (precision < TP_BIGINT_PRECISION)
			{
			  precision = TP_BIGINT_PRECISION;
			}
		    }
		  else
		    {
		      assert (false);
		    }
		  break;

		default:
		  assert (false);
		  break;
		}

	      if (PT_IS_STRING_TYPE (common_type) && PT_IS_STRING_TYPE (temp->type_enum))
		{
		  /* A bigger codesets's number can represent more characters. */
		  /* to_do : check to use functions pt_common_collation() or pt_make_cast_with_compatble_info(). */
		  if (units < temp->info.data_type.units)
		    {
		      units = temp->info.data_type.units;
		      collation_id = temp->info.data_type.collation_id;
		    }
		}
	    }
	  data_type->info.data_type.precision = precision;
	  data_type->info.data_type.dec_precision = scale;
	  data_type->info.data_type.units = units;
	  data_type->info.data_type.collation_id = collation_id;
	}

      arg2 = pt_wrap_collection_with_cast_op (parser, arg2, sig.arg2_type.val.type, data_type, false);
      if (!arg2)
	{
	  return NULL;
	}

      expr->info.expr.arg2 = arg2;
    }
  else if (PT_IS_HOSTVAR (arg2))
    {
      TP_DOMAIN *d = tp_domain_resolve_default (pt_type_enum_to_db (PT_TYPE_SET));
      pt_set_expected_domain (arg2, d);
      pt_preset_hostvar (parser, arg2);

      error = pt_coerce_expression_argument (parser, expr, &arg1, arg1_eq_type, NULL);
      if (error != NO_ERROR)
	{
	  return NULL;
	}
      expr->info.expr.arg1 = arg1;
    }
  else
    {
      /* This is a semantic error */
      return NULL;
    }
  return expr;
}


/*
 * pt_expr_get_return_type () - get the return type of an expression based on
 *				the expression signature and the types of its
 *				arguments
 *  return  : the expression return type
 *  expr(in): an SQL expression
 *  sig(in) : the expression signature
 *
 *  Remarks: This function is called after the expression signature has been
 *	     decided and the expression arguments have been wrapped with CASTs
 *	     to the type described in the signature. At this point we can
 *	     decide the return type based on the argument types which are
 *	     proper CUBRID types (i.e.: not generic types)
 */
PT_TYPE_ENUM
pt_expr_get_return_type (PT_NODE *expr, const EXPRESSION_SIGNATURE sig)
{
  PT_TYPE_ENUM arg1_type = PT_TYPE_NONE, arg2_type = PT_TYPE_NONE;
  PT_TYPE_ENUM arg3_type = PT_TYPE_NONE;

  if (sig.return_type.type == pt_arg_type::NORMAL)
    {
      /* if the signature does not define a generic type, return the defined type */
      return sig.return_type.val.type;
    }

  if (expr->info.expr.arg1)
    {
      arg1_type = expr->info.expr.arg1->type_enum;
      if (arg1_type == PT_TYPE_MAYBE)
	{
	  if (expr->info.expr.arg1->expected_domain)
	    {
	      /* we were able to decide an expected domain for this argument and we can use it in deciding the return
	       * type */
	      arg1_type = pt_db_to_type_enum (expr->info.expr.arg1->expected_domain->type->id);
	    }
	}
    }

  if (expr->info.expr.arg2)
    {
      arg2_type = expr->info.expr.arg2->type_enum;
      if (arg2_type == PT_TYPE_MAYBE)
	{
	  if (expr->info.expr.arg2->expected_domain)
	    {
	      /* we were able to decide an expected domain for this argument and we can use it in deciding the return
	       * type */
	      arg2_type = pt_db_to_type_enum (expr->info.expr.arg2->expected_domain->type->id);
	    }
	}
    }

  if (expr->info.expr.arg3)
    {
      arg3_type = expr->info.expr.arg3->type_enum;
      if (arg3_type == PT_TYPE_MAYBE)
	{
	  if (expr->info.expr.arg3->expected_domain)
	    {
	      /* we were able to decide an expected domain for this argument and we can use it in deciding the return
	       * type */
	      arg3_type = pt_db_to_type_enum (expr->info.expr.arg3->expected_domain->type->id);
	    }
	}
    }

  /* the return type of the signature is a generic type */
  switch (sig.return_type.val.generic_type)
    {
    case PT_GENERIC_TYPE_STRING:
    {
      /* The return type might be CHAR, VARCHAR, NCHAR or VARNCHAR. Since not all arguments are required to be of
       * string type, we have to infer the return type based only on the string type arguments */
      PT_TYPE_ENUM common_type = PT_TYPE_NONE;
      if (PT_IS_STRING_TYPE (arg1_type))
	{
	  common_type = arg1_type;
	  if (PT_IS_STRING_TYPE (arg2_type))
	    {
	      common_type = pt_common_type (arg1_type, arg2_type);
	      if (PT_IS_STRING_TYPE (arg3_type))
		{
		  common_type = pt_common_type (common_type, arg3_type);
		}
	      return common_type;
	    }

	  if (PT_IS_STRING_TYPE (arg3_type))
	    {
	      common_type = pt_common_type (common_type, arg3_type);
	    }
	  return common_type;
	}
      /* arg1 is not string type */
      if (PT_IS_STRING_TYPE (arg2_type))
	{
	  common_type = arg2_type;
	  if (PT_IS_STRING_TYPE (arg3_type))
	    {
	      common_type = pt_common_type (common_type, arg3_type);
	    }
	  return common_type;
	}

      /* arg1 and arg2 are not of string type */
      if (PT_IS_STRING_TYPE (arg3_type))
	{
	  return arg3_type;
	}

      if (common_type != PT_TYPE_NONE)
	{
	  return common_type;
	}
      break;
    }

    case PT_GENERIC_TYPE_STRING_VARYING:
    {
      PT_ARG_TYPE type (PT_GENERIC_TYPE_NCHAR);
      /* if one or the arguments is of national string type the return type must be VARNCHAR, else it is VARCHAR */
      if (pt_are_equivalent_types (type, arg1_type) || pt_are_equivalent_types (type, arg2_type)
	  || pt_are_equivalent_types (type, arg3_type))
	{
	  return PT_TYPE_VARNCHAR;
	}
      return PT_TYPE_VARCHAR;
    }

    case PT_GENERIC_TYPE_CHAR:
      if (arg1_type == PT_TYPE_VARCHAR || arg2_type == PT_TYPE_VARCHAR || arg3_type == PT_TYPE_VARCHAR)
	{
	  return PT_TYPE_VARCHAR;
	}
      return PT_TYPE_CHAR;

    case PT_GENERIC_TYPE_NCHAR:
      if (arg1_type == PT_TYPE_VARNCHAR || arg2_type == PT_TYPE_VARNCHAR || arg3_type == PT_TYPE_VARNCHAR)
	{
	  return PT_TYPE_VARNCHAR;
	}
      return PT_TYPE_NCHAR;

    case PT_GENERIC_TYPE_NUMBER:
    case PT_GENERIC_TYPE_DISCRETE_NUMBER:
    case PT_GENERIC_TYPE_ANY:
    case PT_GENERIC_TYPE_DATE:
    case PT_GENERIC_TYPE_DATETIME:
    case PT_GENERIC_TYPE_SEQUENCE:
    case PT_GENERIC_TYPE_BIT:
    {
      PT_TYPE_ENUM common_type = PT_TYPE_NONE;
      if (arg2_type == PT_TYPE_NONE
	  || (!pt_is_symmetric_op (expr->info.expr.op) && !pt_is_op_with_forced_common_type (expr->info.expr.op)))
	{
	  return arg1_type;
	}
      common_type = pt_common_type (arg1_type, arg2_type);
      if (arg3_type != PT_TYPE_NONE)
	{
	  common_type = pt_common_type (common_type, arg3_type);
	}

      if (common_type != PT_TYPE_NONE)
	{
	  return common_type;
	}
      break;
    }
    default:
      break;
    }

  /* we might reach this point on expressions with a single argument of value null */
  if (arg1_type == PT_TYPE_NULL)
    {
      return PT_TYPE_NULL;
    }
  return PT_TYPE_NONE;
}
