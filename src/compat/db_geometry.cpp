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
 * db_geometry.cpp - GEOS-backed geometry/geography support
 */

#include "db_geometry.hpp"

#include "dbtype.h"
#include "error_manager.h"
#include "memory_private_allocator.hpp"

#include <geos_c.h>
#include <cassert>
#include <cstring>

static GEOSContextHandle_t
db_spatial_create_context (void)
{
  return GEOS_init_r ();
}

static int
db_spatial_geos_type_to_subtype (int geos_type)
{
  switch (geos_type)
    {
    case GEOS_POINT:
      return DB_SPATIAL_SUBTYPE_POINT;
    case GEOS_LINESTRING:
    case GEOS_LINEARRING:
      return DB_SPATIAL_SUBTYPE_LINESTRING;
    case GEOS_POLYGON:
      return DB_SPATIAL_SUBTYPE_POLYGON;
    case GEOS_MULTIPOINT:
      return DB_SPATIAL_SUBTYPE_MULTIPOINT;
    case GEOS_MULTILINESTRING:
      return DB_SPATIAL_SUBTYPE_MULTILINESTRING;
    case GEOS_MULTIPOLYGON:
      return DB_SPATIAL_SUBTYPE_MULTIPOLYGON;
    case GEOS_GEOMETRYCOLLECTION:
      return DB_SPATIAL_SUBTYPE_GEOMETRYCOLLECTION;
    default:
      return DB_SPATIAL_SUBTYPE_ANY;
    }
}

