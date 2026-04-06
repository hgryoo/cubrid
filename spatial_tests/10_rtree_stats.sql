-- =============================================================
-- 10_rtree_stats.sql
-- R-tree statistics: rtree_get_stats(), UPDATE STATISTICS
-- Covers: Phase 4 optimizer integration — stats collection
-- =============================================================

DROP TABLE IF EXISTS geo_stats;

CREATE TABLE geo_stats (
  id   INTEGER PRIMARY KEY,
  geom GEOMETRY
);

CREATE INDEX idx_stats_geom ON geo_stats (geom) USING RTREE;

-- Insert enough rows for meaningful stats
INSERT INTO geo_stats VALUES (1,  ST_GeomFromText('POINT (10 10)'));
INSERT INTO geo_stats VALUES (2,  ST_GeomFromText('POINT (20 20)'));
INSERT INTO geo_stats VALUES (3,  ST_GeomFromText('POINT (30 30)'));
INSERT INTO geo_stats VALUES (4,  ST_GeomFromText('POINT (40 40)'));
INSERT INTO geo_stats VALUES (5,  ST_GeomFromText('POINT (50 50)'));
INSERT INTO geo_stats VALUES (6,  ST_GeomFromText('POLYGON ((0 0, 0 100, 100 100, 100 0, 0 0))'));
INSERT INTO geo_stats VALUES (7,  ST_GeomFromText('LINESTRING (0 0, 100 0)'));
INSERT INTO geo_stats VALUES (8,  ST_GeomFromText('LINESTRING (0 100, 100 100)'));
INSERT INTO geo_stats VALUES (9,  ST_GeomFromText('POINT (60 60)'));
INSERT INTO geo_stats VALUES (10, ST_GeomFromText('POINT (70 70)'));

-- ----------------------------------------------------------------
-- Trigger statistics update for the R-tree index
-- rtree_get_stats() should fill BTREE_STATS from root header
-- ----------------------------------------------------------------
UPDATE STATISTICS ON geo_stats;

-- ----------------------------------------------------------------
-- After UPDATE STATISTICS, queries should still work correctly
-- (verifies stats update did not corrupt index)
-- ----------------------------------------------------------------
SELECT COUNT(*) AS all_rows FROM geo_stats;
-- Expected: 10

SELECT COUNT(*) AS in_center
FROM geo_stats
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((25 25, 25 75, 75 75, 75 25, 25 25))'));
-- Expected: ≥5 (points at 30,40,50,60,70 + polygon + lines)

-- ----------------------------------------------------------------
-- Check index metadata through catalog
-- ----------------------------------------------------------------
SELECT i.class_name, i.index_name, i.index_type,
       i.is_unique, i.key_count
FROM db_index i
WHERE i.class_name = 'geo_stats'
ORDER BY i.index_name;

-- ----------------------------------------------------------------
-- SHOW INDEX equivalent
-- ----------------------------------------------------------------
SHOW INDEX FROM geo_stats;

-- ----------------------------------------------------------------
-- UPDATE STATISTICS ALL: global stats refresh including R-tree
-- ----------------------------------------------------------------
UPDATE STATISTICS ON CLASS geo_stats WITH FULLSCAN;

SELECT COUNT(*) AS still_correct FROM geo_stats;
-- Expected: 10

DROP TABLE IF EXISTS geo_stats;
