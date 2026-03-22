-- topological predicates

select st_contains(
         st_geomfromtext('POLYGON ((0 0, 0 5, 5 5, 5 0, 0 0))'),
         st_geomfromtext('POINT (2 2)')
       ) as contains_inside
from db_root;

select st_contains(
         st_geomfromtext('POLYGON ((0 0, 0 5, 5 5, 5 0, 0 0))'),
         st_geomfromtext('POINT (8 8)')
       ) as contains_outside
from db_root;

select st_intersects(
         st_geomfromtext('LINESTRING (0 0, 5 5)'),
         st_geomfromtext('LINESTRING (0 5, 5 0)')
       ) as intersects_cross
from db_root;

select st_intersects(
         st_geomfromtext('POLYGON ((0 0, 0 3, 3 3, 3 0, 0 0))'),
         st_geomfromtext('POINT (5 5)')
       ) as intersects_disjoint
from db_root;

select st_contains(
         st_envelope(st_geomfromtext('LINESTRING (0 0, 4 4)')),
         st_geomfromtext('POINT (2 2)')
       ) as envelope_contains_point
from db_root;

select st_intersects(
         cast('LINESTRING (0 0, 2 2)' as geography(linestring)),
         cast('LINESTRING (0 2, 2 0)' as geography(linestring))
       ) as geography_intersects
from db_root;
