-- =============================================================
-- 02_rtree_insert.sql
-- R-tree DML: INSERT path
-- Covers: rtree_insert → choose_leaf → append_leaf_entry → WAL
--         locator_sr.c RTREE_INDEX dispatch
-- =============================================================

DROP TABLE IF EXISTS geo_objects;

CREATE TABLE geo_objects (
  id       INTEGER PRIMARY KEY,
  category VARCHAR(32),
  geom     GEOMETRY
);

CREATE INDEX idx_geo_geom ON geo_objects (geom) USING RTREE;

-- ----------------------------------------------------------------
-- Insert points (each becomes a degenerate MBR: xmin=xmax, ymin=ymax)
-- ----------------------------------------------------------------
INSERT INTO geo_objects VALUES (1,  'point', ST_GeomFromText('POINT (0 0)'));
INSERT INTO geo_objects VALUES (2,  'point', ST_GeomFromText('POINT (5 5)'));
INSERT INTO geo_objects VALUES (3,  'point', ST_GeomFromText('POINT (10 10)'));
INSERT INTO geo_objects VALUES (4,  'point', ST_GeomFromText('POINT (-5 -5)'));
INSERT INTO geo_objects VALUES (5,  'point', ST_GeomFromText('POINT (100 200)'));

-- ----------------------------------------------------------------
-- Insert linestrings (MBR wraps bounding box of the line)
-- ----------------------------------------------------------------
INSERT INTO geo_objects VALUES (6,  'line', ST_GeomFromText('LINESTRING (0 0, 10 10)'));
INSERT INTO geo_objects VALUES (7,  'line', ST_GeomFromText('LINESTRING (5 0, 5 20)'));
INSERT INTO geo_objects VALUES (8,  'line', ST_GeomFromText('LINESTRING (-10 0, 10 0)'));

-- ----------------------------------------------------------------
-- Insert polygons (MBR = bounding box of the polygon)
-- ----------------------------------------------------------------
INSERT INTO geo_objects VALUES (9,  'poly', ST_GeomFromText('POLYGON ((0 0, 0 10, 10 10, 10 0, 0 0))'));
INSERT INTO geo_objects VALUES (10, 'poly', ST_GeomFromText('POLYGON ((20 20, 20 30, 30 30, 30 20, 20 20))'));
INSERT INTO geo_objects VALUES (11, 'poly', ST_GeomFromText('POLYGON ((-5 -5, -5 5, 5 5, 5 -5, -5 -5))'));
INSERT INTO geo_objects VALUES (12, 'poly', ST_GeomFromText('POLYGON ((0 0, 0 100, 100 100, 100 0, 0 0))'));

-- ----------------------------------------------------------------
-- Bulk insert to trigger node splits (fill leaf page → quadratic split)
-- Insert 50 rows spread across coordinate space
-- ----------------------------------------------------------------
INSERT INTO geo_objects VALUES (101, 'bulk', ST_GeomFromText('POINT (1 1)'));
INSERT INTO geo_objects VALUES (102, 'bulk', ST_GeomFromText('POINT (2 2)'));
INSERT INTO geo_objects VALUES (103, 'bulk', ST_GeomFromText('POINT (3 3)'));
INSERT INTO geo_objects VALUES (104, 'bulk', ST_GeomFromText('POINT (4 4)'));
INSERT INTO geo_objects VALUES (105, 'bulk', ST_GeomFromText('POINT (6 6)'));
INSERT INTO geo_objects VALUES (106, 'bulk', ST_GeomFromText('POINT (7 7)'));
INSERT INTO geo_objects VALUES (107, 'bulk', ST_GeomFromText('POINT (8 8)'));
INSERT INTO geo_objects VALUES (108, 'bulk', ST_GeomFromText('POINT (9 9)'));
INSERT INTO geo_objects VALUES (109, 'bulk', ST_GeomFromText('POINT (11 11)'));
INSERT INTO geo_objects VALUES (110, 'bulk', ST_GeomFromText('POINT (12 12)'));
INSERT INTO geo_objects VALUES (111, 'bulk', ST_GeomFromText('POINT (13 13)'));
INSERT INTO geo_objects VALUES (112, 'bulk', ST_GeomFromText('POINT (14 14)'));
INSERT INTO geo_objects VALUES (113, 'bulk', ST_GeomFromText('POINT (15 15)'));
INSERT INTO geo_objects VALUES (114, 'bulk', ST_GeomFromText('POINT (16 16)'));
INSERT INTO geo_objects VALUES (115, 'bulk', ST_GeomFromText('POINT (17 17)'));
INSERT INTO geo_objects VALUES (116, 'bulk', ST_GeomFromText('POINT (18 18)'));
INSERT INTO geo_objects VALUES (117, 'bulk', ST_GeomFromText('POINT (19 19)'));
INSERT INTO geo_objects VALUES (118, 'bulk', ST_GeomFromText('POINT (20 20)'));
INSERT INTO geo_objects VALUES (119, 'bulk', ST_GeomFromText('POINT (21 21)'));
INSERT INTO geo_objects VALUES (120, 'bulk', ST_GeomFromText('POINT (22 22)'));
INSERT INTO geo_objects VALUES (121, 'bulk', ST_GeomFromText('POINT (23 23)'));
INSERT INTO geo_objects VALUES (122, 'bulk', ST_GeomFromText('POINT (24 24)'));
INSERT INTO geo_objects VALUES (123, 'bulk', ST_GeomFromText('POINT (25 25)'));
INSERT INTO geo_objects VALUES (124, 'bulk', ST_GeomFromText('POINT (50 50)'));
INSERT INTO geo_objects VALUES (125, 'bulk', ST_GeomFromText('POINT (75 75)'));

-- ----------------------------------------------------------------
-- Verify count
-- ----------------------------------------------------------------
SELECT COUNT(*) AS total_rows FROM geo_objects;
-- Expected: 37 rows

SELECT category, COUNT(*) AS cnt
FROM geo_objects
GROUP BY category
ORDER BY category;

-- ----------------------------------------------------------------
-- NULL geometry insert (should be skipped by RTREE_INDEX dispatch)
-- ----------------------------------------------------------------
INSERT INTO geo_objects VALUES (200, 'null_geom', NULL);
SELECT COUNT(*) AS rows_with_null FROM geo_objects WHERE geom IS NULL;

DROP TABLE IF EXISTS geo_objects;
