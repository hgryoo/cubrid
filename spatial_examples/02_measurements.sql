-- point accessors and planar measurements

select st_x(st_geomfromtext('POINT (10 20)')) as x_coord,
       st_y(st_geomfromtext('POINT (10 20)')) as y_coord
from db_root;

select st_distance(st_geomfromtext('POINT (0 0)'), st_geomfromtext('POINT (3 4)')) as point_distance
from db_root;

select st_length(st_geomfromtext('LINESTRING (0 0, 3 4)')) as line_length
from db_root;

select st_area(st_geomfromtext('POLYGON ((0 0, 0 2, 2 2, 2 0, 0 0))')) as polygon_area
from db_root;
