-- null propagation, subtype, and srid metadata checks

select st_astext(cast(null as geometry)) as null_astext
from db_root;

select st_distance(cast(null as geometry), st_geomfromtext('POINT (0 0)')) as null_distance
from db_root;

select st_geometrytype(cast('POINT (9 8)' as geometry(point))) as point_type_name,
       st_geometrytype(cast('GEOMETRYCOLLECTION (POINT (1 1), LINESTRING (0 0, 1 1))' as geometry(geometrycollection))) as collection_type_name
from db_root;

select st_srid(st_setsrid(cast('POINT (1 2)' as geography(point)), 4326)) as geography_srid
from db_root;

select st_x(st_setsrid(cast('POINT (5 6)' as geometry(point)), 3857)) as x_after_setsrid,
       st_y(st_setsrid(cast('POINT (5 6)' as geometry(point)), 3857)) as y_after_setsrid
from db_root;
