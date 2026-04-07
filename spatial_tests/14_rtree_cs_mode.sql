-- =============================================================
-- 14_rtree_cs_mode.sql
-- Phase 6: Client-Server (CS_MODE) network protocol for R-tree
-- Tests NET_SERVER_RTREE_ADDINDEX / LOADINDEX / DELINDEX path.
-- Run this test against a live cub_server (CS_MODE).
-- =============================================================

-- ----------------------------------------------------------------
-- 1. CREATE INDEX (triggers rtree_add_index → NET_SERVER_RTREE_ADDINDEX)
-- ----------------------------------------------------------------
DROP TABLE IF EXISTS cs_geo;

CREATE TABLE cs_geo (
  id    INTEGER PRIMARY KEY,
  name  VARCHAR(64),
  geom  GEOMETRY
);

CREATE INDEX idx_cs_geom ON cs_geo (geom) USING RTREE;

-- ----------------------------------------------------------------
-- 2. INSERT rows (DML triggers rtree_insert via locator_sr.c)
-- ----------------------------------------------------------------
INSERT INTO cs_geo VALUES (1, 'box_a',  ST_GeomFromText('POLYGON((0 0, 0 10, 10 10, 10 0, 0 0))'));
INSERT INTO cs_geo VALUES (2, 'box_b',  ST_GeomFromText('POLYGON((20 20, 20 40, 40 40, 40 20, 20 20))'));
INSERT INTO cs_geo VALUES (3, 'pt_c',   ST_GeomFromText('POINT(5 5)'));
INSERT INTO cs_geo VALUES (4, 'line_d', ST_GeomFromText('LINESTRING(0 0, 50 50)'));

-- ----------------------------------------------------------------
-- 3. Spatial query over CS_MODE network round-trip
-- ----------------------------------------------------------------
SELECT id, name
FROM cs_geo
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON((0 0, 0 15, 15 15, 15 0, 0 0))'))
ORDER BY id;
-- Expected: 1 (box_a), 3 (pt_c inside box_a region), 4 (line_d starts at origin)

-- ----------------------------------------------------------------
-- 4. DROP INDEX (triggers rtree_delete_index → NET_SERVER_RTREE_DELINDEX)
-- ----------------------------------------------------------------
DROP INDEX idx_cs_geom ON cs_geo;

-- After drop, query falls back to heap scan (still returns correct results)
SELECT COUNT(*) AS heap_count
FROM cs_geo
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON((0 0, 0 50, 50 50, 50 0, 0 0))'));
-- Expected: 4

-- ----------------------------------------------------------------
-- 5. Re-create index (tests bulk load via rtree_load_index →
--    NET_SERVER_RTREE_LOADINDEX)
-- ----------------------------------------------------------------
CREATE INDEX idx_cs_geom2 ON cs_geo (geom) USING RTREE;

-- Verify the newly loaded index returns same results
SELECT id, name
FROM cs_geo
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON((0 0, 0 50, 50 50, 50 0, 0 0))'))
ORDER BY id;
-- Expected: all 4 rows

DROP TABLE IF EXISTS cs_geo;
