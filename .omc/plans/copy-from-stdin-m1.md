# Implementation Plan: COPY FROM STDIN (M1 — Binary Format)

## Source Spec
`.omc/specs/deep-interview-copy-from-stdin.md`

## Requirements Summary

Implement a PostgreSQL-style `COPY table (columns) FROM STDIN WITH (FORMAT BINARY)` SQL statement for CUBRID. M1 scope: binary format only, COPY FROM direction only, working end-to-end through the CCI → broker/CAS → server path. Primary use case: high-performance vector/embedding data loading.

## RALPLAN-DR Summary

### Principles
1. **Minimal surface area**: Extend existing APIs (cci_execute) rather than creating parallel infrastructure
2. **Format extensibility**: Grammar and wire protocol must accommodate future CSV/LOADDB formats without redesign
3. **Reuse where sound**: Leverage loaddb's `locator_insert_force()` insertion pattern (`src/loaddb/load_server_loader.cpp:765`) for heap inserts, and generalize `session_set_load_session()`/`session_get_load_session()` infrastructure (`src/session/session.c:3232`) for copy session storage — but create a new binary decoder path since loaddb's text grammar is incompatible
4. **Atomic semantics**: COPY FROM participates in the caller's transaction — `finish()` finalizes the session but does NOT commit; the client decides when to commit. Any failure aborts the session and triggers rollback
5. **Broker transparency**: Data chunks flow through CAS like any other client-server message — no broker bypass

### Decision Drivers
1. **Performance**: Binary format must avoid text-to-float conversion for vector data (the primary motivation)
2. **Deployment compatibility**: Must work through broker/CAS (standard CUBRID deployment)
3. **Implementation scope**: M1 must be shippable independently; future formats (CSV, LOADDB) must not require M1 rework

### Viable Options

#### Option A: New dedicated COPY protocol path (Recommended)
- Add new `NET_SERVER_COPY_*` network request types and `CAS_FC_COPY_*` broker commands
- New server-side `copy_session` class manages the COPY lifecycle
- Binary decoder is a new module, independent of loaddb grammar
- Reuse `locator_insert_force()` insertion pattern and generalize session storage infrastructure
- **Pros**: Clean separation from loaddb; no risk of regressing existing loaddb behavior; format-extensible; reuses proven insertion and session patterns without coupling
- **Cons**: More new code (~1700 lines); must replicate some session plumbing

#### Option B: Extend loaddb protocol for COPY
- Reuse existing `NET_SERVER_LD_*` requests, add a "mode" flag to distinguish COPY vs loaddb
- Extend `cubload::batch` to carry binary content alongside text
- **Pros**: Less new network infrastructure; reuses tested transport; smaller diff
- **Cons**: Couples COPY and loaddb evolution; `cubload::batch::m_content` is `std::string` (text-oriented), adding binary would require breaking changes or awkward dual-mode; risk of loaddb regressions; mode-dispatch bugs could affect existing loaddb
- **Invalidation rationale**: The `cubload::batch` class is fundamentally text-oriented (`m_content` is `std::string`). Retrofitting binary support would either require a breaking API change affecting existing loaddb clients or a fragile dual-mode design. The user explicitly rejected this approach during the interview.
- **Steelman for B**: loaddb already solves session lifecycle, error recovery, batched transport, HA interaction, and heap insertion. A `std::vector<char> m_binary_content` alongside `m_content` would be modest. But the risk is subtle mode-dispatch bugs affecting existing loaddb — mitigable by testing but not eliminable.

**Synthesis (adopted)**: Use Option A's separate protocol and session management but reuse loaddb's `locator_insert_force()` insertion pattern AND generalize the `session_set_load_session`/`session_get_load_session` infrastructure (instead of duplicating it) to support both session types. This captures Option A's protocol separation while reducing plumbing overhead.

## Acceptance Criteria

- [ ] `COPY table (columns) FROM STDIN WITH (FORMAT BINARY)` parses without error in csql_grammar.y
- [ ] `COPY table TO STDOUT` parses but returns `ER_COPY_NOT_SUPPORTED` error at execution
- [ ] CCI: `cci_prepare` + `cci_execute` on COPY enters copy mode; `cci_copy_send_data(handle, data, len)` streams binary; `cci_copy_end(handle)` finalizes
- [ ] E2E: `CREATE TABLE items (id INT, embedding VECTOR(128))` → COPY binary rows → `SELECT count(*)` returns correct count
- [ ] Data integrity: `SELECT embedding FROM items WHERE id = 0` returns exact binary-sent values
- [ ] Atomic: type mismatch on row N rolls back ALL rows (including 1..N-1)
- [ ] Broker/CAS: works through standard CCI → broker → server path
- [ ] Performance: COPY BINARY for 100K 128-dim vectors is measurably faster than `cubrid loaddb` text format for the same data

