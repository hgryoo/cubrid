-- =============================================================
-- 11_rtree_edge_cases.sql
-- Edge cases and stress tests
-- Covers: NULL geometry, empty table scan, duplicate MBRs,
--         very large/small coordinates, concurrent DDL correctness
-- =============================================================

DROP TABLE IF EXISTS geo_edge;

CREATE TABLE geo_edge (
  id   INTEGER PRIMARY KEY,
  geom GEOMETRY
);

CREATE INDEX idx_edge_geom ON geo_edge (geom) USING RTREE;

-- ----------------------------------------------------------------
-- Edge case 1: NULL geometry values
-- R-tree should skip NULLs (no entry added to index)
-- ----------------------------------------------------------------
INSERT INTO geo_edge VALUES (1, NULL);
INSERT INTO geo_edge VALUES (2, NULL);
INSERT INTO geo_edge VALUES (3, ST_GeomFromText('POINT (1 1)'));

SELECT COUNT(*) AS total FROM geo_edge;
-- Expected: 3

SELECT COUNT(*) AS spatial_results
FROM geo_edge
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((-100 -100, -100 100, 100 100, 100 -100, -100 -100))'));
-- Expected: 1 (only id=3; NULLs should not appear)

-- ----------------------------------------------------------------
-- Edge case 2: Search on empty table
-- ----------------------------------------------------------------
DROP TABLE IF EXISTS geo_empty;
CREATE TABLE geo_empty (id INTEGER PRIMARY KEY, geom GEOMETRY);
CREATE INDEX idx_empty ON geo_empty (geom) USING RTREE;

SELECT COUNT(*) AS empty_search
FROM geo_empty
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((0 0, 0 10, 10 10, 10 0, 0 0))'));
-- Expected: 0

DROP TABLE IF EXISTS geo_empty;

-- ----------------------------------------------------------------
-- Edge case 3: Duplicate MBRs (same coordinates, different OIDs)
-- ----------------------------------------------------------------
INSERT INTO geo_edge VALUES (10, ST_GeomFromText('POINT (5 5)'));
INSERT INTO geo_edge VALUES (11, ST_GeomFromText('POINT (5 5)'));
INSERT INTO geo_edge VALUES (12, ST_GeomFromText('POINT (5 5)'));

SELECT COUNT(*) AS dup_mbr_hits
FROM geo_edge
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((4 4, 4 6, 6 6, 6 4, 4 4))'));
-- Expected: 3 (all three duplicates)

DELETE FROM geo_edge WHERE id = 11;

SELECT COUNT(*) AS after_dup_delete
FROM geo_edge
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((4 4, 4 6, 6 6, 6 4, 4 4))'));
-- Expected: 2

-- ----------------------------------------------------------------
-- Edge case 4: Very large coordinates
-- ----------------------------------------------------------------
INSERT INTO geo_edge VALUES (20, ST_GeomFromText('POINT (1e6 1e6)'));
INSERT INTO geo_edge VALUES (21, ST_GeomFromText('POINT (-1e6 -1e6)'));
INSERT INTO geo_edge VALUES (22, ST_GeomFromText('POLYGON ((999000 999000, 999000 1001000, 1001000 1001000, 1001000 999000, 999000 999000))'));

SELECT COUNT(*) AS large_coord_hits
FROM geo_edge
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((999999 999999, 999999 1000001, 1000001 1000001, 1000001 999999, 999999 999999))'));
-- Expected: 2 (id=20 at 1e6,1e6 and id=22 polygon)

-- ----------------------------------------------------------------
-- Edge case 5: Very small coordinates (floating point precision)
-- ----------------------------------------------------------------
INSERT INTO geo_edge VALUES (30, ST_GeomFromText('POINT (0.0001 0.0001)'));
INSERT INTO geo_edge VALUES (31, ST_GeomFromText('POINT (0.0002 0.0002)'));

SELECT COUNT(*) AS small_coord_hits
FROM geo_edge
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((0 0, 0 0.0005, 0.0005 0.0005, 0.0005 0, 0 0))'));
-- Expected: 2

-- ----------------------------------------------------------------
-- Edge case 6: Single-row table (degenerate tree = root is leaf with 1 entry)
-- ----------------------------------------------------------------
DROP TABLE IF EXISTS geo_single;
CREATE TABLE geo_single (id INTEGER PRIMARY KEY, geom GEOMETRY);
CREATE INDEX idx_single ON geo_single (geom) USING RTREE;
INSERT INTO geo_single VALUES (1, ST_GeomFromText('POINT (42 42)'));

SELECT id FROM geo_single
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((41 41, 41 43, 43 43, 43 41, 41 41))'));
-- Expected: 1

DELETE FROM geo_single WHERE id = 1;

SELECT COUNT(*) AS empty_after_single_delete FROM geo_single;
-- Expected: 0

SELECT COUNT(*) AS search_empty_tree
FROM geo_single
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((0 0, 0 100, 100 100, 100 0, 0 0))'));
-- Expected: 0

DROP TABLE IF EXISTS geo_single;

-- ----------------------------------------------------------------
-- Edge case 7: Table with R-tree index — CREATE INDEX after data load
-- ----------------------------------------------------------------
DROP TABLE IF EXISTS geo_late_index;
CREATE TABLE geo_late_index (id INTEGER PRIMARY KEY, geom GEOMETRY);

-- Insert data BEFORE creating the index
INSERT INTO geo_late_index VALUES (1, ST_GeomFromText('POINT (1 1)'));
INSERT INTO geo_late_index VALUES (2, ST_GeomFromText('POINT (2 2)'));
INSERT INTO geo_late_index VALUES (3, ST_GeomFromText('POINT (3 3)'));

-- Now create the R-tree index (should load all existing rows)
CREATE INDEX idx_late ON geo_late_index (geom) USING RTREE;

SELECT COUNT(*) AS late_index_hits
FROM geo_late_index
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((0 0, 0 4, 4 4, 4 0, 0 0))'));
-- Expected: 3

DROP TABLE IF EXISTS geo_late_index;
DROP TABLE IF EXISTS geo_edge;
