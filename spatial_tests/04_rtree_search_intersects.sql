-- =============================================================
-- 04_rtree_search_intersects.sql
-- R-tree search: RTREE_SEARCH_INTERSECTS mode
-- Covers: rtree_search → rtree_search_page (DFS),
--         scan_open_rtree_scan / scan_next_rtree_scan
-- =============================================================

DROP TABLE IF EXISTS geo_search;

CREATE TABLE geo_search (
  id    INTEGER PRIMARY KEY,
  label VARCHAR(64),
  geom  GEOMETRY
);

CREATE INDEX idx_search_geom ON geo_search (geom) USING RTREE;

-- Setup: objects spread across a 100x100 grid
INSERT INTO geo_search VALUES (1,  'origin',       ST_GeomFromText('POINT (0 0)'));
INSERT INTO geo_search VALUES (2,  'center',       ST_GeomFromText('POINT (50 50)'));
INSERT INTO geo_search VALUES (3,  'far_corner',   ST_GeomFromText('POINT (100 100)'));
INSERT INTO geo_search VALUES (4,  'small_box',    ST_GeomFromText('POLYGON ((10 10, 10 20, 20 20, 20 10, 10 10))'));
INSERT INTO geo_search VALUES (5,  'medium_box',   ST_GeomFromText('POLYGON ((30 30, 30 60, 60 60, 60 30, 30 30))'));
INSERT INTO geo_search VALUES (6,  'large_box',    ST_GeomFromText('POLYGON ((0 0, 0 100, 100 100, 100 0, 0 0))'));
INSERT INTO geo_search VALUES (7,  'line_diag',    ST_GeomFromText('LINESTRING (0 0, 100 100)'));
INSERT INTO geo_search VALUES (8,  'line_horiz',   ST_GeomFromText('LINESTRING (0 50, 100 50)'));
INSERT INTO geo_search VALUES (9,  'neg_point',    ST_GeomFromText('POINT (-10 -10)'));
INSERT INTO geo_search VALUES (10, 'far_point',    ST_GeomFromText('POINT (200 200)'));

-- ----------------------------------------------------------------
-- Search: window [0,0]-[25,25] -- should intersect origin, small_box, large_box, line_diag
-- ----------------------------------------------------------------
SELECT g.id, g.label
FROM geo_search g
WHERE ST_Intersects(g.geom, ST_GeomFromText('POLYGON ((0 0, 0 25, 25 25, 25 0, 0 0))'))
ORDER BY g.id;
-- Expected ids: 1 (origin=point(0,0) on boundary), 4 (small_box overlaps), 6, 7

-- ----------------------------------------------------------------
-- Search: window [40,40]-[70,70] -- should intersect center, medium_box, large_box, line_diag, line_horiz
-- ----------------------------------------------------------------
SELECT g.id, g.label
FROM geo_search g
WHERE ST_Intersects(g.geom, ST_GeomFromText('POLYGON ((40 40, 40 70, 70 70, 70 40, 40 40))'))
ORDER BY g.id;
-- Expected ids: 2, 5, 6, 7, 8

-- ----------------------------------------------------------------
-- Search: point query (degenerate window = single point)
-- ----------------------------------------------------------------
SELECT g.id, g.label
FROM geo_search g
WHERE ST_Intersects(g.geom, ST_GeomFromText('POINT (50 50)'))
ORDER BY g.id;
-- Expected: 2 (exact match), 5 (contains point), 6, 7, 8

-- ----------------------------------------------------------------
-- Search: window that matches nothing
-- ----------------------------------------------------------------
SELECT COUNT(*) AS empty_result
FROM geo_search g
WHERE ST_Intersects(g.geom, ST_GeomFromText('POLYGON ((300 300, 300 400, 400 400, 400 300, 300 300))'));
-- Expected: 0

-- ----------------------------------------------------------------
-- Search: window covering everything
-- ----------------------------------------------------------------
SELECT COUNT(*) AS all_except_far
FROM geo_search g
WHERE ST_Intersects(g.geom, ST_GeomFromText('POLYGON ((-50 -50, -50 250, 250 250, 250 -50, -50 -50))'));
-- Expected: 10 (all rows)

-- ----------------------------------------------------------------
-- Index scan with JOIN (tests scan integration with heap fetch)
-- ----------------------------------------------------------------
CREATE TABLE geo_labels (
  id    INTEGER PRIMARY KEY,
  desc  VARCHAR(128)
);
INSERT INTO geo_labels VALUES (1, 'The origin point');
INSERT INTO geo_labels VALUES (4, 'A small bounding box');
INSERT INTO geo_labels VALUES (6, 'The large bounding box');

SELECT g.id, g.label, l.desc
FROM geo_search g
JOIN geo_labels l ON g.id = l.id
WHERE ST_Intersects(g.geom, ST_GeomFromText('POLYGON ((0 0, 0 25, 25 25, 25 0, 0 0))'))
ORDER BY g.id;

DROP TABLE IF EXISTS geo_labels;
DROP TABLE IF EXISTS geo_search;
