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

Batch execution is available through [`run_examples.sh`](/home/hgryoo/dev/cubrid_spatial/spatial_examples/run_examples.sh).

Example:

```bash
cd spatial_examples
./run_examples.sh demodb -u dba
```

The script writes per-example stdout/stderr logs and a summary report under `spatial_examples/results/`.
