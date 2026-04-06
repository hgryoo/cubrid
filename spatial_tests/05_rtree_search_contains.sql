-- =============================================================
-- 05_rtree_search_contains.sql
-- R-tree search: RTREE_SEARCH_CONTAINS and RTREE_SEARCH_CONTAINED modes
-- Covers: rtree_mbr_contains, rtree_mbr_intersects logic in search
-- =============================================================

DROP TABLE IF EXISTS geo_contain;

CREATE TABLE geo_contain (
  id    INTEGER PRIMARY KEY,
  label VARCHAR(64),
  geom  GEOMETRY
);

CREATE INDEX idx_contain_geom ON geo_contain (geom) USING RTREE;

INSERT INTO geo_contain VALUES (1, 'point_inside',   ST_GeomFromText('POINT (5 5)'));
INSERT INTO geo_contain VALUES (2, 'point_on_edge',  ST_GeomFromText('POINT (0 5)'));
INSERT INTO geo_contain VALUES (3, 'point_outside',  ST_GeomFromText('POINT (15 15)'));
INSERT INTO geo_contain VALUES (4, 'small_inside',   ST_GeomFromText('POLYGON ((2 2, 2 8, 8 8, 8 2, 2 2))'));
INSERT INTO geo_contain VALUES (5, 'exact_match',    ST_GeomFromText('POLYGON ((0 0, 0 10, 10 10, 10 0, 0 0))'));
INSERT INTO geo_contain VALUES (6, 'partly_outside', ST_GeomFromText('POLYGON ((5 5, 5 15, 15 15, 15 5, 5 5))'));
INSERT INTO geo_contain VALUES (7, 'fully_outside',  ST_GeomFromText('POLYGON ((20 20, 20 30, 30 30, 30 20, 20 20))'));
INSERT INTO geo_contain VALUES (8, 'contains_query', ST_GeomFromText('POLYGON ((-5 -5, -5 15, 15 15, 15 -5, -5 -5))'));

-- Search window: POLYGON ((0 0, 0 10, 10 10, 10 0, 0 0))

-- ----------------------------------------------------------------
-- ST_Contains(window, geom): geom fully inside window
-- Finds objects whose MBR is contained by the search window
-- Expected: 1 (point_inside), 2 (on edge), 4 (small_inside), 5 (exact_match)
-- ----------------------------------------------------------------
SELECT id, label
FROM geo_contain
WHERE ST_Contains(
        ST_GeomFromText('POLYGON ((0 0, 0 10, 10 10, 10 0, 0 0))'),
        geom
      )
ORDER BY id;

-- ----------------------------------------------------------------
-- ST_Within(geom, window): same as ST_Contains(window, geom)
-- ----------------------------------------------------------------
SELECT id, label
FROM geo_contain
WHERE ST_Within(
        geom,
        ST_GeomFromText('POLYGON ((0 0, 0 10, 10 10, 10 0, 0 0))')
      )
ORDER BY id;

-- ----------------------------------------------------------------
-- ST_Contains(geom, window): geom fully contains the window
-- Finds objects that contain the search window
-- Expected: 8 (contains_query polygon contains the window)
-- ----------------------------------------------------------------
SELECT id, label
FROM geo_contain
WHERE ST_Contains(
        geom,
        ST_GeomFromText('POLYGON ((0 0, 0 10, 10 10, 10 0, 0 0))')
      )
ORDER BY id;

-- ----------------------------------------------------------------
-- Mixed: intersects vs contains comparison
-- Intersects: anything that touches the window
-- ----------------------------------------------------------------
SELECT 'intersects' AS mode, COUNT(*) AS cnt
FROM geo_contain
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((0 0, 0 10, 10 10, 10 0, 0 0))'))
UNION ALL
SELECT 'contained', COUNT(*)
FROM geo_contain
WHERE ST_Within(geom, ST_GeomFromText('POLYGON ((0 0, 0 10, 10 10, 10 0, 0 0))'));
-- Intersects should be larger than contained

DROP TABLE IF EXISTS geo_contain;
