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


/*
 * jsp_tt.hpp - Java Stored Procedure Server Module Header
 *
 * Note:
 */

#include "jsp_packing.hpp"

#include "dbtype.h"
#include "dbtype_def.h"

#include "db_date.h"
#include "jsp_comm.h"
#include "numeric_opfunc.h"
#include "set_object.h"
#include "object_primitive.h"

namespace cubprocedure
{
  sp_info::sp_info ()
    : name ()
    , args ()
    , arg_count (0)
    , return_type (0)
  {

  }

  void
  sp_info::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (SP_CODE_INVOKE);
    serializator.pack_string (name);
    serializator.pack_int (arg_count);

    for (const sp_arg &arg : args)
      {
	arg.pack (serializator);
      }
  }

  size_t
  sp_arg::get_packed_size (cubpacking::packer &serializator, std::size_t start_offset) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // mode
    size += serializator.get_packed_int_size (size); // arg_data_type
    size += get_value_packed_size (serializator, size, value);
    return size;
  }

  size_t
  sp_arg::get_value_packed_size (cubpacking::packer &serializator, std::size_t start_offset, DB_VALUE *val) const
  {
    size_t size = serializator.get_packed_int_size (start_offset); // param_type
    size += serializator.get_packed_int_size (size); // value's size
	
    int param_type = db_value_type (val);
    switch (param_type)
      {
      case DB_TYPE_INTEGER:
	size += serializator.get_packed_int_size (size);
	break;

      case DB_TYPE_BIGINT:
	size += serializator.get_packed_bigint_size (size);
	break;

      case DB_TYPE_SHORT:
	size += serializator.get_packed_short_size (size);
	break;

      case DB_TYPE_FLOAT:
	size += serializator.get_packed_float_size (size);
	break;

      case DB_TYPE_DOUBLE:
	size += serializator.get_packed_double_size (size);
	break;

      case DB_TYPE_NUMERIC:
      {
	char str_buf[NUMERIC_MAX_STRING_SIZE];
	numeric_db_value_print (val, str_buf);
	size += serializator.get_packed_c_string_size (str_buf, strlen (str_buf), size);
      }
      break;

      case DB_TYPE_CHAR:
      case DB_TYPE_NCHAR:
      case DB_TYPE_VARNCHAR:
      case DB_TYPE_STRING:
	// TODO: support unicode decomposed string
      {
	size += serializator.get_packed_string_size (db_get_string (val), size);
      }
      break;

      case DB_TYPE_BIT:
      case DB_TYPE_VARBIT:
	// NOTE: This type was not implemented at the previous version
	break;

      case DB_TYPE_DATE:
      {
	size += serializator.get_packed_int_size (size);
	size += serializator.get_packed_int_size (size);
	size += serializator.get_packed_int_size (size);
      }
      break;

      case DB_TYPE_TIME:
      {
	size += serializator.get_packed_int_size (size);
	size += serializator.get_packed_int_size (size);
	size += serializator.get_packed_int_size (size);
      }
      break;

      case DB_TYPE_TIMESTAMP:
      {
	size += serializator.get_packed_int_size (size);
	size += serializator.get_packed_int_size (size);
	size += serializator.get_packed_int_size (size);
	size += serializator.get_packed_int_size (size);
	size += serializator.get_packed_int_size (size);
	size += serializator.get_packed_int_size (size);
      }
      break;

      case DB_TYPE_DATETIME:
      {
	size += serializator.get_packed_int_size (size);
	size += serializator.get_packed_int_size (size);
	size += serializator.get_packed_int_size (size);
	size += serializator.get_packed_int_size (size);
	size += serializator.get_packed_int_size (size);
	size += serializator.get_packed_int_size (size);
	size += serializator.get_packed_int_size (size);
      }
      break;

      case DB_TYPE_SET:
      case DB_TYPE_MULTISET:
      case DB_TYPE_SEQUENCE:
      {
	size += serializator.get_packed_int_size (size); // ncol

	DB_SET *set = db_get_set (val);
	int ncol = set_size (set);
	DB_VALUE v;
	for (int i = 0; i < ncol; i++)
	  {
	    set_get_element (set, i, &v);
	    size += get_value_packed_size (serializator, size, &v);
	  }
	pr_clear_value (&v);
      }
      break;

      case DB_TYPE_MONETARY:
	size += serializator.get_packed_double_size (size);
	break;

      case DB_TYPE_OBJECT:
	assert (false);
	break;

      case DB_TYPE_NULL:
	break;
      default:
	assert (false);
	break;
      }
  }

  void
  sp_arg::pack (cubpacking::packer &serializator) const
  {
    serializator.pack_int (arg_mode);
    serializator.pack_int (arg_type);

    pack_db_value (serializator, value);
  }

  void
  sp_arg::unpack (cubpacking::unpacker &deserializator)
  {
    // nothing
  }

  void
  sp_arg::pack_db_value (cubpacking::packer &serializator, DB_VALUE *val) const
  {
    int param_type = db_value_type (val);
    serializator.pack_int (param_type);
    switch (param_type)
      {
      case DB_TYPE_INTEGER:
	serializator.pack_int (sizeof (int));
	serializator.pack_int (db_get_int (val));
	break;

      case DB_TYPE_BIGINT:
	serializator.pack_int (sizeof (DB_BIGINT));
	serializator.pack_bigint (db_get_bigint (val));
	break;

      case DB_TYPE_SHORT:
	serializator.pack_int (sizeof (int));
	serializator.pack_short (db_get_short (val));
	break;

      case DB_TYPE_FLOAT:
	serializator.pack_int (sizeof (float));
	serializator.pack_float (db_get_float (val));
	break;

      case DB_TYPE_DOUBLE:
	serializator.pack_int (sizeof (double));
	serializator.pack_double (db_get_double (val));
	break;

      case DB_TYPE_NUMERIC:
      {
	char str_buf[NUMERIC_MAX_STRING_SIZE];
	numeric_db_value_print (val, str_buf);
	serializator.pack_int (strlen (str_buf));
	serializator.pack_c_string (str_buf, strlen (str_buf));
	break;

	case DB_TYPE_CHAR:
	case DB_TYPE_NCHAR:
	case DB_TYPE_VARNCHAR:
	case DB_TYPE_STRING:
	  // TODO: support unicode decomposed string
	  serializator.pack_string (db_get_string (val));
	  break;

	case DB_TYPE_BIT:
	case DB_TYPE_VARBIT:
	  // NOTE: This type was not implemented at the previous version
	  break;

	case DB_TYPE_DATE:
	{
	  int year, month, day;
	  db_date_decode (db_get_date (val), &month, &day, &year);
	  serializator.pack_int (sizeof (int) * 3);
	  serializator.pack_int (year);
	  serializator.pack_int (month - 1);
	  serializator.pack_int (day);
	}
	break;

	case DB_TYPE_TIME:
	{
	  int hour, min, sec;
	  db_time_decode (db_get_time (val), &hour, &min, &sec);
	  serializator.pack_int (sizeof (int) * 3);
	  serializator.pack_int (hour);
	  serializator.pack_int (min);
	  serializator.pack_int (sec);
	}
	break;

	case DB_TYPE_TIMESTAMP:
	{
	  int year, month, day, hour, min, sec;
	  DB_TIMESTAMP *timestamp = db_get_timestamp (val);
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
	  DB_DATETIME *datetime = db_get_datetime (val);
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
	  DB_SET *set = db_get_set (val);
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

	      pack_db_value (serializator, &v);
	      pr_clear_value (&v);
	    }
	}
	break;

	case DB_TYPE_MONETARY:
	{
	  DB_MONETARY *v = db_get_monetary (val);
	  serializator.pack_int (sizeof (double));
	  serializator.pack_double (v->amount);
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
}