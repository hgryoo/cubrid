-- geometric processing queries using inline spatial datasets

select q.id,
       st_geometrytype(q.geom) as geom_type,
       st_astext(st_envelope(q.geom)) as bbox_wkt,
       st_area(st_envelope(q.geom)) as bbox_area
from (
  select 1 as id, st_geomfromtext('LINESTRING (0 0, 3 4)') as geom from db_root
  union all
  select 2 as id, st_geomfromtext('POLYGON ((2 1, 2 4, 6 4, 6 1, 2 1))') as geom from db_root
  union all
  select 3 as id, st_geomfromtext('POINT (8 3)') as geom from db_root
) q
order by q.id;

select p.id,
       st_contains(st_envelope(p.geom), st_geomfromtext('POINT (2 2)')) as bbox_contains_probe,
       st_intersects(st_envelope(p.geom), st_geomfromtext('LINESTRING (1 3, 7 3)')) as bbox_intersects_scanline
from (
  select 1 as id, st_geomfromtext('LINESTRING (0 0, 3 4)') as geom from db_root
  union all
  select 2 as id, st_geomfromtext('POLYGON ((2 1, 2 4, 6 4, 6 1, 2 1))') as geom from db_root
  union all
  select 3 as id, st_geomfromtext('LINESTRING (7 0, 9 3)') as geom from db_root
) p
order by p.id;

select a.id as from_id,
       b.id as to_id,
       st_distance(a.geom, b.geom) as pair_distance
from (
  select 1 as id, st_geomfromtext('POINT (0 0)') as geom from db_root
  union all
  select 2 as id, st_geomfromtext('POINT (3 4)') as geom from db_root
  union all
  select 3 as id, st_geomfromtext('POINT (6 8)') as geom from db_root
) a,
(
  select 1 as id, st_geomfromtext('POINT (0 0)') as geom from db_root
  union all
  select 2 as id, st_geomfromtext('POINT (3 4)') as geom from db_root
  union all
  select 3 as id, st_geomfromtext('POINT (6 8)') as geom from db_root
) b
where a.id < b.id
order by a.id, b.id;

select s.id,
       st_length(s.route) as route_length,
       st_astext(st_envelope(s.route)) as route_bbox
from (
  select 1 as id, st_setsrid(st_geomfromtext('LINESTRING (126.97 37.56, 127.02 37.50)'), 4326) as route from db_root
  union all
  select 2 as id, st_setsrid(st_geomfromtext('LINESTRING (127.00 37.55, 127.05 37.58)'), 4326) as route from db_root
) s
order by s.id;

select st_intersects(a.geom, b.geom) as route_intersects,
       st_distance(a.geom, b.geom) as route_distance
from (
  select st_geomfromtext('LINESTRING (0 0, 4 4)') as geom from db_root
) a,
(
  select st_geomfromtext('LINESTRING (0 4, 4 0)') as geom from db_root
) b;
