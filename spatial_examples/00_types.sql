-- basic type parsing and subtype preservation

select cast('POINT (127.0 37.5)' as geometry(point)) as geom_point
from db_root;

select cast('LINESTRING (0 0, 1 1, 2 1)' as geometry(linestring)) as geom_line
from db_root;

select cast('POLYGON ((0 0, 0 2, 2 2, 2 0, 0 0))' as geography(polygon)) as geog_polygon
from db_root;

select cast('MULTIPOINT ((0 0), (1 1), (2 2))' as geometry(multipoint)) as geom_multipoint
from db_root;

select cast('MULTILINESTRING ((0 0, 1 1), (2 2, 3 3))' as geometry(multilinestring)) as geom_multiline
from db_root;

select cast('MULTIPOLYGON (((0 0, 0 1, 1 1, 1 0, 0 0)))' as geometry(multipolygon)) as geom_multipolygon
from db_root;

select cast('GEOMETRYCOLLECTION (POINT (1 1), LINESTRING (0 0, 2 2))' as geometry(geometrycollection)) as geom_collection
from db_root;
