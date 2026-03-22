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

#include "spatial_opfunc.h"

#include "db_geometry.hpp"
#include "dbtype.h"
#include "error_manager.h"

#include <cstring>

static bool
spatial_has_null_arg (DB_VALUE * const *args, int num_args)
{
  for (int i = 0; i < num_args; i++)
    {
      if (DB_IS_NULL (args[i]))
	{
	  return true;
	}
    }
  return false;
}

static int
spatial_make_varchar_result (DB_VALUE *result, const char *str)
{
  int length;

  if (str == NULL)
    {
      db_make_null (result);
      return NO_ERROR;
    }

  length = (int) strlen (str);
  return db_make_varchar (result, length, str, length, LANG_COERCIBLE_CODESET, LANG_COERCIBLE_COLL);
}

static int
spatial_get_int_arg (DB_VALUE *value, int *arg_int)
{
  switch (DB_VALUE_DOMAIN_TYPE (value))
    {
    case DB_TYPE_SHORT:
      *arg_int = db_get_short (value);
      return NO_ERROR;
    case DB_TYPE_INTEGER:
      *arg_int = db_get_int (value);
      return NO_ERROR;
    case DB_TYPE_BIGINT:
      *arg_int = (int) db_get_bigint (value);
      return NO_ERROR;
    default:
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_QPROC_INVALID_DATATYPE;
    }
}

int
db_evaluate_st_astext (DB_VALUE *result, DB_VALUE * const *args, int num_args)
{
  const char *text = NULL;
  int error;

  db_make_null (result);
  if (num_args != 1 || spatial_has_null_arg (args, num_args))
    {
      return NO_ERROR;
    }

  error = db_spatial_as_text (args[0], &text);
  if (error != NO_ERROR)
    {
      return error;
    }

  return spatial_make_varchar_result (result, text);
}

int
db_evaluate_st_geomfromtext (DB_VALUE *result, DB_VALUE * const *args, int num_args)
{
  DB_TYPE result_type = DB_TYPE_GEOMETRY;
  int srid = 0;
  int error;

  db_make_null (result);
  if (num_args < 1 || num_args > 2 || spatial_has_null_arg (args, num_args))
    {
      return NO_ERROR;
    }

  if (!DB_IS_STRING (args[0]))
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_QPROC_INVALID_DATATYPE;
    }

  if (num_args == 2)
    {
      error = spatial_get_int_arg (args[1], &srid);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }

  if (DB_VALUE_DOMAIN_TYPE (result) == DB_TYPE_GEOGRAPHY)
    {
      result_type = DB_TYPE_GEOGRAPHY;
    }

  return db_spatial_from_text (result, result_type, db_get_string (args[0]), db_get_string_size (args[0]), srid);
}

int
db_evaluate_st_srid (DB_VALUE *result, DB_VALUE * const *args, int num_args)
{
  int srid;
  int error;

  db_make_null (result);
  if (num_args != 1 || spatial_has_null_arg (args, num_args))
    {
      return NO_ERROR;
    }

  error = db_spatial_get_srid (args[0], &srid);
  if (error != NO_ERROR)
    {
      return error;
    }

  db_make_int (result, srid);
  return NO_ERROR;
}

int
db_evaluate_st_setsrid (DB_VALUE *result, DB_VALUE * const *args, int num_args)
{
  int srid;
  int error;

  db_make_null (result);
  if (num_args != 2 || spatial_has_null_arg (args, num_args))
    {
      return NO_ERROR;
    }

  error = spatial_get_int_arg (args[1], &srid);
  if (error != NO_ERROR)
    {
      return error;
    }

  return db_spatial_clone_with_srid (args[0], srid, result);
}

