# Deep Interview Spec: COPY FROM STDIN Statement

## Metadata
- Interview ID: di-copy-stmt-20260415
- Rounds: 10
- Final Ambiguity Score: 18%
- Type: brownfield
- Generated: 2026-04-15
- Threshold: 20%
- Status: PASSED

## Clarity Breakdown
| Dimension | Score | Weight | Weighted |
|-----------|-------|--------|----------|
| Goal Clarity | 0.90 | 0.35 | 0.315 |
| Constraint Clarity | 0.75 | 0.25 | 0.188 |
| Success Criteria | 0.75 | 0.25 | 0.188 |
| Context Clarity | 0.85 | 0.15 | 0.128 |
| **Total Clarity** | | | **0.818** |
| **Ambiguity** | | | **0.182** |

## Goal

Implement a PostgreSQL-style `COPY table (columns) FROM STDIN WITH (FORMAT ...)` SQL statement for CUBRID that enables client applications to stream data over the SQL protocol through the broker/CAS layer to the server for high-performance bulk data loading. The primary motivation is programmatic API access (not possible with the current CLI-only `cubrid loaddb`) and binary format support to eliminate text parsing overhead when loading vector/embedding data.

### Milestone Breakdown

**M1 (this spec):** Binary vector load via CCI
- `COPY table (columns) FROM STDIN WITH (FORMAT BINARY)` end-to-end
- Custom length-prefixed binary wire format
- CCI API: extend `cci_execute` + new `cci_copy_send_data`
- Works through broker/CAS layer
- Atomic error handling (rollback all on failure)

**M2 (future):** CSV format support
**M3 (future):** LOADDB object-file format support via COPY
**M4 (future):** COPY TO STDOUT (export direction)

## Constraints

