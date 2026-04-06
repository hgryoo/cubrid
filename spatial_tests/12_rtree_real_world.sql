-- =============================================================
-- 12_rtree_real_world.sql
-- Real-world usage patterns: GIS-style queries
-- Covers: range search, nearest-region queries, spatial joins,
--         multi-predicate queries (spatial + attribute filter)
-- =============================================================

DROP TABLE IF EXISTS cities;
DROP TABLE IF EXISTS districts;
DROP TABLE IF EXISTS roads;

-- ----------------------------------------------------------------
-- Setup: simplified geographic dataset (Korea-inspired coordinates)
-- Using approximate WGS84-like coordinates (lon/lat as x/y)
-- ----------------------------------------------------------------
CREATE TABLE cities (
  id       INTEGER PRIMARY KEY,
  name     VARCHAR(64),
  pop      INTEGER,
  location GEOMETRY  -- POINT (lon lat)
);

CREATE TABLE districts (
  id      INTEGER PRIMARY KEY,
  name    VARCHAR(64),
  region  GEOMETRY  -- POLYGON bounding box
);

CREATE TABLE roads (
  id    INTEGER PRIMARY KEY,
  name  VARCHAR(64),
  path  GEOMETRY  -- LINESTRING
);

CREATE INDEX idx_city_loc    ON cities    (location) USING RTREE;
CREATE INDEX idx_dist_region ON districts (region)   USING RTREE;
CREATE INDEX idx_road_path   ON roads     (path)     USING RTREE;

-- Cities (x=longitude, y=latitude approximate)
INSERT INTO cities VALUES (1,  'Seoul',    9800000, ST_GeomFromText('POINT (126.97 37.56)'));
INSERT INTO cities VALUES (2,  'Busan',    3400000, ST_GeomFromText('POINT (129.04 35.10)'));
INSERT INTO cities VALUES (3,  'Incheon',  3000000, ST_GeomFromText('POINT (126.70 37.45)'));
INSERT INTO cities VALUES (4,  'Daegu',    2400000, ST_GeomFromText('POINT (128.60 35.87)'));
INSERT INTO cities VALUES (5,  'Daejeon',  1500000, ST_GeomFromText('POINT (127.39 36.35)'));
INSERT INTO cities VALUES (6,  'Gwangju',  1500000, ST_GeomFromText('POINT (126.85 35.16)'));
INSERT INTO cities VALUES (7,  'Suwon',    1200000, ST_GeomFromText('POINT (127.02 37.27)'));
INSERT INTO cities VALUES (8,  'Ulsan',    1100000, ST_GeomFromText('POINT (129.32 35.54)'));
INSERT INTO cities VALUES (9,  'Changwon', 1000000, ST_GeomFromText('POINT (128.68 35.23)'));
INSERT INTO cities VALUES (10, 'Seongnam',  930000, ST_GeomFromText('POINT (127.13 37.44)'));

-- Districts / administrative regions
INSERT INTO districts VALUES (1, 'Capital_Region',
  ST_GeomFromText('POLYGON ((126.0 36.8, 126.0 38.3, 128.5 38.3, 128.5 36.8, 126.0 36.8))'));
INSERT INTO districts VALUES (2, 'Southeast_Region',
  ST_GeomFromText('POLYGON ((127.5 34.5, 127.5 36.5, 130.0 36.5, 130.0 34.5, 127.5 34.5))'));
INSERT INTO districts VALUES (3, 'Southwest_Region',
  ST_GeomFromText('POLYGON ((125.5 34.0, 125.5 36.5, 127.5 36.5, 127.5 34.0, 125.5 34.0))'));
INSERT INTO districts VALUES (4, 'All_Korea',
  ST_GeomFromText('POLYGON ((124.0 33.0, 124.0 39.0, 131.0 39.0, 131.0 33.0, 124.0 33.0))'));

-- Roads (simplified)
INSERT INTO roads VALUES (1, 'Expressway_1',   ST_GeomFromText('LINESTRING (126.97 37.56, 128.60 35.87)'));
INSERT INTO roads VALUES (2, 'Expressway_2',   ST_GeomFromText('LINESTRING (126.97 37.56, 127.39 36.35)'));
INSERT INTO roads VALUES (3, 'Coastal_Road',   ST_GeomFromText('LINESTRING (126.85 35.16, 128.68 35.23, 129.04 35.10, 129.32 35.54)'));

-- ----------------------------------------------------------------
-- Query 1: Cities in the Capital Region
-- ----------------------------------------------------------------
SELECT c.name, c.pop
FROM cities c
WHERE ST_Intersects(c.location, (SELECT region FROM districts WHERE id = 1))
ORDER BY c.pop DESC;
-- Expected: Seoul, Incheon, Suwon, Seongnam, Daejeon(maybe)

-- ----------------------------------------------------------------
-- Query 2: Cities in bounding box [126, 35] - [128, 37]
-- (central Korea approximate)
-- ----------------------------------------------------------------
SELECT name, pop
FROM cities
WHERE ST_Intersects(location, ST_GeomFromText('POLYGON ((126 35, 126 37, 128 37, 128 35, 126 35))'))
ORDER BY pop DESC;

-- ----------------------------------------------------------------
-- Query 3: Which district contains Seoul?
-- ----------------------------------------------------------------
SELECT d.name
FROM districts d
WHERE ST_Contains(d.region, (SELECT location FROM cities WHERE name = 'Seoul'))
ORDER BY d.name;
-- Expected: Capital_Region, All_Korea

-- ----------------------------------------------------------------
-- Query 4: Spatial + attribute filter (cities with pop > 2M in Southeast)
-- ----------------------------------------------------------------
SELECT c.name, c.pop
FROM cities c
WHERE ST_Intersects(c.location, (SELECT region FROM districts WHERE name = 'Southeast_Region'))
  AND c.pop > 2000000
ORDER BY c.pop DESC;
-- Expected: Busan (3.4M), Daegu (2.4M)

-- ----------------------------------------------------------------
-- Query 5: Roads passing through the Capital Region
-- ----------------------------------------------------------------
SELECT r.name
FROM roads r
WHERE ST_Intersects(r.path, (SELECT region FROM districts WHERE id = 1))
ORDER BY r.name;
-- Expected: Expressway_1, Expressway_2

-- ----------------------------------------------------------------
-- Query 6: Spatial self-join — which cities are within 2 degrees of Seoul?
-- (approximation: using bounding box ±2 degrees around Seoul)
-- ----------------------------------------------------------------
SELECT c2.name
FROM cities c1
JOIN cities c2 ON c1.id <> c2.id
WHERE c1.name = 'Seoul'
  AND ST_Intersects(c2.location,
        ST_GeomFromText('POLYGON ((124.97 35.56, 124.97 39.56, 128.97 39.56, 128.97 35.56, 124.97 35.56))'))
ORDER BY c2.name;
-- Expected: cities within 2-degree bounding box of Seoul (126.97, 37.56)

-- ----------------------------------------------------------------
-- Query 7: COUNT aggregation over spatial index scan
-- ----------------------------------------------------------------
SELECT COUNT(*) AS cities_in_korea
FROM cities
WHERE ST_Intersects(location, (SELECT region FROM districts WHERE name = 'All_Korea'));
-- Expected: 10 (all cities)

-- ----------------------------------------------------------------
-- Cleanup
-- ----------------------------------------------------------------
DROP TABLE IF EXISTS cities;
DROP TABLE IF EXISTS districts;
DROP TABLE IF EXISTS roads;
