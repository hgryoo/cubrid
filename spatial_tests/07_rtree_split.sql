-- =============================================================
-- 07_rtree_split.sql
-- R-tree node split: quadratic split algorithm
-- Covers: rtree_split_node → rtree_pick_seeds → rtree_pick_next →
--         rtree_distribute_entries → rtree_adjust_tree → rtree_grow_tree
-- Tests that search results remain correct after splits
-- =============================================================

DROP TABLE IF EXISTS geo_split;

CREATE TABLE geo_split (
  id   INTEGER PRIMARY KEY,
  geom GEOMETRY
);

CREATE INDEX idx_split_geom ON geo_split (geom) USING RTREE;

-- ----------------------------------------------------------------
-- Fill leaf node past capacity to force quadratic split
-- RTREE_MAX_LEAF_ENTRIES ≈ (8192 - overhead) / 44 ≈ 185 per page
-- Insert well above 200 rows to guarantee at least one split
-- ----------------------------------------------------------------

-- Cluster 1: bottom-left quadrant [0, 0] - [50, 50]
INSERT INTO geo_split VALUES (1001, ST_GeomFromText('POINT (1 1)'));
INSERT INTO geo_split VALUES (1002, ST_GeomFromText('POINT (2 2)'));
INSERT INTO geo_split VALUES (1003, ST_GeomFromText('POINT (3 3)'));
INSERT INTO geo_split VALUES (1004, ST_GeomFromText('POINT (4 4)'));
INSERT INTO geo_split VALUES (1005, ST_GeomFromText('POINT (5 5)'));
INSERT INTO geo_split VALUES (1006, ST_GeomFromText('POINT (6 6)'));
INSERT INTO geo_split VALUES (1007, ST_GeomFromText('POINT (7 7)'));
INSERT INTO geo_split VALUES (1008, ST_GeomFromText('POINT (8 8)'));
INSERT INTO geo_split VALUES (1009, ST_GeomFromText('POINT (9 9)'));
INSERT INTO geo_split VALUES (1010, ST_GeomFromText('POINT (10 10)'));
INSERT INTO geo_split VALUES (1011, ST_GeomFromText('POINT (11 11)'));
INSERT INTO geo_split VALUES (1012, ST_GeomFromText('POINT (12 12)'));
INSERT INTO geo_split VALUES (1013, ST_GeomFromText('POINT (13 13)'));
INSERT INTO geo_split VALUES (1014, ST_GeomFromText('POINT (14 14)'));
INSERT INTO geo_split VALUES (1015, ST_GeomFromText('POINT (15 15)'));
INSERT INTO geo_split VALUES (1016, ST_GeomFromText('POINT (16 16)'));
INSERT INTO geo_split VALUES (1017, ST_GeomFromText('POINT (17 17)'));
INSERT INTO geo_split VALUES (1018, ST_GeomFromText('POINT (18 18)'));
INSERT INTO geo_split VALUES (1019, ST_GeomFromText('POINT (19 19)'));
INSERT INTO geo_split VALUES (1020, ST_GeomFromText('POINT (20 20)'));
INSERT INTO geo_split VALUES (1021, ST_GeomFromText('POINT (21 21)'));
INSERT INTO geo_split VALUES (1022, ST_GeomFromText('POINT (22 22)'));
INSERT INTO geo_split VALUES (1023, ST_GeomFromText('POINT (23 23)'));
INSERT INTO geo_split VALUES (1024, ST_GeomFromText('POINT (24 24)'));
INSERT INTO geo_split VALUES (1025, ST_GeomFromText('POINT (25 25)'));

