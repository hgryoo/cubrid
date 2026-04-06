# R-tree Spatial Index Test Suite

SQL test files for the CUBRID R-tree spatial index implementation.

## Test Files

| File | Phase | Description |
|------|-------|-------------|
| `01_rtree_ddl.sql` | Phase 1/2 | CREATE / DROP R-tree index, catalog verification |
| `02_rtree_insert.sql` | Phase 2/3 | INSERT path: choose_leaf, append_leaf_entry, WAL RVRT_LEAF_INSERT |
| `03_rtree_delete.sql` | Phase 2/3 | DELETE path: find_and_delete, condense_tree, D4 shorten, WAL RVRT_LEAF_DELETE |
| `04_rtree_search_intersects.sql` | Phase 2/4 | Search INTERSECTS mode: DFS scan, scan_open_rtree_scan |
| `05_rtree_search_contains.sql` | Phase 2/4 | Search CONTAINS / CONTAINED modes: ST_Contains, ST_Within |
| `06_rtree_update.sql` | Phase 2/3 | UPDATE path: delete old MBR + insert new MBR in same transaction |
| `07_rtree_split.sql` | Phase 1/3 | Node split: quadratic split algorithm, adjust_tree, grow_tree |
| `08_rtree_transaction.sql` | Phase 3 | WAL/transaction: COMMIT, ROLLBACK insert/delete/update |
| `09_rtree_mbr_bridge.sql` | Phase 2 | MBR extraction: db_spatial_get_mbr() for Point/Line/Polygon/Multi/Collection |
| `10_rtree_stats.sql` | Phase 4 | Statistics: rtree_get_stats(), UPDATE STATISTICS, catalog query |
| `11_rtree_edge_cases.sql` | All | Edge cases: NULL, empty table, duplicate MBR, large/small coords |
| `12_rtree_real_world.sql` | All | Real-world GIS queries: spatial joins, attribute+spatial filters |

## Running Tests

```bash
# Connect to CUBRID and run a test file
csql -u dba -p "" demodb -i spatial_tests/01_rtree_ddl.sql

# Run all tests in sequence
for f in spatial_tests/*.sql; do
    echo "=== Running $f ==="
    csql -u dba -p "" demodb -i "$f"
done
```

## Implementation Coverage

### Phase 1 — Algorithm (rtree.c)
- MBR computation: `rtree_mbr_area`, `rtree_mbr_union`, `rtree_mbr_enlargement`
- Quadratic split: `rtree_pick_seeds`, `rtree_pick_next`, `rtree_distribute_entries`
- Insert: `rtree_choose_leaf`, `rtree_adjust_tree`, `rtree_grow_tree`
- Search: `rtree_search_page` (DFS, all three modes)
- Delete: `rtree_find_and_delete`, condense_tree, shorten_tree

### Phase 2 — Storage & DML Integration
- MBR bridge: `db_spatial_get_mbr()` in `db_geometry.cpp`
- DML dispatch: `locator_sr.c` RTREE_INDEX case
- Scan manager: `scan_open_rtree_scan`, `scan_next_rtree_scan`, `scan_close_rtree_scan`
- `S_RTREE_SCAN` integrated into all switch dispatch sites

### Phase 3 — WAL Logging
- Recovery enum: `RVRT_NEW_PAGE` (130) … `RVRT_ROOT_HEADER_UPD` (137)
- Log calls on: page init, leaf insert/delete, non-leaf insert/update/delete, headers
- `rtree_insert` / `rtree_delete` wrapped in `log_sysop_start/commit`
- 12 `rtree_rv_*` recovery functions

### Phase 4 — Query Optimizer Integration
- `ACCESS_METHOD_INDEX_RTREE` enum value
- `RTREE_SPEC_INFO` struct in `access_spec.hpp`
- `rtree_specptr` field in `access_spec_node`
- Dispatch in `qexec_execute_scan()` → `scan_open_rtree_scan()`
- `rtree_get_stats()` for optimizer cardinality estimates
