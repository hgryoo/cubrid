-- text io, srid metadata, type name and envelope

select st_astext(st_geomfromtext('POINT (10 20)')) as wkt
from db_root;

select st_geometrytype(st_geomfromtext('POLYGON ((0 0, 0 1, 1 1, 1 0, 0 0))')) as geom_type
from db_root;

select st_srid(st_setsrid(st_geomfromtext('POINT (10 20)'), 4326)) as srid
from db_root;

select st_astext(st_envelope(st_geomfromtext('LINESTRING (0 0, 2 3)'))) as envelope_wkt
from db_root;

select st_geometrytype(st_setsrid(st_geomfromtext('POINT (10 20)'), 4326)) as typed_after_srid
from db_root;

select st_astext(st_setsrid(st_geomfromtext('LINESTRING (0 0, 1 1, 2 1)'), 3857)) as linestring_wkt
from db_root;

select st_srid(cast('POINT (126.97 37.56)' as geography(point))) as geography_default_srid
from db_root;

select st_geometrytype(cast('MULTIPOINT ((0 0), (5 5))' as geometry(multipoint))) as multi_geom_type
from db_root;
