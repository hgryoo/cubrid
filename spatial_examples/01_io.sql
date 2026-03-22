-- text io, srid metadata, type name and envelope

select st_astext(st_geomfromtext('POINT (10 20)')) as wkt
from db_root;

select st_geometrytype(st_geomfromtext('POLYGON ((0 0, 0 1, 1 1, 1 0, 0 0))')) as geom_type
from db_root;

select st_srid(st_setsrid(st_geomfromtext('POINT (10 20)'), 4326)) as srid
from db_root;

select st_astext(st_envelope(st_geomfromtext('LINESTRING (0 0, 2 3)'))) as envelope_wkt
from db_root;
