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
 * type_check_expr_def.cpp: definitions of each operator's expression definitions
 */

#include "type_check_expr.hpp"

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
      // case 2) maybe a new op is defined and it omitted to define a expression definition
      // TODO: need assertion?
      assert (false);
      return false;
    }

  def.op = op;
  def.sigs = sigs;

  return true;
}
