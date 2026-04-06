-- =============================================================
-- 03_rtree_delete.sql
-- R-tree DML: DELETE path
-- Covers: rtree_delete → rtree_find_and_delete → RVRT_LEAF_DELETE WAL
--         condense_tree, shorten_tree (D4)
-- =============================================================

DROP TABLE IF EXISTS geo_del;

CREATE TABLE geo_del (
  id   INTEGER PRIMARY KEY,
  geom GEOMETRY
);

CREATE INDEX idx_geo_del ON geo_del (geom) USING RTREE;

-- Setup: insert a set of rows
INSERT INTO geo_del VALUES (1,  ST_GeomFromText('POINT (0 0)'));
INSERT INTO geo_del VALUES (2,  ST_GeomFromText('POINT (5 5)'));
INSERT INTO geo_del VALUES (3,  ST_GeomFromText('POINT (10 10)'));
INSERT INTO geo_del VALUES (4,  ST_GeomFromText('POINT (15 15)'));
INSERT INTO geo_del VALUES (5,  ST_GeomFromText('POINT (20 20)'));
INSERT INTO geo_del VALUES (6,  ST_GeomFromText('POLYGON ((0 0, 0 5, 5 5, 5 0, 0 0))'));
INSERT INTO geo_del VALUES (7,  ST_GeomFromText('POLYGON ((10 10, 10 20, 20 20, 20 10, 10 10))'));
INSERT INTO geo_del VALUES (8,  ST_GeomFromText('LINESTRING (0 0, 100 100)'));
INSERT INTO geo_del VALUES (9,  ST_GeomFromText('POINT (50 50)'));
INSERT INTO geo_del VALUES (10, ST_GeomFromText('POINT (99 99)'));

SELECT COUNT(*) AS before_delete FROM geo_del;
-- Expected: 10

-- ----------------------------------------------------------------
-- Simple single-row delete
-- ----------------------------------------------------------------
DELETE FROM geo_del WHERE id = 1;
SELECT COUNT(*) AS after_delete_1 FROM geo_del;
-- Expected: 9

-- ----------------------------------------------------------------
-- Delete by spatial predicate (tests full scan with rtree index)
-- ----------------------------------------------------------------
DELETE FROM geo_del WHERE id = 2;
DELETE FROM geo_del WHERE id = 3;
SELECT COUNT(*) AS after_delete_3 FROM geo_del;
-- Expected: 7

-- ----------------------------------------------------------------
-- Delete polygon entries (MBR-based delete in index)
-- ----------------------------------------------------------------
DELETE FROM geo_del WHERE category = 'poly';  -- no category col, use id
DELETE FROM geo_del WHERE id IN (6, 7);
SELECT COUNT(*) AS after_delete_polygons FROM geo_del;
-- Expected: 5

-- ----------------------------------------------------------------
-- Delete all remaining rows (triggers shorten_tree / D4 path)
-- ----------------------------------------------------------------
DELETE FROM geo_del WHERE id IN (4, 5, 8, 9, 10);
SELECT COUNT(*) AS after_delete_all FROM geo_del;
-- Expected: 0

-- ----------------------------------------------------------------
-- Reinsert after full delete (tests tree rebuild from empty root)
-- ----------------------------------------------------------------
INSERT INTO geo_del VALUES (100, ST_GeomFromText('POINT (1 1)'));
INSERT INTO geo_del VALUES (101, ST_GeomFromText('POINT (2 2)'));
SELECT COUNT(*) AS after_reinsert FROM geo_del;
-- Expected: 2

DROP TABLE IF EXISTS geo_del;
