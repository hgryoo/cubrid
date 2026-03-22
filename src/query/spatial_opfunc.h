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

#ifndef _SPATIAL_OPFUNC_H_
#define _SPATIAL_OPFUNC_H_

#include "dbtype_def.h"

extern int db_evaluate_st_astext (DB_VALUE * result, DB_VALUE * const *args, int num_args);
extern int db_evaluate_st_geomfromtext (DB_VALUE * result, DB_VALUE * const *args, int num_args);
extern int db_evaluate_st_srid (DB_VALUE * result, DB_VALUE * const *args, int num_args);
extern int db_evaluate_st_setsrid (DB_VALUE * result, DB_VALUE * const *args, int num_args);
extern int db_evaluate_st_geometrytype (DB_VALUE * result, DB_VALUE * const *args, int num_args);
extern int db_evaluate_st_x (DB_VALUE * result, DB_VALUE * const *args, int num_args);
extern int db_evaluate_st_y (DB_VALUE * result, DB_VALUE * const *args, int num_args);
extern int db_evaluate_st_distance (DB_VALUE * result, DB_VALUE * const *args, int num_args);
extern int db_evaluate_st_contains (DB_VALUE * result, DB_VALUE * const *args, int num_args);
extern int db_evaluate_st_intersects (DB_VALUE * result, DB_VALUE * const *args, int num_args);
extern int db_evaluate_st_area (DB_VALUE * result, DB_VALUE * const *args, int num_args);
extern int db_evaluate_st_length (DB_VALUE * result, DB_VALUE * const *args, int num_args);
extern int db_evaluate_st_envelope (DB_VALUE * result, DB_VALUE * const *args, int num_args);

#endif /* _SPATIAL_OPFUNC_H_ */
