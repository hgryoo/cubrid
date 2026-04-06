-- =============================================================
-- 01_rtree_ddl.sql
-- R-tree index DDL: CREATE / DROP index, column types
-- Covers: xrtree_add_index, xrtree_delete_index (rtree.c)
-- =============================================================

-- ----------------------------------------------------------------
-- Setup: create tables with geometry columns
-- ----------------------------------------------------------------
DROP TABLE IF EXISTS spatial_points;
DROP TABLE IF EXISTS spatial_polygons;
DROP TABLE IF EXISTS spatial_mixed;

CREATE TABLE spatial_points (
  id     INTEGER PRIMARY KEY,
  name   VARCHAR(64),
  geom   GEOMETRY
);

CREATE TABLE spatial_polygons (
  id     INTEGER PRIMARY KEY,
  name   VARCHAR(64),
  region GEOMETRY
);

CREATE TABLE spatial_mixed (
  id     INTEGER PRIMARY KEY,
  label  VARCHAR(64),
  shape  GEOMETRY,
  alt_shape GEOMETRY
);

-- ----------------------------------------------------------------
-- Create R-tree indexes
-- ----------------------------------------------------------------
-- Basic R-tree index
CREATE INDEX idx_points_geom    ON spatial_points   (geom)   USING RTREE;
CREATE INDEX idx_polygons_region ON spatial_polygons (region) USING RTREE;

-- R-tree index on first geometry column
CREATE INDEX idx_mixed_shape    ON spatial_mixed    (shape)  USING RTREE;

-- ----------------------------------------------------------------
-- Verify indexes exist (query catalog)
-- ----------------------------------------------------------------
SELECT class_name, index_name, index_type
FROM db_index
WHERE class_name IN ('spatial_points', 'spatial_polygons', 'spatial_mixed')
ORDER BY class_name, index_name;

-- ----------------------------------------------------------------
-- Drop and recreate (tests xrtree_delete_index path)
-- ----------------------------------------------------------------
DROP INDEX idx_points_geom ON spatial_points;

CREATE INDEX idx_points_geom ON spatial_points (geom) USING RTREE;

-- ----------------------------------------------------------------
-- Drop index via ALTER TABLE
-- ----------------------------------------------------------------
ALTER TABLE spatial_mixed DROP INDEX idx_mixed_shape;
ALTER TABLE spatial_mixed ADD INDEX idx_mixed_shape (shape) USING RTREE;

-- ----------------------------------------------------------------
-- Cleanup
-- ----------------------------------------------------------------
DROP TABLE IF EXISTS spatial_points;
DROP TABLE IF EXISTS spatial_polygons;
DROP TABLE IF EXISTS spatial_mixed;
