-- =============================================================
-- 13_rtree_xasl_generation.sql
-- Phase 5-C: XASL generation for R-tree index scans
-- Tests the pt_find_rtree_predicate / pt_find_rtree_index_for_col
-- path in pt_to_class_spec_list that creates ACCESS_METHOD_INDEX_RTREE
-- access specs instead of falling back to a heap scan.
-- =============================================================

DROP TABLE IF EXISTS xasl_geo;

CREATE TABLE xasl_geo (
  id    INTEGER PRIMARY KEY,
  name  VARCHAR(64),
  geom  GEOMETRY
);

CREATE INDEX idx_xasl_geom ON xasl_geo (geom) USING RTREE;

-- Insert test data spread over a grid
INSERT INTO xasl_geo VALUES (1, 'q1_small',  ST_GeomFromText('POLYGON((0 0, 0 5, 5 5, 5 0, 0 0))'));
INSERT INTO xasl_geo VALUES (2, 'q1_big',    ST_GeomFromText('POLYGON((0 0, 0 20, 20 20, 20 0, 0 0))'));
INSERT INTO xasl_geo VALUES (3, 'q2_pt',     ST_GeomFromText('POINT(50 50)'));
INSERT INTO xasl_geo VALUES (4, 'q3_line',   ST_GeomFromText('LINESTRING(60 0, 60 100)'));
INSERT INTO xasl_geo VALUES (5, 'full_cover',ST_GeomFromText('POLYGON((0 0, 0 100, 100 100, 100 0, 0 0))'));

-- ----------------------------------------------------------------
-- 1. ST_Intersects with literal geometry — triggers R-tree path
-- ----------------------------------------------------------------
SELECT id, name
FROM xasl_geo
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON((0 0, 0 10, 10 10, 10 0, 0 0))'))
ORDER BY id;
-- Expected: 1 (small_q1 intersects), 2 (big_q1 intersects), 5 (full_cover intersects)

-- ----------------------------------------------------------------
-- 2. ST_Contains — geometry column fully contains search region
-- ----------------------------------------------------------------
SELECT id, name
FROM xasl_geo
WHERE ST_Contains(geom, ST_GeomFromText('POINT(1 1)'))
ORDER BY id;
-- Expected: 1, 2, 5 (all polygons that contain the point (1,1))

-- ----------------------------------------------------------------
-- 3. ST_Within — search geometry fully within column geometry
-- ----------------------------------------------------------------
SELECT id, name
FROM xasl_geo
WHERE ST_Within(geom, ST_GeomFromText('POLYGON((-1 -1, -1 101, 101 101, 101 -1, -1 -1))'))
ORDER BY id;
-- Expected: all 5 rows (the large polygon contains all MBRs)

-- ----------------------------------------------------------------
-- 4. R-tree scan with additional WHERE predicate (post-filter)
-- ----------------------------------------------------------------
SELECT id, name
FROM xasl_geo
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON((0 0, 0 100, 100 100, 100 0, 0 0))'))
  AND id > 2
ORDER BY id;
-- Expected: 3, 4, 5

-- ----------------------------------------------------------------
-- 5. R-tree scan in subquery
-- ----------------------------------------------------------------
SELECT t.id, t.name
FROM (
  SELECT id, name
  FROM xasl_geo
  WHERE ST_Intersects(geom, ST_GeomFromText('POINT(50 50)'))
) t
ORDER BY t.id;
-- Expected: 3 (exact point match), 5 (polygon contains the point)

-- ----------------------------------------------------------------
-- 6. Verify R-tree scan result count matches heap scan
-- ----------------------------------------------------------------
SELECT COUNT(*) AS rtree_count
FROM xasl_geo
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON((0 0, 0 100, 100 100, 100 0, 0 0))'));
-- Expected: 5

DROP TABLE IF EXISTS xasl_geo;
