-- =============================================================
-- 06_rtree_update.sql
-- R-tree DML: UPDATE path
-- Covers: locator_sr.c UPDATE dispatch = rtree_delete old MBR +
--         rtree_insert new MBR (two operations, same transaction)
-- =============================================================

DROP TABLE IF EXISTS geo_update;

CREATE TABLE geo_update (
  id    INTEGER PRIMARY KEY,
  label VARCHAR(64),
  geom  GEOMETRY
);

CREATE INDEX idx_update_geom ON geo_update (geom) USING RTREE;

INSERT INTO geo_update VALUES (1, 'obj_a', ST_GeomFromText('POINT (0 0)'));
INSERT INTO geo_update VALUES (2, 'obj_b', ST_GeomFromText('POINT (10 10)'));
INSERT INTO geo_update VALUES (3, 'obj_c', ST_GeomFromText('POLYGON ((0 0, 0 5, 5 5, 5 0, 0 0))'));
INSERT INTO geo_update VALUES (4, 'obj_d', ST_GeomFromText('POINT (50 50)'));
INSERT INTO geo_update VALUES (5, 'obj_e', ST_GeomFromText('LINESTRING (0 0, 20 20)'));

-- ----------------------------------------------------------------
-- Update: move a point (delete old MBR, insert new MBR in index)
-- ----------------------------------------------------------------
UPDATE geo_update SET geom = ST_GeomFromText('POINT (100 100)') WHERE id = 1;

-- Verify old position no longer found
SELECT COUNT(*) AS old_pos_gone
FROM geo_update
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((-1 -1, -1 1, 1 1, 1 -1, -1 -1))'));
-- Expected: 0

-- Verify new position found
SELECT COUNT(*) AS new_pos_found
FROM geo_update
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((99 99, 99 101, 101 101, 101 99, 99 99))'));
-- Expected: 1

-- ----------------------------------------------------------------
-- Update: change polygon to a different area
-- ----------------------------------------------------------------
UPDATE geo_update SET geom = ST_GeomFromText('POLYGON ((80 80, 80 90, 90 90, 90 80, 80 80))') WHERE id = 3;

SELECT COUNT(*) AS old_area_gone
FROM geo_update
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((0 0, 0 6, 6 6, 6 0, 0 0))'))
  AND id = 3;
-- Expected: 0

SELECT COUNT(*) AS new_area_found
FROM geo_update
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((79 79, 79 91, 91 91, 91 79, 79 79))'))
  AND id = 3;
-- Expected: 1

-- ----------------------------------------------------------------
-- Update: set geometry to NULL (should remove from R-tree index)
-- ----------------------------------------------------------------
UPDATE geo_update SET geom = NULL WHERE id = 4;

SELECT COUNT(*) AS null_not_indexed
FROM geo_update
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((0 0, 0 200, 200 200, 200 0, 0 0))'));
-- Expected: 3 (ids 2, 3, 5 -- not 1 which is at 100,100 inside window, actually 4 items)
-- Note: id=1 is at (100,100) which IS inside [0,200] window
-- So expected: 4 rows visible, minus NULL row = 4 (ids 1, 2, 3_new_pos_outside_window, 5)

-- ----------------------------------------------------------------
-- Update: restore from NULL to a geometry
-- ----------------------------------------------------------------
UPDATE geo_update SET geom = ST_GeomFromText('POINT (60 60)') WHERE id = 4;

SELECT id, label
FROM geo_update
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((55 55, 55 65, 65 65, 65 55, 55 55))'))
ORDER BY id;
-- Expected: id=4

-- ----------------------------------------------------------------
-- Batch update: shift all geometries by 1000 units (stress test)
-- ----------------------------------------------------------------
UPDATE geo_update SET geom = ST_GeomFromText('POINT (1001 1001)') WHERE id = 2;
UPDATE geo_update SET geom = ST_GeomFromText('POINT (1002 1002)') WHERE id = 4;

SELECT COUNT(*) AS in_high_zone
FROM geo_update
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((1000 1000, 1000 1010, 1010 1010, 1010 1000, 1000 1000))'));
-- Expected: 2

DROP TABLE IF EXISTS geo_update;
