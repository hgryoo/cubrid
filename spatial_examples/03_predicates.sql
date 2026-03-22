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
