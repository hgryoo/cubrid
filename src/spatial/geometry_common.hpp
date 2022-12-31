/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
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

//
// geometry_common
//

#ifndef _GEOMETRY_COMMON_HPP_
#define _GEOMETRY_COMMON_HPP_

#include <string>

namespace cubspatial {

enum class DB_GEOMETRY_TYPE
{
  DB_GEOMETRY_NULL = 0,
  DB_GEOMETRY_UNKNOWN,
  DB_GEOMETRY_POINT,
  DB_GEOMETRY_LINESTRING,
  DB_GEOMETRY_POLYGON,
  DB_GEOMETRY_MULTIPOINT,
  DB_GEOMETRY_MULTILINESTRING,
  DB_GEOMETRY_MULTIPOLYGON,
  DB_GEOMETRY_GEOMETRYCOLLECTION,
  DB_GEOMETRY_CIRCULARSTRING,
  DB_GEOMETRY_COMPOUNDCURVE,
  DB_GEOMETRY_CURVEPOLYGON,
  DB_GEOMETRY_MULTICURVE,
  DB_GEOMETRY_MULTISURFACE,
  DB_GEOMETRY_CURVE,
  DB_GEOMETRY_SURFACE,
  DB_GEOMETRY_POLYHEDRALSURFACE,
  DB_GEOMETRY_TIN,
  DB_GEOMETRY_TRIANGLE,
  DB_GEOMETRY_SOLID,
  DB_GEOMETRY_MULTISOLID,
};

template <typename GeometryInstance, typename Factory>
class Geometry {
public:
    virtual ~Geometry() {}

    virtual Geometry& clear() = 0;
    virtual bool is_empty() const = 0;

    DB_GEOMETRY_TYPE geometry_type () const { return geometry_type_; }

    int dimension () = 0;
    int coordinate_dimension () = 0;
    int spatial_dimension () = 0;
    int SRID ();

    /* query */
    bool equals (const Geometry& geometry);
    bool disjoint (const Geometry& geometry);
    bool intersects (const Geometry& geometry);
    bool touches (const Geometry& geometry);
    bool crosses (const Geometry& geometry);
    bool within (const Geometry& geometry);
    bool contains (const Geometry& geometry);
    bool overlaps (const Geometry& geometry);
    bool relate (const Geometry& geometry);
    bool locateAlong (const Geometry& geometry);
    bool locateBetween (const Geometry& geometry);

    /* analysis */
    int distance (const Geometry& geometry);
    Geometry buffer (int distance);
    Geometry convexHull ();
    Geometry intersection (const Geometry& geometry);
    Geometry union_ (const Geometry& geometry);
    Geometry difference (const Geometry& geometry);
    Geometry sym_difference (const Geometry& geometry);

protected:
    Geometry(GeometryType gtype, int dim): type(gtype), dimension(dim) {}

private:
    int type;
    int dimension;
    void* m_user_data;
    GeometryInstance* instance;
};

}

#endif // _GEOMETRY_HPP_