## Implementation Steps

### Phase 1: Define Types and Protocol Constants

**Step 1.1: Add CUBRID_STMT_COPY statement type**
- File: `src/compat/dbtype_def.h` (before `CUBRID_MAX_STMT_TYPE` at line ~151)
  - Add `CUBRID_STMT_COPY` enum value to the `CUBRID_STMT_TYPE` enum
- File: `src/compat/dbi_compat.h` (after line ~128)
  - Add `#define SQLX_CMD_COPY CUBRID_STMT_COPY` alias
- File: `src/parser/parse_tree.h` (line ~1035, between `PT_RENAME_SYNONYM` and `PT_DIFFERENCE`)
  - Add `PT_COPY = CUBRID_STMT_COPY`
- File: `src/parser/parse_tree.h` (pt_statement_info union, ~line 3494-3570)
  - Add `PT_COPY_INFO` struct: `{ PT_NODE *table_name; PT_NODE *column_list; int direction; /* 0=FROM, 1=TO */ int format; /* 0=BINARY, 1=CSV, 2=LOADDB */ }`
  - Add `PT_COPY_INFO copy` member to the `pt_statement_info` union

**Step 1.2: Add network request types**
- File: `src/communication/network.h` (append before the terminator at end of macro, ~line 285)
  - Add `NET_SERVER_COPY_INIT`, `NET_SERVER_COPY_SEND_DATA`, `NET_SERVER_COPY_END`
  - **Note**: Append at end to avoid renumbering existing request IDs (network compatibility)

**Step 1.3: Register network handlers**
- File: `src/communication/network_sr.c` (`net_server_init()`, handler registration area ~line 696-717)
  - Register `scopy_from_init`, `scopy_from_send_data`, `scopy_from_end` for the new request types

