-- =============================================================
-- 09_rtree_mbr_bridge.sql
-- MBR extraction bridge: db_spatial_get_mbr / rtree_mbr_from_db_spatial
-- Covers: db_geometry.cpp db_spatial_get_mbr() with GEOS
--         for various geometry types (Point, Line, Polygon, Multi*, Collection)
-- =============================================================

DROP TABLE IF EXISTS geo_mbr;

CREATE TABLE geo_mbr (
  id    INTEGER PRIMARY KEY,
  label VARCHAR(64),
  geom  GEOMETRY
);

CREATE INDEX idx_mbr_geom ON geo_mbr (geom) USING RTREE;

-- ----------------------------------------------------------------
-- POINT: MBR is a degenerate box (xmin=xmax, ymin=ymax)
-- ----------------------------------------------------------------
INSERT INTO geo_mbr VALUES (1, 'point_origin',  ST_GeomFromText('POINT (0 0)'));
INSERT INTO geo_mbr VALUES (2, 'point_pos',     ST_GeomFromText('POINT (3.5 7.2)'));
INSERT INTO geo_mbr VALUES (3, 'point_neg',     ST_GeomFromText('POINT (-10 -20)'));

-- Search that should find each point exactly
SELECT id, label FROM geo_mbr
WHERE ST_Intersects(geom, ST_GeomFromText('POINT (0 0)'))
ORDER BY id;
-- Expected: 1

SELECT id, label FROM geo_mbr
WHERE ST_Intersects(geom, ST_GeomFromText('POINT (3.5 7.2)'))
ORDER BY id;
-- Expected: 2

-- ----------------------------------------------------------------
-- LINESTRING: MBR = bounding box of all vertices
-- ----------------------------------------------------------------
INSERT INTO geo_mbr VALUES (10, 'line_simple',  ST_GeomFromText('LINESTRING (1 1, 5 5)'));
INSERT INTO geo_mbr VALUES (11, 'line_horiz',   ST_GeomFromText('LINESTRING (0 3, 10 3)'));
INSERT INTO geo_mbr VALUES (12, 'line_complex', ST_GeomFromText('LINESTRING (0 0, 5 10, 10 5, 15 15)'));

-- MBR for line_simple should be [1,1]-[5,5]
SELECT id, label FROM geo_mbr
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((0 0, 0 6, 6 6, 6 0, 0 0))'))
  AND id BETWEEN 10 AND 19
ORDER BY id;
-- Expected: 10 (line_simple), 11 (line_horiz if y=3 is in range 0..6), 12 (partially)

-- ----------------------------------------------------------------
-- POLYGON: MBR = bounding box of exterior ring
-- ----------------------------------------------------------------
INSERT INTO geo_mbr VALUES (20, 'poly_square',   ST_GeomFromText('POLYGON ((0 0, 0 10, 10 10, 10 0, 0 0))'));
INSERT INTO geo_mbr VALUES (21, 'poly_rect',     ST_GeomFromText('POLYGON ((5 2, 5 8, 15 8, 15 2, 5 2))'));
INSERT INTO geo_mbr VALUES (22, 'poly_with_hole',
  ST_GeomFromText('POLYGON ((0 0, 0 20, 20 20, 20 0, 0 0), (5 5, 5 15, 15 15, 15 5, 5 5))'));

-- MBR for poly_square should be [0,0]-[10,10]
SELECT ST_AsText(ST_Envelope(geom)) AS mbr_wkt
FROM geo_mbr WHERE id = 20;

SELECT id FROM geo_mbr
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((-1 -1, -1 11, 11 11, 11 -1, -1 -1))'))
  AND id BETWEEN 20 AND 29
ORDER BY id;
-- Expected: 20, 21 (overlap), 22

-- ----------------------------------------------------------------
-- MULTIPOINT: MBR covers all component points
-- ----------------------------------------------------------------
INSERT INTO geo_mbr VALUES (30, 'multipoint',
  ST_GeomFromText('MULTIPOINT ((0 0), (10 0), (5 10))'));

-- MBR should be [0,0]-[10,10]
SELECT id FROM geo_mbr
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((-1 -1, -1 11, 11 11, 11 -1, -1 -1))'))
  AND id = 30;
-- Expected: 30

-- ----------------------------------------------------------------
-- MULTIPOLYGON: MBR covers all component polygons
-- ----------------------------------------------------------------
INSERT INTO geo_mbr VALUES (40, 'multipoly',
  ST_GeomFromText('MULTIPOLYGON (((0 0, 0 5, 5 5, 5 0, 0 0)), ((20 20, 20 25, 25 25, 25 20, 20 20)))'));

-- MBR should be [0,0]-[25,25]
SELECT id FROM geo_mbr
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((-1 -1, -1 26, 26 26, 26 -1, -1 -1))'))
  AND id = 40;
-- Expected: 40

-- Only one component visible in small window
SELECT id FROM geo_mbr
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((0 0, 0 6, 6 6, 6 0, 0 0))'))
  AND id = 40;
-- Expected: 40 (MBR still intersects even though only first component does)

-- ----------------------------------------------------------------
-- Geometry collection: MBR covers all sub-geometries
-- ----------------------------------------------------------------
INSERT INTO geo_mbr VALUES (50, 'geomcoll',
  ST_GeomFromText('GEOMETRYCOLLECTION (POINT (0 0), LINESTRING (5 5, 10 10), POLYGON ((15 15, 15 20, 20 20, 20 15, 15 15)))'));

SELECT id FROM geo_mbr
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((-1 -1, -1 21, 21 21, 21 -1, -1 -1))'))
  AND id = 50;
-- Expected: 50

DROP TABLE IF EXISTS geo_mbr;
