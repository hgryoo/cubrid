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
 * db_spatial_geometry.hpp - structures and functions related to geometry
 */

#ifndef _DB_SPATIAL_GEOMETRY_HPP_
#define _DB_SPATIAL_GEOMETRY_HPP_

#include <algorithm>
#include <string>
#include <vector>
#include <memory>

#if defined (__cplusplus)

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

class CUB_COORDINATE
{
    private:
        static CUB_COORDINATE null_coordinate;

    public:
        double x;
        double y;
        double z;

        void set_null ();
        static CUB_COORDINATE& get_null ();
        bool is_null () const;
        
        bool equals_3d(const Coordinate& other) const;
        bool equals_2d(const Coordinate& other) const;
        bool equals(const Coordinate& other) const;
        int compare_to(const Coordinate& other) const;
        std::string to_string() const;
        double distance(const CUB_COORDINATE& p) const;
        double distanceSquared(const CUB_COORDINATE& p) const;
};

template <typename GeometryType, typename Factory>
class CUB_GEOMETRY
{
public:
    const Factory* getFactory() const { }
    int dimension ();
    int coordinate_dimension ();
    int spatial_dimension ();
    DB_GEOMETRY_TYPE geometry_type ();
    int SRID ();
    std::string as_text ();
    std::string as_binary ();
    bool is_empty ();
    bool is_simple ();
    bool is_3D ();
    bool is_measured ();

    CUB_GEOMETRY* envelope ();
    CUB_GEOMETRY* boundary ();

    /* query */
    bool equals (const CUB_GEOMETRY& geometry);
    bool disjoint (const CUB_GEOMETRY& geometry);
    bool intersects (const CUB_GEOMETRY& geometry);
    bool touches (const CUB_GEOMETRY& geometry);
    bool crosses (const CUB_GEOMETRY& geometry);
    bool within (const CUB_GEOMETRY& geometry);
    bool contains (const CUB_GEOMETRY& geometry);
    bool overlaps (const CUB_GEOMETRY& geometry);
    bool relate (const CUB_GEOMETRY& geometry);
    bool locateAlong (const CUB_GEOMETRY& geometry);
    bool locateBetween (const CUB_GEOMETRY& geometry);

    /* analysis */
    int distance (const CUB_GEOMETRY& geometry);
    CUB_GEOMETRY buffer (int distance);
    CUB_GEOMETRY convexHull ();
    CUB_GEOMETRY intersection (const CUB_GEOMETRY& geometry);
    CUB_GEOMETRY union_ (const CUB_GEOMETRY& geometry);
    CUB_GEOMETRY difference (const CUB_GEOMETRY& geometry);
    CUB_GEOMETRY sym_difference (const CUB_GEOMETRY& geometry);

private:
    void* m_user_data;
};

#endif

#endif