**Step 1.4: Add CAS protocol commands**
- File: `src/broker/cas_protocol.h` (before `CAS_FC_MAX` at line ~223)
  - Add `CAS_FC_COPY_SEND_DATA`, `CAS_FC_COPY_END` (let enum auto-increment, don't hardcode values)
  - `CAS_FC_MAX` automatically becomes the next value

**Step 1.5: Define binary wire format header**
- New file: `src/loaddb/copy_binary_format.hpp`
  - Header structure: `{ uint32_t magic; uint16_t version; uint16_t num_columns; uint32_t type_oids[num_columns]; }`
  - Per-row: `{ uint16_t num_fields; (int32_t field_len, byte[] data)... }` — -1 field_len = NULL
  - Footer: `int16_t = -1` (sentinel)
  - Type OID mapping to `DB_TYPE` values (`DB_TYPE_INTEGER=5`, `DB_TYPE_FLOAT=2`, `DB_TYPE_VECTOR=41`, etc.)

### Phase 2: SQL Grammar and Parse Tree

**Step 2.1: Register keywords and declare grammar tokens**
- File: `src/parser/keyword.c`
  - Add `COPY` to the keyword table — **not currently a keyword**. Use non-reserved flag to prevent breaking schemas using `copy` as identifier.
  - Add `STDIN` to the keyword table — **not currently a keyword**
  - Add `STDOUT` to the keyword table — **not currently a keyword**
  - **Important**: `BINARY` is already a grammar token (`%token BINARY` at `csql_grammar.y:1133`). `FORMAT` exists ONLY as a function-name keyword in `keyword.c:693` with NO corresponding `%token` in `csql_grammar.y`. These are different categories:
    - `BINARY`: first-section keyword with `%token` → usable in grammar productions ✓
    - `FORMAT`: second-section function-name keyword with NO `%token` → NOT usable in grammar productions ✗
  - **Decision**: Add `FORMAT` as a new non-reserved keyword token. Add `%token FORMAT` in `csql_grammar.y` and upgrade `FORMAT` from function-name-only to a proper keyword with token mapping in `keyword.c`. Test bison generation for shift/reduce conflicts after adding.
  - **Alternative if FORMAT causes conflicts**: Use identifier matching — `copy_option: identifier BINARY` where the semantic action checks `strcmp(identifier_text, "format") == 0`. Less clean but avoids any grammar conflicts.

**Step 2.2: Add COPY grammar rules**
- File: `src/parser/csql_grammar.y`
  - Add `%token COPY STDIN STDOUT FORMAT` token declarations (BINARY already declared)
  - Add grammar productions:
    ```
    copy_stmt
      : COPY table_spec opt_column_list FROM STDIN copy_options
      | COPY table_spec TO STDOUT copy_options
      ;
    copy_options
      : /* empty */
      | WITH '(' copy_option_list ')'
      ;
    copy_option_list
      : copy_option
      | copy_option_list ',' copy_option
      ;
    copy_option
      : FORMAT BINARY
      | FORMAT CSV
      | FORMAT identifier  /* for future extensibility */
      ;
    ```
  - Semantic actions create `PT_COPY` node with `PT_COPY_INFO` populated
  - **Verification**: Run bison on the modified grammar to check for shift/reduce conflicts before proceeding

**Step 2.3: Add COPY to statement dispatch — BOTH paths**

COPY must be handled in BOTH `do_statement()` (secondary/csql path) AND `do_execute_statement()` (primary CCI path).

- File: `src/query/execute_statement.c`
  - **`do_statement()` (line 3099)**:
    - Fetch-version switch (~line 3165): Add `case PT_COPY:` to DIRTY_VERSION group
    - Execution switch (~line 3245): Add `case PT_COPY: error = do_copy(parser, statement); break;`
  - **`do_execute_statement()` (line 3818)** — THIS IS THE PRIMARY CCI EXECUTION PATH:
    - Fetch-version switch (~line 3882): Add `case PT_COPY:` to DIRTY_VERSION group
    - Execution switch (~line 3968): Add `case PT_COPY: err = do_copy(parser, statement); break;`
  - **`do_prepare_statement()` (line 3760)**: No changes needed — its `default` case returns `NO_ERROR` (line 3796), which is correct for COPY since no XASL generation is required. **This is a deliberate design decision**: COPY has no prepare phase, only an execute phase. Note: COPY statements may enter the prepared statement cache; on re-execute, the COPY protocol restarts (init → send_data → end). This is safe because the session is per-execution, not per-prepare.
- File: `src/query/execute_statement.h`
  - Declare `int do_copy(PARSER_CONTEXT *parser, PT_NODE *statement);`

**Step 2.4: Statement classification**
- File: `src/parser/parser_support.c` (`pt_is_ddl_statement()`, line ~1511)
  - **Do NOT add PT_COPY here.** COPY is DML (bulk insert), not DDL. Adding it would incorrectly trigger HA statement-based replication via `is_stmt_based_repl_type()` at `execute_statement.c:392`.
  - COPY's inserted rows will be replicated via WAL-based row replication (the standard path for DML in CUBRID HA), same as INSERT.

**Step 2.5: Implement do_copy() client-side logic**
- File: `src/query/execute_statement.c` (new function, ~60 lines)
  - Validate: direction FROM only for M1 (TO → `er_set(ER_COPY_NOT_SUPPORTED)`)
  - Validate: FORMAT BINARY only for M1 (CSV/LOADDB → `er_set(ER_COPY_NOT_SUPPORTED)`)
  - Resolve table name to class OID
  - Resolve column list to attribute descriptors (types, positions)
  - Call `copy_from_init()` (network interface) to create server-side session
  - Return statement handle in "COPY mode" state via a flag on the PT_NODE (`PT_COPY_INFO.copy_mode_active = true`). CCI's `cci_execute` checks the statement type (`CUBRID_STMT_COPY`) to detect COPY mode — no special return code needed.

**Step 2.6: Parse tree function array registrations (CRITICAL)**
- File: `src/parser/parse_tree_cl.c`
  - Every new PT_NODE type requires 3 function array registrations. Without these, `parser_walk_tree` on a COPY node will segfault (NULL function pointer dereference).
  - **`pt_apply_func_array[PT_COPY]`** (~line 5145 area): Implement `pt_apply_copy()` — tree walker that visits `PT_COPY_INFO.table_name` and `PT_COPY_INFO.column_list` children
  - **`pt_init_func_array[PT_COPY]`** (~line 5280 area): Register `pt_init_func_null_function` (no special initialization needed for COPY nodes)
  - **`pt_print_func_array[PT_COPY]`** (~line 5408 area): Implement `pt_print_copy()` — reconstructs `COPY table (col_list) FROM STDIN WITH (FORMAT BINARY)` SQL text from the parse tree
  - Reference: Follow the pattern of `PT_RENAME_SYNONYM` (the most recent PT type) at lines 5145, 5280, 5408

### Phase 3: Server-Side COPY Session

**Step 3.1: Generalize session storage**
- File: `src/session/session.h`
  - Add `session_set_copy_session()` / `session_get_copy_session()` declarations
  - Follow the existing `session_set_load_session()` pattern at `session.c:3232`
- File: `src/session/session.c`
  - Implement `session_set_copy_session()` / `session_get_copy_session()`
  - Store `copy_session` pointer in the per-connection session state (same mechanism loaddb uses)
  - **Concurrent COPY prevention**: `session_set_copy_session()` must check if a copy session already exists on this connection. If so, return `ER_COPY_SESSION_ERROR` ("COPY already in progress"). Only one COPY session per connection is allowed.
  - **Implementation detail**: Add `copy_session *copy_session_p` field to `struct session_state` at `session.c:141` (alongside `load_session_p` at line 140)
  - **Disconnect cleanup**: Add copy session cleanup in `session_stop_attached_threads()` at `session.c:3312`, following the `load_session_p` cleanup pattern at lines 3321-3327. This ensures a client disconnect mid-COPY properly cleans up the session and its SCANCACHE.

**Step 3.2: Create copy session class**
- New file: `src/loaddb/copy_session.hpp`
  - `class copy_session { OID class_oid; std::vector<DB_TYPE> col_types; int num_cols; int rows_loaded; HFID hfid; SCANCACHE *scan_cache; }`
  - Methods: `init(class_oid, col_types)`, `receive_data(char *data, int len)`, `finish()`, `abort()`
  - **Transaction ownership**: `copy_session` does NOT own the transaction. `finish()` finalizes the session state and returns the row count. `abort()` cleans up session state and sets error. The caller's transaction scope determines commit/rollback.
- New file: `src/loaddb/copy_session.cpp` (~200 lines)
  - `init()`: Resolve class OID → `HFID` via `heap_get_class_info()`. Initialize `SCANCACHE` via `heap_scancache_start_modify()`. Set up attribute descriptors.
  - `receive_data()`: Iterate over binary rows in the chunk using `decode_binary_row()`. For each decoded row: build `RECDES` from `DB_VALUE` array, call `locator_insert_force()` (pattern from `load_server_loader.cpp:765`). Increment `rows_loaded`.
  - `finish()`: Close `SCANCACHE` via `heap_scancache_end_modify()`. Return `rows_loaded`. Clean up session state.
  - `abort()`: Close `SCANCACHE`, clean up state, set error. Caller handles rollback.
  - **Chunk boundary handling**: Rows must NOT span chunks. Each `cci_copy_send_data()` call must contain complete rows only. The binary format's per-row length prefix allows the decoder to verify this and return an error if a row is truncated.

**Step 3.3: Binary row decoder**
- New file: `src/loaddb/copy_binary_decoder.hpp` (~20 lines)
  - Declare `int decode_binary_row(const char *buf, int len, const DB_TYPE *types, int ncols, DB_VALUE *out_vals, int *bytes_consumed);`
- New file: `src/loaddb/copy_binary_decoder.cpp` (~150 lines)
  - `decode_binary_row()`: Decodes one row from the binary buffer.
  - Per-field: read int32 length, then raw bytes → type-specific conversion:
    - `DB_TYPE_INTEGER`: 4 bytes, network byte order → `db_make_int()`
    - `DB_TYPE_FLOAT`: 4 bytes IEEE 754 → `db_make_float()`
    - `DB_TYPE_DOUBLE`: 8 bytes IEEE 754 → `db_make_double()`
    - `DB_TYPE_VARCHAR`: raw bytes → `db_make_varchar()`
    - `DB_TYPE_VECTOR`: 4-byte dim count + dim*4 bytes of float32 → `db_make_vector()`
    - NULL: field_len = -1 → `db_make_null()`
  - Sets `*bytes_consumed` so the caller can advance through the buffer for multi-row chunks
  - Returns `NO_ERROR` or error code (triggers session abort → full rollback)

**Step 3.4: Server-side network handlers**
- File: `src/communication/network_interface_sr.cpp` (after sloaddb functions, ~line 10940)
  - `scopy_from_init()`: Create `copy_session`, store via `session_set_copy_session()`, resolve class/columns, return session ID
  - `scopy_from_send_data()`: Retrieve `copy_session` via `session_get_copy_session()`, receive binary chunk, decode rows, insert into heap
  - `scopy_from_end()`: Finalize session, return row count + status

**Step 3.5: Client-side network functions**
- File: `src/communication/network_interface_cl.c` (after loaddb functions, ~line 10874)
  - `copy_from_init(OID *class_oid, DB_TYPE *col_types, int ncols)` → `NET_SERVER_COPY_INIT`
  - `copy_from_send_data(int session_id, char *data, int len)` → `NET_SERVER_COPY_SEND_DATA`
  - `copy_from_end(int session_id)` → `NET_SERVER_COPY_END`
  - **SA_MODE stubs**: Each function needs `#if defined(CS_MODE)` / `#else` branches. SA_MODE stubs should call the server-side implementation directly (same pattern as `loaddb_init` at `network_interface_cl.c:10725-10747` where SA_MODE returns `NO_ERROR` or calls the server function directly).

### Phase 4: Broker/CAS Forwarding

**Step 4.1: Add CAS COPY command handlers**
- File: `src/broker/cas_function.c`
  - `fn_copy_send_data()`: Receive binary chunk from CCI client, forward to server via `copy_from_send_data()`
  - `fn_copy_end()`: Forward finalization to server via `copy_from_end()`, return result to CCI client
- File: `src/broker/cas_function.h`
  - Declare `fn_copy_send_data`, `fn_copy_end`

**Step 4.2: Register CAS function handlers**
- File: `src/broker/cas.c`
  - Extend `server_fn_table` array (line ~75-120): Map `CAS_FC_COPY_SEND_DATA → fn_copy_send_data`, `CAS_FC_COPY_END → fn_copy_end`
  - Extend `server_func_name` array (line ~122): Add `"copy_send_data"`, `"copy_end"` name strings

**Step 4.3: CAS execute extension for COPY mode**
- File: `src/broker/cas_execute.c`
  - After `ux_execute()` detects `CUBRID_STMT_COPY`, set request handle to "COPY mode"
  - Return a response indicating COPY mode is active (CCI reads this to know data streaming is expected)
  - **Auto-commit suppression**: When CAS detects `CUBRID_STMT_COPY`, it MUST suppress auto-commit after execute. Standard CAS auto-commit logic (`do_commit_after_execute()` at `cas_execute.c:10317`) checks `has_stmt_result_set` — COPY returns false via `has_stmt_result_set()` at `cas_common_execute.c:457`, which would trigger an immediate commit BEFORE any data is sent. **Fix**: In `do_commit_after_execute()` at `cas_execute.c:10317`, add an early `return false` when `server_handle.q_result->stmt_type == CUBRID_STMT_COPY`. Auto-commit happens only after `fn_copy_end` completes.

**Step 4.4: Broker statistics (known limitation)**
- `get_stmt_type()` in `src/broker/cas_common_execute.c:131` does `strncasecmp` against known SQL verbs. COPY is not among them and would be classified as `CUBRID_MAX_STMT_TYPE`. `update_query_execution_count()` would not increment any counter. **M1 accepts this**: COPY operations won't appear in broker monitoring. Can be addressed in a follow-up by adding "COPY" to the verb table.

### Phase 5: CCI Driver Extensions

**Step 5.1: Add CCI copy functions**
- File: `cubrid-cci/src/cci/cas_cci.h` (after cci_execute declaration, ~line 837)
  - `extern int cci_copy_send_data(int req_handle, const char *data, int data_len, T_CCI_ERROR *err_buf);`
  - `extern int cci_copy_end(int req_handle, T_CCI_ERROR *err_buf);`
- File: `cubrid-cci/src/cci/cas_cci.c`
  - `cci_copy_send_data()`: Validate handle is in `COPY_MODE_ACTIVE`. Send `CAS_FC_COPY_SEND_DATA` message with binary data payload.
  - `cci_copy_end()`: Send `CAS_FC_COPY_END`, receive final row count/status. Clear `COPY_MODE_ACTIVE` flag.

**Step 5.2: Extend cci_execute for COPY mode detection**
- File: `cubrid-cci/src/cci/cas_cci.c`
  - After `cci_execute()` receives result, check if statement type is `CUBRID_STMT_COPY`
  - Set internal request handle state to `COPY_MODE_ACTIVE` — add a `bool copy_mode_active` field to the internal `T_REQ_HANDLE` structure (or equivalent CCI internal struct)
  - Return special result code indicating COPY mode (client calls `cci_copy_send_data` next)

### Phase 6: Error Code and Messages

**Step 6.1: Add error codes**
- File: `src/base/error_code.h`: Add `ER_COPY_NOT_SUPPORTED`, `ER_COPY_BINARY_FORMAT_ERROR`, `ER_COPY_SESSION_ERROR`
- File: `src/compat/dbi_compat.h`: Mirror error codes for client-visible use
- File: `msg/en_US.utf8/cubrid.msg`: Add English messages
- File: `msg/ko_KR.utf8/cubrid.msg`: Add Korean messages
- Update `ER_LAST_ERROR` constant in `error_code.h`

### Phase 7: Build System

**Step 7.1: Update CMakeLists.txt for new source files**
- File: `cubrid/CMakeLists.txt` (`LOADDB_SOURCES` / `LOADDB_HEADERS` lists, ~line 591-617)
  - Add `copy_binary_format.hpp`, `copy_session.hpp`, `copy_session.cpp`, `copy_binary_decoder.hpp`, `copy_binary_decoder.cpp`
- File: `unit_tests/loaddb/CMakeLists.txt`
  - Add `test_copy_binary_decoder.cpp` as a test source

### Phase 8: Testing and Benchmarking

**Step 8.1: Unit tests**
- File: `unit_tests/loaddb/test_copy_binary_decoder.cpp`
  - Test binary decoding for INT, FLOAT, DOUBLE, VARCHAR, VECTOR types
  - Test NULL handling (field_len = -1)
  - Test malformed data (truncated rows, wrong type sizes) → error
  - Test endianness correctness
  - Test multi-row chunk decoding (multiple rows in one buffer, verify bytes_consumed)

**Step 8.2: Integration test**
- C test program using CCI:
  1. `CREATE TABLE items (id INT, embedding VECTOR(128))`
  2. `cci_prepare(conn, "COPY items (id, embedding) FROM STDIN WITH (FORMAT BINARY)")`
  3. `cci_execute(handle)` → enters COPY mode
  4. Build binary buffer: header + N rows of (int4, 128-float vector)
  5. `cci_copy_send_data(handle, buffer, len)`
  6. `cci_copy_end(handle)` → returns row count
  7. `SELECT count(*) FROM items` → verify N
  8. `SELECT embedding FROM items WHERE id = 0` → verify exact vector values
  9. Test rollback: send malformed row, verify 0 rows committed
  10. Test concurrent COPY: attempt two COPY sessions on same connection → verify error

**Step 8.3: Performance benchmark**
- Script: Load 100K 128-dim vectors via `cubrid loaddb` (text format) vs COPY BINARY
- Measure: wall-clock time, server CPU, rows/second
- Report: speedup factor for COPY BINARY over loaddb text

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Binary wire format bugs (endianness, alignment) | Data corruption | Comprehensive unit tests for decoder; test on both little/big-endian if possible |
| CAS forwarding adds latency | Performance below expectations | Benchmark with/without broker to quantify overhead; CAS forwarding is simple passthrough |
| Grammar conflicts from FORMAT/COPY tokens | Build breaks | Make COPY non-reserved keyword; test bison generation after adding FORMAT token; fallback to identifier matching if conflicts arise |
| COPY as reserved keyword breaks existing schemas | Backward-incompatible | Use non-reserved (context-sensitive) keyword for COPY |
| HA replication incorrect | Data loss in HA setup | COPY is DML, NOT DDL — rows replicate via WAL (same as INSERT). Do NOT add to `pt_is_ddl_statement()` |
| Transaction ownership confusion | Partial commit or double-commit | copy_session does NOT own transaction; `finish()` finalizes session only, client commits |
| Auto-commit triggers before data sent | Empty COPY (commit before data) | Suppress auto-commit for `CUBRID_STMT_COPY` in CAS; auto-commit deferred to `fn_copy_end` |
| Concurrent COPY sessions on same connection | Session pointer overwritten | `session_set_copy_session()` checks for existing session; returns error if already active |
| Parse tree walk crashes on COPY node | Segfault | Register `pt_apply_copy`, `pt_init_copy`, `pt_print_copy` in `parse_tree_cl.c` function arrays |
| Loaddb regression from shared headers | Existing feature broken | Option A keeps COPY independent of loaddb; only share insertion pattern and session storage generalization |
| CCI submodule changes require coordinated release | Deployment complexity | CCI changes are backward-compatible additions; old CCI works without COPY |
| Old CCI sends CAS_FC_COPY to old server | Protocol mismatch | Old CCI doesn't have COPY functions so can't send them. New CCI to old server: CAS_FC_COPY_* index exceeds `server_fn_table` bounds — add bounds check in CAS dispatch if not already present |

## Verification Steps

1. Build: `./build.sh -m debug -c "-DUNIT_TESTS=ON"` succeeds with no new warnings
2. Bison: `csql_grammar.y` compiles without shift/reduce conflicts from COPY/FORMAT tokens
3. Grammar: `COPY items (id, embedding) FROM STDIN WITH (FORMAT BINARY)` parses in csql
4. Grammar: `COPY items TO STDOUT` parses but returns error at execution
5. Parse tree: `parser_walk_tree` on a COPY node does not crash (pt_apply_copy registered)
6. Unit tests: `test_copy_binary_decoder` passes all cases
7. Integration: CCI test program loads 1000 rows, verifies count and values
8. Rollback: CCI test with bad row verifies 0 rows committed
9. Broker: Integration test runs through broker (not direct connection)
10. Auto-commit: Verify COPY works in auto-commit mode (CAS defers commit to copy_end)
11. Perf: Benchmark shows measurable speedup over loaddb text for 100K vectors
12. HA: Verify rows inserted via COPY appear on replica via WAL replication

## File Change Summary

| File | Change Type | Lines (est.) |
|------|-------------|-------------|
| `src/compat/dbtype_def.h` | Modify | +3 |
| `src/compat/dbi_compat.h` | Modify | +5 |
| `src/parser/parse_tree.h` | Modify | +25 |
| `src/parser/keyword.c` | Modify | +15 |
| `src/parser/csql_grammar.y` | Modify | +80 |
| `src/parser/parse_tree_cl.c` | Modify | +50 |
| `src/query/execute_statement.c` | Modify | +80 |
| `src/query/execute_statement.h` | Modify | +2 |
| `src/parser/parser_support.c` | Verify only | 0 (confirm PT_COPY NOT added to DDL check) |
| `src/communication/network.h` | Modify | +5 |
| `src/communication/network_sr.c` | Modify | +10 |
| `src/communication/network_interface_sr.cpp` | Modify | +120 |
| `src/communication/network_interface_cl.c` | Modify | +100 |
| `src/session/session.h` | Modify | +5 |
| `src/session/session.c` | Modify | +35 |
| `src/loaddb/copy_binary_format.hpp` | **New** | ~60 |
| `src/loaddb/copy_session.hpp` | **New** | ~80 |
| `src/loaddb/copy_session.cpp` | **New** | ~200 |
| `src/loaddb/copy_binary_decoder.hpp` | **New** | ~20 |
| `src/loaddb/copy_binary_decoder.cpp` | **New** | ~150 |
| `src/broker/cas_protocol.h` | Modify | +3 |
| `src/broker/cas_function.c` | Modify | +60 |
| `src/broker/cas_function.h` | Modify | +3 |
| `src/broker/cas.c` | Modify | +6 |
| `src/broker/cas_execute.c` | Modify | +20 |
| `src/broker/cas_common_execute.c` | Note | 0 (known limitation: COPY not in broker stats) |
| `cubrid-cci/src/cci/cas_cci.h` | Modify | +5 |
| `cubrid-cci/src/cci/cas_cci.c` | Modify | +80 |
| `src/base/error_code.h` | Modify | +5 |
| `msg/en_US.utf8/cubrid.msg` | Modify | +5 |
| `msg/ko_KR.utf8/cubrid.msg` | Modify | +5 |
| `cubrid/CMakeLists.txt` | Modify | +5 |
| `unit_tests/loaddb/CMakeLists.txt` | Modify | +3 |
| `unit_tests/loaddb/test_copy_binary_decoder.cpp` | **New** | ~150 |
| **Total** | **34 files (6 new)** | **~1700** |

---

## ADR

**Decision**: Implement COPY FROM STDIN with a new dedicated protocol path (Option A), with targeted reuse of loaddb patterns (synthesis approach).

**Drivers**: Binary format performance for vector data; programmatic CCI API access; broker compatibility; no regression risk to existing loaddb.

**Alternatives considered**: Extend loaddb protocol (Option B) — rejected because `cubload::batch::m_content` is text-oriented (`std::string`) and retrofitting binary would couple COPY and loaddb evolution. Steelman: loaddb already solves session lifecycle, HA interaction, and heap insertion, but the mode-dispatch risk and coupling cost outweigh the code savings.

**Why chosen**: Clean separation preserves loaddb stability, enables independent format evolution, and aligns with user's explicit rejection of cubload reuse for binary. Synthesis path reduces plumbing overhead by reusing `locator_insert_force()` insertion pattern and generalizing `session_set_load_session()` infrastructure.

**Consequences**: ~1700 lines across 34 files (6 new); COPY and loaddb share insertion pattern and session storage but no protocol/batch coupling; future formats (CSV/LOADDB) plug into the same COPY protocol. Known limitation: COPY not tracked in broker monitoring stats (M1 accepted).

**Follow-ups**: M2 (CSV format), M3 (LOADDB format via COPY), M4 (COPY TO direction), broker stats for COPY.

## Review Changelog

### Architect Review (Round 1)
- Fixed `CUBRID_STMT_COPY` location: `dbtype_def.h` (not `dbi_compat.h`), with `SQLX_CMD_COPY` alias in `dbi_compat.h`
- Added `do_execute_statement()` as PRIMARY CCI path (was missing entirely)
- Added `keyword.c` registration for COPY, STDIN, STDOUT
- Added `session.c`/`session.h` for copy session storage (generalized from loaddb pattern)
- Added `network_sr.c` handler registration
- Added `server_func_name` array entries in `cas.c`
- Clarified transaction ownership: copy_session does NOT own transaction
- Added HA replication note: COPY is DML, not DDL — uses WAL row replication
- Changed COPY to non-reserved keyword to prevent backward-compatibility breaks
- Fixed NET_SERVER_COPY_* insertion point: append at end (not after loaddb entries)
- Removed hardcoded CAS_FC enum values (let auto-increment)
- Added `PT_COPY_INFO` to `pt_statement_info` union location reference
- Updated estimate from ~1200 lines / 23 files to ~1600 lines / 29 files
- Added steelman for Option B and synthesis path in RALPLAN-DR

### Critic Review (Round 1)
- **MAJOR**: Added Step 2.6 — `parse_tree_cl.c` function array registrations (`pt_apply`, `pt_init`, `pt_print`) to prevent segfaults on COPY parse tree operations
- **MAJOR**: Fixed FORMAT token issue — FORMAT is NOT a grammar token despite being in `keyword.c`. Added explicit step to declare `%token FORMAT` in grammar with fallback to identifier matching if conflicts arise
- Added `copy_binary_decoder.hpp` header file (was missing — decoder function needs declaration)
- Added `copy_session.cpp` implementation details (init/receive_data/finish/abort with SCANCACHE, HFID setup)
- Added SA_MODE stubs requirement for `network_interface_cl.c` functions
- Added auto-commit suppression in CAS execute (Step 4.3) — prevents premature commit before data is sent
- Added concurrent COPY prevention in `session_set_copy_session()`
- Added chunk boundary handling constraint: rows must not span chunks
- Added CMakeLists.txt updates for new files (Phase 7)
- Added broker statistics known limitation note (Step 4.4)
- Added protocol version safety note for old CCI → new server
- Clarified COPY mode detection mechanism (flag on T_REQ_HANDLE, not PT_NODE)
- Added `bytes_consumed` output parameter to `decode_binary_row()` for multi-row chunks
- Updated estimate from ~1600 lines / 29 files to ~1700 lines / 34 files

### Architect Review (Round 2) — APPROVED
- Auto-commit suppression: specified exact location (`do_commit_after_execute()` at `cas_execute.c:10317`, check `stmt_type == CUBRID_STMT_COPY`)
- Session storage: explicitly added `copy_session_p` field to `struct session_state` at `session.c:141`
- Disconnect cleanup: added `session_stop_attached_threads()` at `session.c:3312` as cleanup location, following `load_session_p` pattern at lines 3321-3327
- Confirmed CAS dispatch bounds check already safe (`func_code >= CAS_FC_MAX` at `cas.c:1030`)

### Critic Review (Round 2) — APPROVED
- Verified both MAJOR round-1 fixes are genuine and correct (parse_tree_cl.c, FORMAT token)
- Verified all round-1 minor fixes addressed
- Accepted Architect round-2 items as valid implementation refinements
- Plan ready for execution
