-- basic type parsing and subtype preservation

select cast('POINT (127.0 37.5)' as geometry(point)) as geom_point
from db_root;

select cast('LINESTRING (0 0, 1 1, 2 1)' as geometry(linestring)) as geom_line
from db_root;

select cast('POLYGON ((0 0, 0 2, 2 2, 2 0, 0 0))' as geography(polygon)) as geog_polygon
from db_root;