- **Error handling:** Atomic — any row failure (type mismatch, constraint violation, malformed data) rolls back the entire COPY operation
- **Wire format:** Custom simple format — length-prefixed binary values with a minimal header (NOT PostgreSQL's binary COPY wire protocol)
- **Deployment path:** Must work through broker/CAS layer (standard CUBRID deployment), not direct-only
- **CCI API design:** Extend existing `cci_prepare`/`cci_execute` flow with a new `cci_copy_send_data(handle, data, len)` function for streaming binary chunks after execute. Minimal API surface change.
- **Direction:** COPY FROM only in M1. Grammar should reserve COPY TO syntax for future implementation.
- **Format:** BINARY only in M1. Grammar should support `WITH (FORMAT ...)` clause to allow CSV/LOADDB in future.
- **SQL grammar:** Add COPY statement to `src/parser/csql_grammar.y` following PostgreSQL syntax patterns

## Non-Goals

- COPY TO (export) — deferred to M4
- CSV format — deferred to M2
- LOADDB object-file format via COPY — deferred to M3
- PostgreSQL binary wire protocol compatibility — using custom simple format instead
- New dedicated CCI copy functions (cci_copy_start, etc.) — extending cci_execute instead
- Direct CCI-to-server bypass of broker — not needed, must go through broker/CAS
- Skip-bad-rows / ON_ERROR option — M1 is atomic only
- Server reading from local files (COPY FROM '/path') — only STDIN mode in M1

## Acceptance Criteria

- [ ] SQL syntax `COPY table (columns) FROM STDIN WITH (FORMAT BINARY)` is parsed by `csql_grammar.y` without errors
- [ ] CCI driver: `cci_prepare` + `cci_execute` on a COPY statement enters COPY mode; `cci_copy_send_data(handle, binary_data, len)` sends binary chunks; `cci_copy_end(handle)` finalizes
- [ ] End-to-end test: `CREATE TABLE items (id INT, embedding VECTOR(128))`, then COPY binary int4+vector rows via CCI, then `SELECT count(*)` returns correct row count
- [ ] Data integrity: `SELECT embedding FROM items WHERE id = 0` returns the exact vector values that were sent in binary
- [ ] Atomic rollback: if any row fails (e.g., type mismatch, constraint violation), the entire COPY operation is rolled back and no partial data is committed
- [ ] Broker/CAS: COPY works through the standard CCI → broker/CAS → server path, not just direct connections
- [ ] Performance benchmark: COPY BINARY vector loading is measurably faster than `cubrid loaddb` text-based vector loading for the same dataset (e.g., 100K+ 128-dim vectors)
- [ ] COPY TO syntax is reserved in the grammar (parse without execution, returning "not yet supported" error)

## Assumptions Exposed & Resolved

| Assumption | Challenge | Resolution |
|------------|-----------|------------|
| "Server-side STDIN" means server reads from its own stdin | Asked: what does "server-side" mean in the data flow? | PostgreSQL model: client streams data over SQL protocol; "server-side" means server does parsing/insertion |
| COPY needs all formats in M1 | Simplifier: what's the simplest valuable version? | Binary vector load via CCI only for M1; CSV and LOADDB deferred |
| cubload::batch packing can be reused for binary | Asked about wire format options | User correctly identified cubload batch carries text (`m_content` is `std::string`); chose custom simple format instead |
| loaddb is adequate for vector loading | Contrarian: what's specifically slow about loaddb? | Two bottlenecks: (1) CLI-only, no programmatic API; (2) text parsing overhead for float vectors |
| Could bypass broker for M1 simplicity | Asked about broker/CAS requirement | Must work through broker — standard production deployment path |
| Could use new dedicated CCI functions | Asked about CCI API design | Extend existing cci_execute flow instead, minimizing API surface change |

## Technical Context

### Existing Infrastructure (Brownfield)

**Loaddb utility** (`src/loaddb/`):
- `load_grammar.yy` / `load_lexer.l` — custom object-file format parser (text-based, not CSV/binary)
- `load_common.hpp` — `cubload::batch` with `m_content` (text string), pack/unpack serialization
- `load_session.hpp` — server-side session with `load_file()` and `load_batch()` modes, thread pool
- `load_server_loader.cpp` — object insertion logic (reusable for COPY server-side insertion)
- `load_worker_manager.hpp` — multi-threaded worker pool (potentially reusable)

**Network protocol** (`src/communication/`):
- `network.h` — 7 existing loaddb request types: `NET_SERVER_LD_INIT`, `LD_INSTALL_CLASS`, `LD_LOAD_BATCH`, `LD_FETCH_STATUS`, `LD_DESTROY`, `LD_INTERRUPT`, `LD_UPDATE_STATS`
- `network_interface_cl.c` — client-side: `loaddb_init()`, `loaddb_load_batch()`, etc.
- `network_interface_sr.cpp` — server-side: `sloaddb_init()`, `sloaddb_load_batch()`, etc.

**SQL parser** (`src/parser/`):
- `csql_grammar.y` — 26,460-line bison grammar; no COPY syntax exists; INSERT, PREPARE/EXECUTE patterns exist as reference

**CCI driver** (`cubrid-cci/` submodule):
- C library with `cci_prepare()`, `cci_execute()`, `cci_next_result()` functions
- Needs new `cci_copy_send_data()` and `cci_copy_end()` functions

**Broker/CAS** (`src/broker/`):
- Proxy layer between CCI clients and server
- Must forward COPY data chunks — likely needs new CAS command handling for COPY streaming

### New Components Needed

1. **SQL grammar**: COPY statement in `csql_grammar.y` producing a new `PT_COPY` node type
2. **Binary format parser**: Server-side decoder for custom length-prefixed binary format
3. **CCI driver extensions**: `cci_copy_send_data()`, `cci_copy_end()` functions
4. **CAS COPY forwarding**: Broker/CAS code to proxy COPY data chunks between CCI and server
5. **Server COPY handler**: Execution path from parsed COPY statement to data insertion (may leverage loaddb session/worker infrastructure)
6. **Network requests**: New `NET_SERVER_COPY_*` request types (or reuse/extend loaddb ones)

## Ontology (Key Entities)

| Entity | Type | Fields | Relationships |
|--------|------|--------|---------------|
| COPY Statement | core domain | direction(FROM), column list, WITH clause, FORMAT option | operates on Table, receives Client Data Stream, specifies Format |
| Table | core domain | name, columns | target of COPY FROM |
| Client Data Stream | supporting | stdin on client side, binary chunks | sent via CCI Driver API through SQL Protocol |
| SQL Protocol | supporting | request/response, streaming mode | transports data from CCI through Broker to Server |
| Format | core domain | BINARY (M1), CSV (M2), LOADDB (M3) | determines server-side parser/decoder |
| Vector/Embedding | core domain | dimension, float array, int4 ID | primary use case data type for BINARY format |
| Batch | existing infra | id, class_id, content, rows | existing loaddb transport unit (text-only) |
| CCI Driver API | supporting | cci_execute, cci_copy_send_data, cci_copy_end | client-side entry point for COPY |
| Transaction | supporting | atomic rollback on error | wraps entire COPY operation |
| Binary Wire Format | core domain | length-prefixed values, minimal header, custom | protocol contract between CCI and server |
| Performance Benchmark | supporting | loaddb-text vs COPY-binary, vector dataset | validates "better performance" goal |

## Ontology Convergence

| Round | Entity Count | New | Changed | Stable | Stability Ratio |
|-------|-------------|-----|---------|--------|----------------|
| 1 | 4 | 4 | - | - | N/A |
| 2 | 5 | 1 | 1 | 3 | 80% |
| 3 | 7 | 2 | 0 | 5 | 71% |
| 4 | 8 | 1 | 0 | 7 | 88% |
| 5 | 9 | 1 | 0 | 8 | 89% |
| 6 | 9 | 0 | 0 | 9 | 100% |
| 7 | 9 | 0 | 0 | 9 | 100% |
| 8 | 10 | 1 | 0 | 9 | 90% |
| 9 | 11 | 1 | 0 | 10 | 91% |
| 10 | 11 | 0 | 0 | 11 | 100% |

Ontology fully converged by round 10. Core entities (COPY Statement, Table, Format, Binary Wire Format) stabilized early. Supporting entities (CCI Driver API, Transaction, Performance Benchmark) were added incrementally as constraints clarified.

## Interview Transcript
<details>
<summary>Full Q&A (10 rounds)</summary>

### Round 1
**Q:** For this first version, which direction(s) does the COPY statement need to support? (COPY FROM only, Both FROM and TO, or COPY FROM first TO later)
**A:** COPY FROM first, TO later — implement FROM (loading) now with grammar that reserves TO syntax for a future phase.
**Ambiguity:** 78% (Goal: 0.35, Constraints: 0.10, Criteria: 0.10, Context: 0.30)

### Round 2
**Q:** When you say 'STDIN on server-side', which data flow do you mean? (PostgreSQL model, Server reads local file, or Both)
**A:** PostgreSQL model — client reads from its local stdin/file and streams data over the SQL protocol to the server.
**Ambiguity:** 69% (Goal: 0.55, Constraints: 0.15, Criteria: 0.10, Context: 0.40)

### Round 3
**Q:** What data format should COPY FROM accept? (CSV only, CSV + TEXT, CSV + loaddb format, or CSV with custom delimiters)
**A:** Want to support like PostgreSQL with FORMAT BINARY for vector data, plus CSV and loaddb format. Showed Python psycopg3 example: `cur.copy("COPY items (id, embedding) FROM STDIN WITH (FORMAT BINARY)")` with `copy.set_types(["int4", "vector"])` and `copy.write_row()`.
**Ambiguity:** 52% (Goal: 0.75, Constraints: 0.40, Criteria: 0.20, Context: 0.45)

### Round 4 (Contrarian Mode)
**Q:** What specifically is slow or inadequate about the current loaddb approach that COPY must fix?
**A:** Both API + binary — need programmatic access via SQL protocol AND binary format to eliminate text parsing overhead for vector data.
**Ambiguity:** 46% (Goal: 0.80, Constraints: 0.40, Criteria: 0.35, Context: 0.50)

### Round 5
**Q:** What should happen when a row fails to load? (Atomic rollback, Skip bad rows, Atomic first skip later, or Configurable)
**A:** Atomic (rollback all) — any row failure rolls back the entire COPY operation.
**Ambiguity:** 41% (Goal: 0.80, Constraints: 0.55, Criteria: 0.35, Context: 0.55)

### Round 6 (Simplifier Mode)
**Q:** If you could only ship ONE scenario end-to-end for the first milestone, which would it be?
**A:** Binary vector load via CCI — COPY items (id, embedding) FROM STDIN WITH (FORMAT BINARY) end-to-end via CCI driver.
**Ambiguity:** 34% (Goal: 0.85, Constraints: 0.55, Criteria: 0.55, Context: 0.55)

### Round 7
**Q:** For the CCI driver's COPY support, which API approach? (New CCI copy functions, Extend cci_execute, or PG-compatible protocol)
**A:** Extend cci_execute — COPY is a regular SQL statement via cci_prepare/cci_execute, then new cci_copy_send_data sends binary chunks.
**Ambiguity:** 32% (Goal: 0.85, Constraints: 0.55, Criteria: 0.55, Context: 0.70)

### Round 8
**Q:** For the binary wire format, which approach? (PostgreSQL-compatible, Custom simple format, or Reuse cubload packing)
**A:** Custom simple format — correctly identified that cubload::batch m_content is text-only (std::string), so not suitable for binary. Chose length-prefixed binary values with minimal header.
**Ambiguity:** 28% (Goal: 0.85, Constraints: 0.70, Criteria: 0.55, Context: 0.75)

### Round 9
**Q:** Is the end-to-end test scenario (CREATE TABLE, COPY binary vectors, verify data) the right acceptance test?
**A:** Performance testing is required — need to compare between loaddb format (text-based vector) and the new COPY statement.
**Ambiguity:** 21% (Goal: 0.90, Constraints: 0.70, Criteria: 0.75, Context: 0.75)

### Round 10
**Q:** Should COPY work through the broker/CAS layer for M1, or direct CCI-to-server only?
**A:** Through broker (required) — must work through standard CUBRID deployment path.
**Ambiguity:** 18% (Goal: 0.90, Constraints: 0.75, Criteria: 0.75, Context: 0.85)

</details>
