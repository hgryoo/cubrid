Spatial SQL examples for the currently wired GIS path.

The examples below are intentionally limited to functions that now execute through GEOS:

- `ST_GeomFromText`
- `ST_AsText`
- `ST_SRID`
- `ST_SetSRID`
- `ST_GeometryType`
- `ST_X`
- `ST_Y`
- `ST_Area`
- `ST_Length`
- `ST_Distance`
- `ST_Contains`
- `ST_Intersects`
- `ST_Envelope`

Each `.sql` file can be executed independently from `csql`.

Current example groups:

- `00_types.sql`: type declarations and subtype-preserving casts
- `01_io.sql`: WKT roundtrip, subtype names, SRID metadata, envelope output
- `02_measurements.sql`: point accessors, distance, length, area
- `03_predicates.sql`: contains/intersects checks across geometry and geography
- `04_nulls_and_metadata.sql`: NULL propagation and metadata retention
- `05_table_roundtrip.sql`: table DDL/DML with geometry/geography columns

Batch execution is available through [`run_examples.sh`](/home/hgryoo/dev/cubrid_spatial/spatial_examples/run_examples.sh).

Example:

```bash
cd spatial_examples
./run_examples.sh demodb -u dba
```

The script writes per-example stdout/stderr logs and a summary report under `spatial_examples/results/`.
