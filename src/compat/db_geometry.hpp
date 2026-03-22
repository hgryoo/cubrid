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
 * db_geometry.hpp - functions related to geometry/geography values
 */

#ifndef _DB_GEOMETRY_HPP_
#define _DB_GEOMETRY_HPP_

#include "dbtype_def.h"

#ifdef __cplusplus
extern "C"
{
#endif

void db_spatial_free (DB_SPATIAL *spatial);
int db_spatial_build_memory (DB_TYPE type, DB_SPATIAL *spatial, int declared_subtype, int srid);

const char *db_spatial_subtype_to_name (int subtype);
int db_spatial_get_subtype (const DB_VALUE *value, int *subtype_out);
int db_spatial_get_srid (const DB_VALUE *value, int *srid_out);
int db_spatial_from_text (DB_VALUE *result, DB_TYPE type, const char *text, int length, int srid);
int db_spatial_clone_with_srid (const DB_VALUE *src, int srid, DB_VALUE *result);
int db_spatial_as_text (const DB_VALUE *value, const char **text_out);
int db_spatial_get_point_x (const DB_VALUE *value, double *x_out);
int db_spatial_get_point_y (const DB_VALUE *value, double *y_out);
int db_spatial_get_area (const DB_VALUE *value, double *area_out);
int db_spatial_get_length (const DB_VALUE *value, double *length_out);
int db_spatial_distance (const DB_VALUE *value1, const DB_VALUE *value2, double *distance_out);
int db_spatial_contains (const DB_VALUE *value1, const DB_VALUE *value2, int *result_out);
int db_spatial_intersects (const DB_VALUE *value1, const DB_VALUE *value2, int *result_out);
int db_spatial_envelope (const DB_VALUE *value, DB_VALUE *result);

#ifdef __cplusplus
}
#endif

#endif /* _DB_GEOMETRY_HPP_ */
