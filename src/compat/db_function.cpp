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

#include "db_function.hpp"
// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

const char *
fcode_get_uppercase_name (FUNC_CODE ftype)
{
  switch (ftype)
    {
    case PT_MIN:
      return "MIN";
    case PT_MAX:
      return "MAX";
    case PT_SUM:
      return "SUM";
    case PT_AVG:
      return "AVG";
    case PT_STDDEV:
      return "STDDEV";
    case PT_STDDEV_POP:
      return "STDDEV_POP";
    case PT_STDDEV_SAMP:
      return "STDDEV_SAMP";
    case PT_VARIANCE:
      return "VARIANCE";
    case PT_VAR_POP:
      return "VAR_POP";
    case PT_VAR_SAMP:
      return "VAR_SAMP";
    case PT_COUNT:
      return "COUNT";
    case PT_COUNT_STAR:
      return "COUNT_STAR";
    case PT_CUME_DIST:
      return "CUME_DIST";
    case PT_PERCENT_RANK:
      return "PERCENT_RANK";
    case PT_LEAD:
      return "LEAD";
    case PT_LAG:
      return "LAG";
    case PT_GROUPBY_NUM:
      return "GROUPBY_NUM";
    case PT_AGG_BIT_AND:
      return "BIT_AND";
    case PT_AGG_BIT_OR:
      return "BIT_OR";
    case PT_AGG_BIT_XOR:
      return "BIT_XOR";
    case PT_TOP_AGG_FUNC:
      return "TOP_AGG_FUNC";
    case PT_GROUP_CONCAT:
      return "GROUP_CONCAT";
    case PT_GENERIC:
      return "GENERIC";
    case PT_ROW_NUMBER:
      return "ROW_NUMBER";
    case PT_RANK:
      return "RANK";
    case PT_DENSE_RANK:
      return "DENSE_RANK";
    case PT_NTILE:
      return "NTILE";
    case PT_FIRST_VALUE:
      return "FIRST_VALUE";
    case PT_LAST_VALUE:
      return "LAST_VALUE";
    case PT_NTH_VALUE:
      return "NTH_VALUE";
    case PT_MEDIAN:
      return "MEDIAN";
    case PT_PERCENTILE_CONT:
      return "PERCENTILE_CONT";
    case PT_PERCENTILE_DISC:
      return "PERCENTILE_DISC";
    case PT_JSON_ARRAYAGG:
      return "JSON_ARRAYAGG";
    case PT_JSON_OBJECTAGG:
      return "JSON_OBJECTAGG";

    case F_TABLE_SET:
      return "F_TABLE_SET";
    case F_TABLE_MULTISET:
      return "F_TABLE_MULTISET";
    case F_TABLE_SEQUENCE:
      return "F_TABLE_SEQUENCE";
    case F_TOP_TABLE_FUNC:
      return "F_TOP_TABLE_FUNC";
    case F_MIDXKEY:
      return "F_MIDXKEY";
    case F_SET:
      return "F_SET";
    case F_MULTISET:
      return "F_MULTISET";
    case F_SEQUENCE:
      return "F_SEQUENCE";
    case F_VID:
      return "F_VID";
    case F_GENERIC:
      return "F_GENERIC";
    case F_CLASS_OF:
      return "F_CLASS_OF";
    case F_INSERT_SUBSTRING:
      return "INSERT";
    case F_ELT:
      return "ELT";
    case F_BENCHMARK:
      return "BENCHMARK";
    case F_JSON_ARRAY:
      return "JSON_ARRAY";
    case F_JSON_ARRAY_APPEND:
      return "JSON_ARRAY_APPEND";
    case F_JSON_ARRAY_INSERT:
      return "JSON_ARRAY_INSERT";
    case F_JSON_CONTAINS:
      return "JSON_CONTAINS";
    case F_JSON_CONTAINS_PATH:
      return "JSON_CONTAINS_PATH";
    case F_JSON_DEPTH:
      return "JSON_DEPTH";
    case F_JSON_EXTRACT:
      return "JSON_EXTRACT";
    case F_JSON_GET_ALL_PATHS:
      return "JSON_GET_ALL_PATHS";
    case F_JSON_INSERT:
      return "JSON_INSERT";
    case F_JSON_KEYS:
      return "JSON_KEYS";
    case F_JSON_LENGTH:
      return "JSON_LENGTH";
    case F_JSON_MERGE:
      return "JSON_MERGE";
    case F_JSON_MERGE_PATCH:
      return "JSON_MERGE_PATCH";
    case F_JSON_OBJECT:
      return "JSON_OBJECT";
    case F_JSON_PRETTY:
      return "JSON_PRETTY";
    case F_JSON_QUOTE:
      return "JSON_QUOTE";
    case F_JSON_REMOVE:
      return "JSON_REMOVE";
    case F_JSON_REPLACE:
      return "JSON_REPLACE";
    case F_JSON_SEARCH:
      return "JSON_SEARCH";
    case F_JSON_SET:
      return "JSON_SET";
    case F_JSON_TYPE:
      return "JSON_TYPE";
    case F_JSON_UNQUOTE:
      return "JSON_UNQUOTE";
    case F_JSON_VALID:
      return "JSON_VALID";
    case F_REGEXP_COUNT:
      return "REGEXP_COUNT";
    case F_REGEXP_INSTR:
      return "REGEXP_INSTR";
    case F_REGEXP_LIKE:
      return "REGEXP_LIKE";
    case F_REGEXP_REPLACE:
      return "REGEXP_REPLACE";
    case F_REGEXP_SUBSTR:
      return "REGEXP_SUBSTR";
    case F_ST_AFFINE: return "ST_AFFINE";
    case F_ST_AREA: return "ST_AREA";
    case F_ST_AREA_SPHEROID: return "ST_AREA_SPHEROID";
    case F_ST_ASGEOJSON: return "ST_ASGEOJSON";
    case F_ST_ASHEXWKB: return "ST_ASHEXWKB";
    case F_ST_ASSVG: return "ST_ASSVG";
    case F_ST_ASTEXT: return "ST_ASTEXT";
    case F_ST_ASWKB: return "ST_ASWKB";
    case F_ST_ASBINARY: return "ST_ASBINARY";
    case F_ST_AZIMUTH: return "ST_AZIMUTH";
    case F_ST_BOUNDARY: return "ST_BOUNDARY";
    case F_ST_BUFFER: return "ST_BUFFER";
    case F_ST_BUILDAREA: return "ST_BUILDAREA";
    case F_ST_CENTROID: return "ST_CENTROID";
    case F_ST_COLLECT: return "ST_COLLECT";
    case F_ST_COLLECTIONEXTRACT: return "ST_COLLECTIONEXTRACT";
    case F_ST_CONCAVEHULL: return "ST_CONCAVEHULL";
    case F_ST_CONTAINS: return "ST_CONTAINS";
    case F_ST_CONTAINSPROPERLY: return "ST_CONTAINSPROPERLY";
    case F_ST_CONVEXHULL: return "ST_CONVEXHULL";
    case F_ST_COVERAGEINVALIDEDGES: return "ST_COVERAGEINVALIDEDGES";
    case F_ST_COVERAGESIMPLIFY: return "ST_COVERAGESIMPLIFY";
    case F_ST_COVERAGEUNION: return "ST_COVERAGEUNION";
    case F_ST_COVEREDBY: return "ST_COVEREDBY";
    case F_ST_COVERS: return "ST_COVERS";
    case F_ST_CROSSES: return "ST_CROSSES";
    case F_ST_DWITHIN: return "ST_DWITHIN";
    case F_ST_DWITHIN_GEOS: return "ST_DWITHIN_GEOS";
    case F_ST_DWITHIN_SPHEROID: return "ST_DWITHIN_SPHEROID";
    case F_ST_DIFFERENCE: return "ST_DIFFERENCE";
    case F_ST_DIMENSION: return "ST_DIMENSION";
    case F_ST_DISJOINT: return "ST_DISJOINT";
    case F_ST_DISTANCE: return "ST_DISTANCE";
    case F_ST_DISTANCE_GEOS: return "ST_DISTANCE_GEOS";
    case F_ST_DISTANCE_SPHERE: return "ST_DISTANCE_SPHERE";
    case F_ST_DISTANCE_SPHEROID: return "ST_DISTANCE_SPHEROID";
    case F_ST_DUMP: return "ST_DUMP";
    case F_ST_ENDPOINT: return "ST_ENDPOINT";
    case F_ST_ENVELOPE: return "ST_ENVELOPE";
    case F_ST_EQUALS: return "ST_EQUALS";
    case F_ST_EXTENT: return "ST_EXTENT";
    case F_ST_EXTENT_APPROX: return "ST_EXTENT_APPROX";
    case F_ST_EXTERIORRING: return "ST_EXTERIORRING";
    case F_ST_FLIPCOORDINATES: return "ST_FLIPCOORDINATES";
    case F_ST_FORCE2D: return "ST_FORCE2D";
    case F_ST_FORCE3DM: return "ST_FORCE3DM";
    case F_ST_FORCE3DZ: return "ST_FORCE3DZ";
    case F_ST_FORCE4D: return "ST_FORCE4D";
    case F_ST_GEOMFROMGEOJSON: return "ST_GEOMFROMGEOJSON";
    case F_ST_GEOMFROMHEXEWKB: return "ST_GEOMFROMHEXEWKB";
    case F_ST_GEOMFROMHEXWKB: return "ST_GEOMFROMHEXWKB";
    case F_ST_GEOMFROMTEXT: return "ST_GEOMFROMTEXT";
    case F_ST_GEOMFROMWKB: return "ST_GEOMFROMWKB";
    case F_ST_GEOMETRYTYPE: return "ST_GEOMETRYTYPE";
    case F_ST_HASM: return "ST_HASM";
    case F_ST_HASZ: return "ST_HASZ";
    case F_ST_HILBERT: return "ST_HILBERT";
    case F_ST_INTERSECTION: return "ST_INTERSECTION";
    case F_ST_INTERSECTS: return "ST_INTERSECTS";
    case F_ST_INTERSECTS_EXTENT: return "ST_INTERSECTS_EXTENT";
    case F_ST_ISCLOSED: return "ST_ISCLOSED";
    case F_ST_ISEMPTY: return "ST_ISEMPTY";
    case F_ST_ISRING: return "ST_ISRING";
    case F_ST_ISSIMPLE: return "ST_ISSIMPLE";
    case F_ST_ISVALID: return "ST_ISVALID";
    case F_ST_LENGTH: return "ST_LENGTH";
    case F_ST_LENGTH_SPHEROID: return "ST_LENGTH_SPHEROID";
    case F_ST_LINEINTERPOLATEPOINT: return "ST_LINEINTERPOLATEPOINT";
    case F_ST_LINEINTERPOLATEPOINTS: return "ST_LINEINTERPOLATEPOINTS";
    case F_ST_LINEMERGE: return "ST_LINEMERGE";
    case F_ST_LINESTRING2DFROMWKB: return "ST_LINESTRING2DFROMWKB";
    case F_ST_LINESUBSTRING: return "ST_LINESUBSTRING";
    case F_ST_M: return "ST_M";
    case F_ST_MMAX: return "ST_MMAX";
    case F_ST_MMIN: return "ST_MMIN";
    case F_ST_MAKEENVELOPE: return "ST_MAKEENVELOPE";
    case F_ST_MAKELINE: return "ST_MAKELINE";
    case F_ST_MAKEPOLYGON: return "ST_MAKEPOLYGON";
    case F_ST_MAKEVALID: return "ST_MAKEVALID";
    case F_ST_MAXIMUMINSCRIBEDCIRCLE: return "ST_MAXIMUMINSCRIBEDCIRCLE";
    case F_ST_MINIMUMROTATEDRECTANGLE: return "ST_MINIMUMROTATEDRECTANGLE";
    case F_ST_MULTI: return "ST_MULTI";
    case F_ST_NGEOMETRIES: return "ST_NGEOMETRIES";
    case F_ST_NINTERIORRINGS: return "ST_NINTERIORRINGS";
    case F_ST_NPOINTS: return "ST_NPOINTS";
    case F_ST_NODE: return "ST_NODE";
    case F_ST_NORMALIZE: return "ST_NORMALIZE";
    case F_ST_NUMGEOMETRIES: return "ST_NUMGEOMETRIES";
    case F_ST_NUMINTERIORRINGS: return "ST_NUMINTERIORRINGS";
    case F_ST_NUMPOINTS: return "ST_NUMPOINTS";
    case F_ST_OVERLAPS: return "ST_OVERLAPS";
    case F_ST_PERIMETER: return "ST_PERIMETER";
    case F_ST_PERIMETER_SPHEROID: return "ST_PERIMETER_SPHEROID";
    case F_ST_POINT: return "ST_POINT";
    case F_ST_POINT2D: return "ST_POINT2D";
    case F_ST_POINT2DFROMWKB: return "ST_POINT2DFROMWKB";
    case F_ST_POINT3D: return "ST_POINT3D";
    case F_ST_POINT4D: return "ST_POINT4D";
    case F_ST_POINTN: return "ST_POINTN";
    case F_ST_POINTONSURFACE: return "ST_POINTONSURFACE";
    case F_ST_POINTS: return "ST_POINTS";
    case F_ST_POLYGON2DFROMWKB: return "ST_POLYGON2DFROMWKB";
    case F_ST_POLYGONIZE: return "ST_POLYGONIZE";
    case F_ST_QUADKEY: return "ST_QUADKEY";
    case F_ST_REDUCEPRECISION: return "ST_REDUCEPRECISION";
    case F_ST_REMOVEREPEATEDPOINTS: return "ST_REMOVEREPEATEDPOINTS";
    case F_ST_REVERSE: return "ST_REVERSE";
    case F_ST_SETSRID: return "ST_SETSRID";
    case F_ST_SHORTESTLINE: return "ST_SHORTESTLINE";
    case F_ST_SIMPLIFY: return "ST_SIMPLIFY";
    case F_ST_SIMPLIFYPRESERVETOPOLOGY: return "ST_SIMPLIFYPRESERVETOPOLOGY";
    case F_ST_SRID: return "ST_SRID";
    case F_ST_STARTPOINT: return "ST_STARTPOINT";
    case F_ST_TILEENVELOPE: return "ST_TILEENVELOPE";
    case F_ST_TOUCHES: return "ST_TOUCHES";
    case F_ST_TRANSFORM: return "ST_TRANSFORM";
    case F_ST_UNION: return "ST_UNION";
    case F_ST_VORONOIDIAGRAM: return "ST_VORONOIDIAGRAM";
    case F_ST_WITHIN: return "ST_WITHIN";
    case F_ST_WITHINPROPERLY: return "ST_WITHINPROPERLY";
    case F_ST_X: return "ST_X";
    case F_ST_XMAX: return "ST_XMAX";
    case F_ST_XMIN: return "ST_XMIN";
    case F_ST_Y: return "ST_Y";
    case F_ST_YMAX: return "ST_YMAX";
    case F_ST_YMIN: return "ST_YMIN";
    case F_ST_Z: return "ST_Z";
    case F_ST_ZMFLAG: return "ST_ZMFLAG";
    case F_ST_ZMAX: return "ST_ZMAX";
    case F_ST_ZMIN: return "ST_ZMIN";
    default:
      return "***UNKNOWN***";
    }
}

