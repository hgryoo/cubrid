-- =============================================================
-- 08_rtree_transaction.sql
-- WAL / transaction correctness
-- Covers: log_sysop_start/commit in rtree_insert/rtree_delete,
--         RVRT_* recovery functions, COMMIT / ROLLBACK semantics
-- =============================================================

DROP TABLE IF EXISTS geo_txn;

CREATE TABLE geo_txn (
  id   INTEGER PRIMARY KEY,
  geom GEOMETRY
);

CREATE INDEX idx_txn_geom ON geo_txn (geom) USING RTREE;

-- ----------------------------------------------------------------
-- Test 1: COMMIT — data must be visible after commit
-- ----------------------------------------------------------------
BEGIN;
INSERT INTO geo_txn VALUES (1, ST_GeomFromText('POINT (1 1)'));
INSERT INTO geo_txn VALUES (2, ST_GeomFromText('POINT (2 2)'));
COMMIT;

SELECT COUNT(*) AS committed_rows FROM geo_txn;
-- Expected: 2

-- ----------------------------------------------------------------
-- Test 2: ROLLBACK insert — data must NOT be visible after rollback
-- ----------------------------------------------------------------
BEGIN;
INSERT INTO geo_txn VALUES (10, ST_GeomFromText('POINT (10 10)'));
INSERT INTO geo_txn VALUES (11, ST_GeomFromText('POINT (11 11)'));
ROLLBACK;

SELECT COUNT(*) AS after_rollback FROM geo_txn;
-- Expected: still 2 (rows 10, 11 were rolled back)

SELECT COUNT(*) AS rolled_back_invisible
FROM geo_txn
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((9 9, 9 12, 12 12, 12 9, 9 9))'));
-- Expected: 0

-- ----------------------------------------------------------------
-- Test 3: ROLLBACK delete — deleted rows must reappear after rollback
-- ----------------------------------------------------------------
BEGIN;
DELETE FROM geo_txn WHERE id = 1;
SELECT COUNT(*) AS during_delete FROM geo_txn;
-- Expected: 1 (visible within txn)
ROLLBACK;

SELECT COUNT(*) AS after_rollback_delete FROM geo_txn;
-- Expected: 2 (row 1 restored)

SELECT COUNT(*) AS row1_back
FROM geo_txn
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((0 0, 0 2, 2 2, 2 0, 0 0))'));
-- Expected: 1 (id=1 at point(1,1))

-- ----------------------------------------------------------------
-- Test 4: ROLLBACK update — old geometry must be visible after rollback
-- ----------------------------------------------------------------
BEGIN;
UPDATE geo_txn SET geom = ST_GeomFromText('POINT (999 999)') WHERE id = 1;

SELECT COUNT(*) AS during_update_new
FROM geo_txn
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((998 998, 998 1000, 1000 1000, 1000 998, 998 998))'));
-- Expected: 1 (new position visible inside txn)

ROLLBACK;

SELECT COUNT(*) AS old_pos_restored
FROM geo_txn
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((0 0, 0 2, 2 2, 2 0, 0 0))'));
-- Expected: 1 (original position restored)

SELECT COUNT(*) AS new_pos_gone
FROM geo_txn
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((998 998, 998 1000, 1000 1000, 1000 998, 998 998))'));
-- Expected: 0

-- ----------------------------------------------------------------
-- Test 5: Nested operations + COMMIT
-- ----------------------------------------------------------------
BEGIN;
INSERT INTO geo_txn VALUES (20, ST_GeomFromText('POINT (20 20)'));
UPDATE geo_txn SET geom = ST_GeomFromText('POINT (100 100)') WHERE id = 2;
DELETE FROM geo_txn WHERE id = 1;
COMMIT;

SELECT id FROM geo_txn ORDER BY id;
-- Expected: ids 2 (at 100,100), 20

SELECT COUNT(*) AS at_new_pos
FROM geo_txn
WHERE ST_Intersects(geom, ST_GeomFromText('POLYGON ((99 99, 99 101, 101 101, 101 99, 99 99))'));
-- Expected: 1 (id=2)

DROP TABLE IF EXISTS geo_txn;