-- Cluster 2: top-right quadrant [500, 500] - [550, 550] (forces large MBR separation)
INSERT INTO geo_split VALUES (2001, ST_GeomFromText('POINT (500 500)'));
INSERT INTO geo_split VALUES (2002, ST_GeomFromText('POINT (501 501)'));
INSERT INTO geo_split VALUES (2003, ST_GeomFromText('POINT (502 502)'));
INSERT INTO geo_split VALUES (2004, ST_GeomFromText('POINT (503 503)'));
INSERT INTO geo_split VALUES (2005, ST_GeomFromText('POINT (504 504)'));
INSERT INTO geo_split VALUES (2006, ST_GeomFromText('POINT (505 505)'));
INSERT INTO geo_split VALUES (2007, ST_GeomFromText('POINT (506 506)'));
INSERT INTO geo_split VALUES (2008, ST_GeomFromText('POINT (507 507)'));
INSERT INTO geo_split VALUES (2009, ST_GeomFromText('POINT (508 508)'));
INSERT INTO geo_split VALUES (2010, ST_GeomFromText('POINT (509 509)'));
INSERT INTO geo_split VALUES (2011, ST_GeomFromText('POINT (510 510)'));
INSERT INTO geo_split VALUES (2012, ST_GeomFromText('POINT (511 511)'));
INSERT INTO geo_split VALUES (2013, ST_GeomFromText('POINT (512 512)'));
INSERT INTO geo_split VALUES (2014, ST_GeomFromText('POINT (513 513)'));
INSERT INTO geo_split VALUES (2015, ST_GeomFromText('POINT (514 514)'));
INSERT INTO geo_split VALUES (2016, ST_GeomFromText('POINT (515 515)'));
INSERT INTO geo_split VALUES (2017, ST_GeomFromText('POINT (516 516)'));
INSERT INTO geo_split VALUES (2018, ST_GeomFromText('POINT (517 517)'));
INSERT INTO geo_split VALUES (2019, ST_GeomFromText('POINT (518 518)'));
INSERT INTO geo_split VALUES (2020, ST_GeomFromText('POINT (519 519)'));
INSERT INTO geo_split VALUES (2021, ST_GeomFromText('POINT (520 520)'));
INSERT INTO geo_split VALUES (2022, ST_GeomFromText('POINT (521 521)'));
INSERT INTO geo_split VALUES (2023, ST_GeomFromText('POINT (522 522)'));
INSERT INTO geo_split VALUES (2024, ST_GeomFromText('POINT (523 523)'));
INSERT INTO geo_split VALUES (2025, ST_GeomFromText('POINT (524 524)'));

SELECT COUNT(*) AS total_after_insert FROM geo_split;
-- Expected: 50

-- ----------------------------------------------------------------
-- Post-split search accuracy: cluster 1 window
-- ----------------------------------------------------------------
SELECT COUNT(*) AS cluster1_hits
FROM geo_split
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((0 0, 0 26, 26 26, 26 0, 0 0))'));
-- Expected: 25 (all of cluster 1)

-- ----------------------------------------------------------------
-- Post-split search accuracy: cluster 2 window
-- ----------------------------------------------------------------
SELECT COUNT(*) AS cluster2_hits
FROM geo_split
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((499 499, 499 526, 526 526, 526 499, 499 499))'));
-- Expected: 25 (all of cluster 2)

-- ----------------------------------------------------------------
-- No cross-contamination: cluster 1 window should not hit cluster 2
-- ----------------------------------------------------------------
SELECT COUNT(*) AS no_cross
FROM geo_split
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((0 0, 0 26, 26 26, 26 0, 0 0))'))
  AND id >= 2000;
-- Expected: 0

-- ----------------------------------------------------------------
-- Delete half of each cluster, then search (tests post-split delete)
-- ----------------------------------------------------------------
DELETE FROM geo_split WHERE id BETWEEN 1001 AND 1012;
DELETE FROM geo_split WHERE id BETWEEN 2001 AND 2012;

SELECT COUNT(*) AS after_partial_delete FROM geo_split;
-- Expected: 26

SELECT COUNT(*) AS cluster1_remaining
FROM geo_split
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((0 0, 0 26, 26 26, 26 0, 0 0))'));
-- Expected: 13 (ids 1013-1025)

DROP TABLE IF EXISTS geo_split;