int
db_evaluate_st_geometrytype (DB_VALUE *result, DB_VALUE * const *args, int num_args)
{
  int subtype;
  int error;

  db_make_null (result);
  if (num_args != 1 || spatial_has_null_arg (args, num_args))
    {
      return NO_ERROR;
    }

  error = db_spatial_get_subtype (args[0], &subtype);
  if (error != NO_ERROR)
    {
      return error;
    }

  return spatial_make_varchar_result (result, db_spatial_subtype_to_name (subtype));
}

int
db_evaluate_st_x (DB_VALUE *result, DB_VALUE * const *args, int num_args)
{
  double coord;
  int error;

  db_make_null (result);
  if (num_args != 1 || spatial_has_null_arg (args, num_args))
    {
      return NO_ERROR;
    }

  error = db_spatial_get_point_x (args[0], &coord);
  if (error != NO_ERROR)
    {
      return error;
    }

  db_make_double (result, coord);
  return NO_ERROR;
}

int
db_evaluate_st_y (DB_VALUE *result, DB_VALUE * const *args, int num_args)
{
  double coord;
  int error;

  db_make_null (result);
  if (num_args != 1 || spatial_has_null_arg (args, num_args))
    {
      return NO_ERROR;
    }

  error = db_spatial_get_point_y (args[0], &coord);
  if (error != NO_ERROR)
    {
      return error;
    }

  db_make_double (result, coord);
  return NO_ERROR;
}

int
db_evaluate_st_area (DB_VALUE *result, DB_VALUE * const *args, int num_args)
{
  double area;
  int error;

  db_make_null (result);
  if (num_args != 1 || spatial_has_null_arg (args, num_args))
    {
      return NO_ERROR;
    }

  error = db_spatial_get_area (args[0], &area);
  if (error != NO_ERROR)
    {
      return error;
    }

  db_make_double (result, area);
  return NO_ERROR;
}

int
db_evaluate_st_length (DB_VALUE *result, DB_VALUE * const *args, int num_args)
{
  double length;
  int error;

  db_make_null (result);
  if (num_args != 1 || spatial_has_null_arg (args, num_args))
    {
      return NO_ERROR;
    }

  error = db_spatial_get_length (args[0], &length);
  if (error != NO_ERROR)
    {
      return error;
    }

  db_make_double (result, length);
  return NO_ERROR;
}

int
db_evaluate_st_distance (DB_VALUE *result, DB_VALUE * const *args, int num_args)
{
  double distance;
  int error;

  db_make_null (result);
  if (num_args != 2 || spatial_has_null_arg (args, num_args))
    {
      return NO_ERROR;
    }

  error = db_spatial_distance (args[0], args[1], &distance);
  if (error != NO_ERROR)
    {
      return error;
    }

  db_make_double (result, distance);
  return NO_ERROR;
}

int
db_evaluate_st_contains (DB_VALUE *result, DB_VALUE * const *args, int num_args)
{
  int contains;
  int error;

  db_make_null (result);
  if (num_args != 2 || spatial_has_null_arg (args, num_args))
    {
      return NO_ERROR;
    }

  error = db_spatial_contains (args[0], args[1], &contains);
  if (error != NO_ERROR)
    {
      return error;
    }

  db_make_int (result, contains);
  return NO_ERROR;
}

int
db_evaluate_st_intersects (DB_VALUE *result, DB_VALUE * const *args, int num_args)
{
  int intersects;
  int error;

  db_make_null (result);
  if (num_args != 2 || spatial_has_null_arg (args, num_args))
    {
      return NO_ERROR;
    }

  error = db_spatial_intersects (args[0], args[1], &intersects);
  if (error != NO_ERROR)
    {
      return error;
    }

  db_make_int (result, intersects);
  return NO_ERROR;
}

int
db_evaluate_st_envelope (DB_VALUE *result, DB_VALUE * const *args, int num_args)
{
  db_make_null (result);
  if (num_args != 1 || spatial_has_null_arg (args, num_args))
    {
      return NO_ERROR;
    }

  return db_spatial_envelope (args[0], result);
}