const char *
fcode_get_lowercase_name (FUNC_CODE ftype)
{
  switch (ftype)
    {
    case PT_MIN:
      return "min";
    case PT_MAX:
      return "max";
    case PT_SUM:
      return "sum";
    case PT_AVG:
      return "avg";
    case PT_STDDEV:
      return "stddev";
    case PT_STDDEV_POP:
      return "stddev_pop";
    case PT_STDDEV_SAMP:
      return "stddev_samp";
    case PT_VARIANCE:
      return "variance";
    case PT_VAR_POP:
      return "var_pop";
    case PT_VAR_SAMP:
      return "var_samp";
    case PT_COUNT:
      return "count";
    case PT_COUNT_STAR:
      return "count";
    case PT_CUME_DIST:
      return "cume_dist";
    case PT_PERCENT_RANK:
      return "percent_rank";
    case PT_GROUPBY_NUM:
      return "groupby_num";
    case PT_AGG_BIT_AND:
      return "bit_and";
    case PT_AGG_BIT_OR:
      return "bit_or";
    case PT_AGG_BIT_XOR:
      return "bit_xor";
    case PT_GROUP_CONCAT:
      return "group_concat";
    case PT_ROW_NUMBER:
      return "row_number";
    case PT_RANK:
      return "rank";
    case PT_DENSE_RANK:
      return "dense_rank";
    case PT_LEAD:
      return "lead";
    case PT_LAG:
      return "lag";
    case PT_NTILE:
      return "ntile";
    case PT_FIRST_VALUE:
      return "first_value";
    case PT_LAST_VALUE:
      return "last_value";
    case PT_NTH_VALUE:
      return "nth_value";
    case PT_MEDIAN:
      return "median";
    case PT_PERCENTILE_CONT:
      return "percentile_cont";
    case PT_PERCENTILE_DISC:
      return "percentile_disc";
    case PT_JSON_ARRAYAGG:
      return "json_arrayagg";
    case PT_JSON_OBJECTAGG:
      return "json_objectagg";

    case F_SEQUENCE:
      return "sequence";
    case F_SET:
      return "set";
    case F_MULTISET:
      return "multiset";

    case F_TABLE_SEQUENCE:
      return "sequence";
    case F_TABLE_SET:
      return "set";
    case F_TABLE_MULTISET:
      return "multiset";
    case F_VID:
      return "vid";		/* internally generated only, vid doesn't parse */
    case F_CLASS_OF:
      return "class";
    case F_INSERT_SUBSTRING:
      return "insert";
    case F_ELT:
      return "elt";
    case F_ST_AFFINE: return "st_affine";
    case F_ST_AREA: return "st_area";
    case F_ST_AREA_SPHEROID: return "st_area_spheroid";
    case F_ST_ASGEOJSON: return "st_asgeojson";
    case F_ST_ASHEXWKB: return "st_ashexwkb";
    case F_ST_ASSVG: return "st_assvg";
    case F_ST_ASTEXT: return "st_astext";
    case F_ST_ASWKB: return "st_aswkb";
    case F_ST_ASBINARY: return "st_asbinary";
    case F_ST_AZIMUTH: return "st_azimuth";
    case F_ST_BOUNDARY: return "st_boundary";
    case F_ST_BUFFER: return "st_buffer";
    case F_ST_BUILDAREA: return "st_buildarea";
    case F_ST_CENTROID: return "st_centroid";
    case F_ST_COLLECT: return "st_collect";
    case F_ST_COLLECTIONEXTRACT: return "st_collectionextract";
    case F_ST_CONCAVEHULL: return "st_concavehull";
    case F_ST_CONTAINS: return "st_contains";
    case F_ST_CONTAINSPROPERLY: return "st_containsproperly";
    case F_ST_CONVEXHULL: return "st_convexhull";
    case F_ST_COVERAGEINVALIDEDGES: return "st_coverageinvalidedges";
    case F_ST_COVERAGESIMPLIFY: return "st_coveragesimplify";
    case F_ST_COVERAGEUNION: return "st_coverageunion";
    case F_ST_COVEREDBY: return "st_coveredby";
    case F_ST_COVERS: return "st_covers";
    case F_ST_CROSSES: return "st_crosses";
    case F_ST_DWITHIN: return "st_dwithin";
    case F_ST_DWITHIN_GEOS: return "st_dwithin_geos";
    case F_ST_DWITHIN_SPHEROID: return "st_dwithin_spheroid";
    case F_ST_DIFFERENCE: return "st_difference";
    case F_ST_DIMENSION: return "st_dimension";
    case F_ST_DISJOINT: return "st_disjoint";
    case F_ST_DISTANCE: return "st_distance";
    case F_ST_DISTANCE_GEOS: return "st_distance_geos";
    case F_ST_DISTANCE_SPHERE: return "st_distance_sphere";
    case F_ST_DISTANCE_SPHEROID: return "st_distance_spheroid";
    case F_ST_DUMP: return "st_dump";
    case F_ST_ENDPOINT: return "st_endpoint";
    case F_ST_ENVELOPE: return "st_envelope";
    case F_ST_EQUALS: return "st_equals";
    case F_ST_EXTENT: return "st_extent";
    case F_ST_EXTENT_APPROX: return "st_extent_approx";
    case F_ST_EXTERIORRING: return "st_exteriorring";
    case F_ST_FLIPCOORDINATES: return "st_flipcoordinates";
    case F_ST_FORCE2D: return "st_force2d";
    case F_ST_FORCE3DM: return "st_force3dm";
    case F_ST_FORCE3DZ: return "st_force3dz";
    case F_ST_FORCE4D: return "st_force4d";
    case F_ST_GEOMFROMGEOJSON: return "st_geomfromgeojson";
    case F_ST_GEOMFROMHEXEWKB: return "st_geomfromhexewkb";
    case F_ST_GEOMFROMHEXWKB: return "st_geomfromhexwkb";
    case F_ST_GEOMFROMTEXT: return "st_geomfromtext";
    case F_ST_GEOMFROMWKB: return "st_geomfromwkb";
    case F_ST_GEOMETRYTYPE: return "st_geometrytype";
    case F_ST_HASM: return "st_hasm";
    case F_ST_HASZ: return "st_hasz";
    case F_ST_HILBERT: return "st_hilbert";
    case F_ST_INTERSECTION: return "st_intersection";
    case F_ST_INTERSECTS: return "st_intersects";
    case F_ST_INTERSECTS_EXTENT: return "st_intersects_extent";
    case F_ST_ISCLOSED: return "st_isclosed";
    case F_ST_ISEMPTY: return "st_isempty";
    case F_ST_ISRING: return "st_isring";
    case F_ST_ISSIMPLE: return "st_issimple";
    case F_ST_ISVALID: return "st_isvalid";
    case F_ST_LENGTH: return "st_length";
    case F_ST_LENGTH_SPHEROID: return "st_length_spheroid";
    case F_ST_LINEINTERPOLATEPOINT: return "st_lineinterpolatepoint";
    case F_ST_LINEINTERPOLATEPOINTS: return "st_lineinterpolatepoints";
    case F_ST_LINEMERGE: return "st_linemerge";
    case F_ST_LINESTRING2DFROMWKB: return "st_linestring2dfromwkb";
    case F_ST_LINESUBSTRING: return "st_linesubstring";
    case F_ST_M: return "st_m";
    case F_ST_MMAX: return "st_mmax";
    case F_ST_MMIN: return "st_mmin";
    case F_ST_MAKEENVELOPE: return "st_makeenvelope";
    case F_ST_MAKELINE: return "st_makeline";
    case F_ST_MAKEPOLYGON: return "st_makepolygon";
    case F_ST_MAKEVALID: return "st_makevalid";
    case F_ST_MAXIMUMINSCRIBEDCIRCLE: return "st_maximuminscribedcircle";
    case F_ST_MINIMUMROTATEDRECTANGLE: return "st_minimumrotatedrectangle";
    case F_ST_MULTI: return "st_multi";
    case F_ST_NGEOMETRIES: return "st_ngeometries";
    case F_ST_NINTERIORRINGS: return "st_ninteriorrings";
    case F_ST_NPOINTS: return "st_npoints";
    case F_ST_NODE: return "st_node";
    case F_ST_NORMALIZE: return "st_normalize";
    case F_ST_NUMGEOMETRIES: return "st_numgeometries";
    case F_ST_NUMINTERIORRINGS: return "st_numinteriorrings";
    case F_ST_NUMPOINTS: return "st_numpoints";
    case F_ST_OVERLAPS: return "st_overlaps";
    case F_ST_PERIMETER: return "st_perimeter";
    case F_ST_PERIMETER_SPHEROID: return "st_perimeter_spheroid";
    case F_ST_POINT: return "st_point";
    case F_ST_POINT2D: return "st_point2d";
    case F_ST_POINT2DFROMWKB: return "st_point2dfromwkb";
    case F_ST_POINT3D: return "st_point3d";
    case F_ST_POINT4D: return "st_point4d";
    case F_ST_POINTN: return "st_pointn";
    case F_ST_POINTONSURFACE: return "st_pointonsurface";
    case F_ST_POINTS: return "st_points";
    case F_ST_POLYGON2DFROMWKB: return "st_polygon2dfromwkb";
    case F_ST_POLYGONIZE: return "st_polygonize";
    case F_ST_QUADKEY: return "st_quadkey";
    case F_ST_REDUCEPRECISION: return "st_reduceprecision";
    case F_ST_REMOVEREPEATEDPOINTS: return "st_removerepeatedpoints";
    case F_ST_REVERSE: return "st_reverse";
    case F_ST_SETSRID: return "st_setsrid";
    case F_ST_SHORTESTLINE: return "st_shortestline";
    case F_ST_SIMPLIFY: return "st_simplify";
    case F_ST_SIMPLIFYPRESERVETOPOLOGY: return "st_simplifypreservetopology";
    case F_ST_SRID: return "st_srid";
    case F_ST_STARTPOINT: return "st_startpoint";
    case F_ST_TILEENVELOPE: return "st_tileenvelope";
    case F_ST_TOUCHES: return "st_touches";
    case F_ST_TRANSFORM: return "st_transform";
    case F_ST_UNION: return "st_union";
    case F_ST_VORONOIDIAGRAM: return "st_voronoidiagram";
    case F_ST_WITHIN: return "st_within";
    case F_ST_WITHINPROPERLY: return "st_withinproperly";
    case F_ST_X: return "st_x";
    case F_ST_XMAX: return "st_xmax";
    case F_ST_XMIN: return "st_xmin";
    case F_ST_Y: return "st_y";
    case F_ST_YMAX: return "st_ymax";
    case F_ST_YMIN: return "st_ymin";
    case F_ST_Z: return "st_z";
    case F_ST_ZMFLAG: return "st_zmflag";
    case F_ST_ZMAX: return "st_zmax";
    case F_ST_ZMIN: return "st_zmin";
    case F_BENCHMARK:
      return "benchmark";
    case F_JSON_ARRAY:
      return "json_array";
    case F_JSON_ARRAY_APPEND:
      return "json_array_append";
    case F_JSON_ARRAY_INSERT:
      return "json_array_insert";
    case F_JSON_CONTAINS:
      return "json_contains";
    case F_JSON_CONTAINS_PATH:
      return "json_contains_path";
    case F_JSON_DEPTH:
      return "json_depth";
    case F_JSON_EXTRACT:
      return "json_extract";
    case F_JSON_GET_ALL_PATHS:
      return "json_get_all_paths";
    case F_JSON_INSERT:
      return "json_insert";
    case F_JSON_KEYS:
      return "json_keys";
    case F_JSON_LENGTH:
      return "json_length";
    case F_JSON_MERGE:
      return "json_merge";
    case F_JSON_MERGE_PATCH:
      return "json_merge_patch";
    case F_JSON_OBJECT:
      return "json_object";
    case F_JSON_PRETTY:
      return "json_pretty";
    case F_JSON_QUOTE:
      return "json_quote";
    case F_JSON_REMOVE:
      return "json_remove";
    case F_JSON_REPLACE:
      return "json_replace";
    case F_JSON_SEARCH:
      return "json_search";
    case F_JSON_SET:
      return "json_set";
    case F_JSON_TYPE:
      return "json_type";
    case F_JSON_UNQUOTE:
      return "json_unquote";
    case F_JSON_VALID:
      return "json_valid";
    case F_REGEXP_COUNT:
      return "regexp_count";
    case F_REGEXP_INSTR:
      return "regexp_instr";
    case F_REGEXP_LIKE:
      return "regexp_like";
    case F_REGEXP_REPLACE:
      return "regexp_replace";
    case F_REGEXP_SUBSTR:
      return "regexp_substr";
    default:
      return "unknown function";
    }
}