static int
db_spatial_copy_serialized (char **copy, const char *serialized, int length)
{
  char *new_copy = NULL;

  assert (copy != NULL);
  *copy = NULL;

  if (serialized == NULL)
    {
      return NO_ERROR;
    }

  new_copy = (char *) db_private_alloc (NULL, length + 1);
  if (new_copy == NULL)
    {
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  memcpy (new_copy, serialized, length);
  new_copy[length] = '\0';
  *copy = new_copy;
  return NO_ERROR;
}

void
db_spatial_free (DB_SPATIAL *spatial)
{
  if (spatial == NULL)
    {
      return;
    }

  if (spatial->geometry != NULL && spatial->context != NULL)
    {
      GEOSGeom_destroy_r ((GEOSContextHandle_t) spatial->context, (GEOSGeometry *) spatial->geometry);
    }

  if (spatial->context != NULL)
    {
      GEOS_finish_r ((GEOSContextHandle_t) spatial->context);
    }

  spatial->geometry = NULL;
  spatial->context = NULL;
  spatial->subtype = DB_SPATIAL_SUBTYPE_ANY;
  spatial->srid = 0;
}

static int
db_spatial_build_from_serialized (DB_TYPE type, DB_SPATIAL *spatial, int declared_subtype, int srid)
{
  GEOSContextHandle_t context = NULL;
  GEOSWKTReader *reader = NULL;
  GEOSGeometry *geometry = NULL;
  int actual_subtype = DB_SPATIAL_SUBTYPE_ANY;
  int geos_type = -1;

  assert (type == DB_TYPE_GEOMETRY || type == DB_TYPE_GEOGRAPHY);

  if (spatial == NULL || spatial->serialized == NULL)
    {
      return NO_ERROR;
    }

  db_spatial_free (spatial);

  context = db_spatial_create_context ();
  if (context == NULL)
    {
      return ER_FAILED;
    }

  reader = GEOSWKTReader_create_r (context);
  if (reader == NULL)
    {
      GEOS_finish_r (context);
      return ER_FAILED;
    }

  geometry = GEOSWKTReader_read_r (context, reader, spatial->serialized);
  GEOSWKTReader_destroy_r (context, reader);

  if (geometry == NULL)
    {
      GEOS_finish_r (context);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  geos_type = GEOSGeomTypeId_r (context, geometry);
  actual_subtype = db_spatial_geos_type_to_subtype (geos_type);
  if (declared_subtype != DB_SPATIAL_SUBTYPE_ANY && actual_subtype != declared_subtype)
    {
      GEOSGeom_destroy_r (context, geometry);
      GEOS_finish_r (context);
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  if (srid != 0)
    {
      GEOSSetSRID_r (context, geometry, srid);
    }
  else
    {
      srid = GEOSGetSRID_r (context, geometry);
    }

  spatial->context = context;
  spatial->geometry = geometry;
  spatial->subtype = actual_subtype;
  spatial->srid = srid;
  return NO_ERROR;
}

int
db_spatial_build_memory (DB_TYPE type, DB_SPATIAL *spatial, int declared_subtype, int srid)
{
  return db_spatial_build_from_serialized (type, spatial, declared_subtype, srid);
}

static int
db_spatial_validate_value (const DB_VALUE *value, const DB_SPATIAL **spatial_out)
{
  DB_TYPE type;
  int error;

  if (value == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type != DB_TYPE_GEOMETRY && type != DB_TYPE_GEOGRAPHY)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_INVALID_DATATYPE, 0);
      return ER_QPROC_INVALID_DATATYPE;
    }

  error = db_spatial_build_internal ((DB_VALUE *) value);
  if (error != NO_ERROR)
    {
      return error;
    }

  *spatial_out = db_get_spatial (value);
  return NO_ERROR;
}

static int
db_spatial_make_temp_geometry (const DB_SPATIAL *spatial, GEOSContextHandle_t context, GEOSGeometry **geometry_out)
{
  GEOSWKTReader *reader = NULL;
  GEOSGeometry *geometry = NULL;

  reader = GEOSWKTReader_create_r (context);
  if (reader == NULL)
    {
      return ER_FAILED;
    }

  geometry = GEOSWKTReader_read_r (context, reader, spatial->serialized);
  GEOSWKTReader_destroy_r (context, reader);
  if (geometry == NULL)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  if (spatial->srid != 0)
    {
      GEOSSetSRID_r (context, geometry, spatial->srid);
    }

  *geometry_out = geometry;
  return NO_ERROR;
}

static int
db_spatial_make_from_geometry (DB_VALUE *result, DB_TYPE type, GEOSContextHandle_t context, GEOSGeometry *geometry, int srid)
{
  GEOSWKTWriter *writer = NULL;
  char *wkt = NULL;
  char *serialized = NULL;
  int subtype;
  int length;
  int error;

  writer = GEOSWKTWriter_create_r (context);
  if (writer == NULL)
    {
      GEOSGeom_destroy_r (context, geometry);
      GEOS_finish_r (context);
      return ER_FAILED;
    }

  GEOSWKTWriter_setTrim_r (context, writer, 1);
  wkt = GEOSWKTWriter_write_r (context, writer, geometry);
  GEOSWKTWriter_destroy_r (context, writer);

  if (wkt == NULL)
    {
      GEOSGeom_destroy_r (context, geometry);
      GEOS_finish_r (context);
      return ER_FAILED;
    }

  length = (int) strlen (wkt);
  serialized = (char *) db_private_alloc (NULL, length + 1);
  if (serialized == NULL)
    {
      GEOSFree_r (context, wkt);
      GEOSGeom_destroy_r (context, geometry);
      GEOS_finish_r (context);
      return ER_OUT_OF_VIRTUAL_MEMORY;
    }

  memcpy (serialized, wkt, length + 1);
  GEOSFree_r (context, wkt);

  subtype = db_spatial_geos_type_to_subtype (GEOSGeomTypeId_r (context, geometry));
  error = db_value_domain_init (result, type, subtype, 0);
  if (error != NO_ERROR)
    {
      db_private_free_and_init (NULL, serialized);
      GEOSGeom_destroy_r (context, geometry);
      GEOS_finish_r (context);
      return error;
    }

  result->domain.general_info.is_null = 0;
  result->data.spatial.serialized = serialized;
  result->data.spatial.length = length;
  result->data.spatial.geometry = geometry;
  result->data.spatial.context = context;
  result->data.spatial.subtype = subtype;
  result->data.spatial.srid = srid;
  result->need_clear = true;
  return NO_ERROR;
}

static int
db_spatial_eval_binary_predicate_or_metric (const DB_VALUE *value1, const DB_VALUE *value2, bool is_metric, int *predicate_out,
					    double *metric_out,
					    char (*predicate_func) (GEOSContextHandle_t, const GEOSGeometry *,
								    const GEOSGeometry *),
					    int (*metric_func) (GEOSContextHandle_t, const GEOSGeometry *,
								const GEOSGeometry *, double *))
{
  const DB_SPATIAL *spatial1 = NULL;
  const DB_SPATIAL *spatial2 = NULL;
  GEOSContextHandle_t context = NULL;
  GEOSGeometry *geom1 = NULL;
  GEOSGeometry *geom2 = NULL;
  int error;
  char predicate;

  error = db_spatial_validate_value (value1, &spatial1);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = db_spatial_validate_value (value2, &spatial2);
  if (error != NO_ERROR)
    {
      return error;
    }

  context = GEOS_init_r ();
  if (context == NULL)
    {
      return ER_FAILED;
    }

  error = db_spatial_make_temp_geometry (spatial1, context, &geom1);
  if (error != NO_ERROR)
    {
      GEOS_finish_r (context);
      return error;
    }

  error = db_spatial_make_temp_geometry (spatial2, context, &geom2);
  if (error != NO_ERROR)
    {
      GEOSGeom_destroy_r (context, geom1);
      GEOS_finish_r (context);
      return error;
    }

  if (is_metric)
    {
      error = metric_func (context, geom1, geom2, metric_out) == 0 ? ER_FAILED : NO_ERROR;
    }
  else
    {
      predicate = predicate_func (context, geom1, geom2);
      if (predicate == 2)
	{
	  error = ER_FAILED;
	}
      else
	{
	  *predicate_out = predicate ? 1 : 0;
	  error = NO_ERROR;
	}
    }

  GEOSGeom_destroy_r (context, geom1);
  GEOSGeom_destroy_r (context, geom2);
  GEOS_finish_r (context);
  return error;
}

int
db_spatial_build_internal (DB_VALUE *value)
{
  DB_SPATIAL *spatial = NULL;
  DB_TYPE type;

  if (value == NULL || DB_IS_NULL (value))
    {
      return NO_ERROR;
    }

  type = DB_VALUE_DOMAIN_TYPE (value);
  if (type != DB_TYPE_GEOMETRY && type != DB_TYPE_GEOGRAPHY)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  spatial = &value->data.spatial;
  return db_spatial_build_from_serialized (type, spatial, spatial->subtype, spatial->srid);
}

const char *
db_spatial_subtype_to_name (int subtype)
{
  switch (subtype)
    {
    case DB_SPATIAL_SUBTYPE_POINT:
      return "POINT";
    case DB_SPATIAL_SUBTYPE_LINESTRING:
      return "LINESTRING";
    case DB_SPATIAL_SUBTYPE_POLYGON:
      return "POLYGON";
    case DB_SPATIAL_SUBTYPE_MULTIPOINT:
      return "MULTIPOINT";
    case DB_SPATIAL_SUBTYPE_MULTILINESTRING:
      return "MULTILINESTRING";
    case DB_SPATIAL_SUBTYPE_MULTIPOLYGON:
      return "MULTIPOLYGON";
    case DB_SPATIAL_SUBTYPE_GEOMETRYCOLLECTION:
      return "GEOMETRYCOLLECTION";
    default:
      return "GEOMETRY";
    }
}

int
db_spatial_get_subtype (const DB_VALUE *value, int *subtype_out)
{
  const DB_SPATIAL *spatial = NULL;
  int error = db_spatial_validate_value (value, &spatial);
  if (error != NO_ERROR)
    {
      return error;
    }

  *subtype_out = spatial->subtype;
  return NO_ERROR;
}

int
db_spatial_get_srid (const DB_VALUE *value, int *srid_out)
{
  const DB_SPATIAL *spatial = NULL;
  int error = db_spatial_validate_value (value, &spatial);
  if (error != NO_ERROR)
    {
      return error;
    }

  *srid_out = spatial->srid;
  return NO_ERROR;
}

int
db_spatial_from_text (DB_VALUE *result, DB_TYPE type, const char *text, int length, int srid)
{
  char *serialized = NULL;
  int error = db_spatial_copy_serialized (&serialized, text, length);
  if (error != NO_ERROR)
    {
      return error;
    }

  return db_make_spatial_ex (result, type, serialized, length, DB_SPATIAL_SUBTYPE_ANY, srid, true);
}

int
db_spatial_clone_with_srid (const DB_VALUE *src, int srid, DB_VALUE *result)
{
  const DB_SPATIAL *spatial = NULL;
  char *serialized = NULL;
  int error = db_spatial_validate_value (src, &spatial);
  if (error != NO_ERROR)
    {
      return error;
    }

  error = db_spatial_copy_serialized (&serialized, spatial->serialized, spatial->length);
  if (error != NO_ERROR)
    {
      return error;
    }

  return db_make_spatial_ex (result, DB_VALUE_DOMAIN_TYPE (src), serialized, spatial->length, spatial->subtype, srid, true);
}

int
db_spatial_as_text (const DB_VALUE *value, const char **text_out)
{
  const DB_SPATIAL *spatial = NULL;
  int error = db_spatial_validate_value (value, &spatial);
  if (error != NO_ERROR)
    {
      return error;
    }

  *text_out = spatial->serialized;
  return NO_ERROR;
}

int
db_spatial_get_point_x (const DB_VALUE *value, double *x_out)
{
  const DB_SPATIAL *spatial = NULL;
  const GEOSCoordSequence *coord_seq = NULL;
  int error = db_spatial_validate_value (value, &spatial);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (spatial->subtype != DB_SPATIAL_SUBTYPE_POINT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  coord_seq =
    GEOSGeom_getCoordSeq_r ((GEOSContextHandle_t) spatial->context, (const GEOSGeometry *) spatial->geometry);
  if (coord_seq == NULL || GEOSCoordSeq_getX_r ((GEOSContextHandle_t) spatial->context, coord_seq, 0, x_out) == 0)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

int
db_spatial_get_point_y (const DB_VALUE *value, double *y_out)
{
  const DB_SPATIAL *spatial = NULL;
  const GEOSCoordSequence *coord_seq = NULL;
  int error = db_spatial_validate_value (value, &spatial);
  if (error != NO_ERROR)
    {
      return error;
    }

  if (spatial->subtype != DB_SPATIAL_SUBTYPE_POINT)
    {
      er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_OBJ_INVALID_ARGUMENTS, 0);
      return ER_OBJ_INVALID_ARGUMENTS;
    }

  coord_seq =
    GEOSGeom_getCoordSeq_r ((GEOSContextHandle_t) spatial->context, (const GEOSGeometry *) spatial->geometry);
  if (coord_seq == NULL || GEOSCoordSeq_getY_r ((GEOSContextHandle_t) spatial->context, coord_seq, 0, y_out) == 0)
    {
      return ER_FAILED;
    }

  return NO_ERROR;
}

int
db_spatial_get_area (const DB_VALUE *value, double *area_out)
{
  const DB_SPATIAL *spatial = NULL;
  int error = db_spatial_validate_value (value, &spatial);
  if (error != NO_ERROR)
    {
      return error;
    }

  return GEOSArea_r ((GEOSContextHandle_t) spatial->context, (const GEOSGeometry *) spatial->geometry,
		     area_out) == 0 ? ER_FAILED : NO_ERROR;
}

int
db_spatial_get_length (const DB_VALUE *value, double *length_out)
{
  const DB_SPATIAL *spatial = NULL;
  int error = db_spatial_validate_value (value, &spatial);
  if (error != NO_ERROR)
    {
      return error;
    }

  return GEOSLength_r ((GEOSContextHandle_t) spatial->context, (const GEOSGeometry *) spatial->geometry,
		       length_out) == 0 ? ER_FAILED : NO_ERROR;
}

int
db_spatial_distance (const DB_VALUE *value1, const DB_VALUE *value2, double *distance_out)
{
  return db_spatial_eval_binary_predicate_or_metric (value1, value2, true, NULL, distance_out, NULL, GEOSDistance_r);
}

int
db_spatial_contains (const DB_VALUE *value1, const DB_VALUE *value2, int *result_out)
{
  return db_spatial_eval_binary_predicate_or_metric (value1, value2, false, result_out, NULL, GEOSContains_r, NULL);
}

int
db_spatial_intersects (const DB_VALUE *value1, const DB_VALUE *value2, int *result_out)
{
  return db_spatial_eval_binary_predicate_or_metric (value1, value2, false, result_out, NULL, GEOSIntersects_r, NULL);
}

int
db_spatial_envelope (const DB_VALUE *value, DB_VALUE *result)
{
  const DB_SPATIAL *spatial = NULL;
  GEOSContextHandle_t context = NULL;
  GEOSGeometry *geom = NULL;
  GEOSGeometry *envelope = NULL;
  int error = db_spatial_validate_value (value, &spatial);
  if (error != NO_ERROR)
    {
      return error;
    }

  context = GEOS_init_r ();
  if (context == NULL)
    {
      return ER_FAILED;
    }

  error = db_spatial_make_temp_geometry (spatial, context, &geom);
  if (error != NO_ERROR)
    {
      GEOS_finish_r (context);
      return error;
    }

  envelope = GEOSEnvelope_r (context, geom);
  GEOSGeom_destroy_r (context, geom);
  if (envelope == NULL)
    {
      GEOS_finish_r (context);
      return ER_FAILED;
    }

  return db_spatial_make_from_geometry (result, DB_VALUE_DOMAIN_TYPE (value), context, envelope, spatial->srid);
}
