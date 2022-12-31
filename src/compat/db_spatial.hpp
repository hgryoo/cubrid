/*
 * Copyright (C) 2016 CUBRID Corporation
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

/*
 * db_spatial.hpp - functions related to geometry and spatial functions
 */

#ifndef _DB_SPATIAL_HPP_
#define _DB_SPATIAL_HPP_

#include "error_manager.h"
#include "memory_reference_store.hpp"
#include "storage_common.h"

// forward definitions
struct or_buf;

/*
 * these also double as type precedence
 * INT and DOUBLE actually have the same precedence
 */


#if defined (__cplusplus)

#include <string>
#include <cstdint>
#include <string>
#include <vector>

#include "db_geos.hpp"

using CUB_GEOMETRY = geos::geom::Geometry
using CUB_GEOMETRY_FACTORY = geos::geom::GeometryFactory;
using CUB_GEOMETRY_TYPE_ID = geos::geom::GeometryTypeId;
using GEOS_WKTWRITER = geos::io::WKTWriter;
using GEOS_WKTREADER = geos::io::WKTReader;
using GEOS_WKBWRITER = geos::io::WKBWriter;
using GEOS_WKBREADER = geos::io::WKBReader;

#else
typedef void CUB_GEOMETRY_FACTORY;
typedef int  CUB_GEOMETRY_TYPE_ID;
typedef void GEOS_WKTWRITER;
typedef void GEOS_WKTREADER;
typedef void GEOS_WKBWRITER;
typedef void GEOS_WKBREADER;
#endif

#if defined (__cplusplus)

extern "C"
{
int db_spatial_serialize (const CUB_GEOMETRY &geom, or_buf &buffer);
std::size_t db_spatial_serialize_length (const CUB_GEOMETRY &geom);
int db_spatial_deserialize (or_buf *buf, CUB_GEOMETRY *&geom);

DB_GEOMETRY_TYPE db_geometry_get_type (CUB_GEOMETRY *&geom);
}

namespace cubspatial
{

}

#endif /* defined (__cplusplus) */

#endif /* _DB_SPATIAL_HPP_ */
