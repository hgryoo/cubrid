-- table-backed roundtrip checks for geometry/geography columns

drop table if exists spatial_example_shapes;

create table spatial_example_shapes
(
  id int,
  shape geometry,
  pt geometry(point),
  route geometry(linestring),
  area geography(polygon)
);

insert into spatial_example_shapes values
(
  1,
  st_geomfromtext('POINT (10 20)'),
  cast('POINT (10 20)' as geometry(point)),
  cast('LINESTRING (0 0, 3 4)' as geometry(linestring)),
  cast('POLYGON ((0 0, 0 2, 2 2, 2 0, 0 0))' as geography(polygon))
);

insert into spatial_example_shapes values
(
  2,
  st_setsrid(st_geomfromtext('LINESTRING (1 1, 4 5)'), 4326),
  cast('POINT (30 40)' as geometry(point)),
  cast('LINESTRING (1 1, 4 5)' as geometry(linestring)),
  cast('POLYGON ((1 1, 1 4, 5 4, 5 1, 1 1))' as geography(polygon))
);

select id,
       st_geometrytype(shape) as shape_type,
       st_astext(pt) as pt_wkt,
       st_length(route) as route_length,
       st_area(area) as area_value
from spatial_example_shapes
order by id;

select id,
       st_srid(shape) as shape_srid,
       st_x(pt) as pt_x,
       st_y(pt) as pt_y,
       st_contains(st_envelope(route), pt) as envelope_contains_pt
from spatial_example_shapes
order by id;

drop table spatial_example_shapes;
