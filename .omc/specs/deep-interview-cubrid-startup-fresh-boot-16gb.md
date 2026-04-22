---
name: CUBRID fresh-boot startup <10s with 16GB buffer on 1-core Docker
description: Reduce cub_server startup time from 35s to <10s for fresh-DB first boot in a 1-core Docker container, given a fixed 16GB data_buffer_size, bind-mount storage, and without relying on THP.
type: project
---

# Deep Interview Spec: CUBRID startup <10s (fresh boot, 16GB buffer, 1 core, no THP)

## Metadata
- Interview ID: deep-interview-cubrid-startup-fresh-boot-16gb
- Rounds: 6
- Final Ambiguity Score: 9.5%
- Type: brownfield
- Generated: 2026-04-18
- Threshold: 20%
- Status: PASSED

## Clarity Breakdown
| Dimension | Score | Weight | Weighted |
|-----------|-------|--------|----------|
| Goal Clarity | 0.95 | 0.35 | 0.333 |
| Constraint Clarity | 0.90 | 0.25 | 0.225 |
| Success Criteria | 0.85 | 0.25 | 0.213 |
| Context Clarity | 0.90 | 0.15 | 0.135 |
| **Total Clarity** | | | **0.905** |
| **Ambiguity** | | | **9.5%** |

## Goal
Reduce wall-clock time of CUBRID `cub_server` startup — measured from process launch to the "ready" log line — from **~35 seconds to <10 seconds**, for the *fresh/empty DB first boot* scenario inside a **Docker container pinned to 1 CPU core** with **`data_buffer_size = 16GB`** (fixed, non-negotiable) and bind-mount/Docker-volume storage (not overlayfs). All optimization must be code-side; config-side shortcuts (reducing buffer size, removing features) are out of scope.

## Constraints
- **Scenario is fixed-fresh-DB first boot.** No log recovery redo, no DWB recovery. Eliminates `log_initialize()` redo and `dwb_load_and_recover_pages()` recovery work as bottleneck candidates.
- **1 CPU core.** Parallelism across cores is unavailable. Only deferral, elimination, or lazy-evaluation of work can help; multithreaded optimizations won't.
- **`data_buffer_size = 16GB` is fixed.** At 16KB page size, that is ~1,048,576 buffer control blocks and 16GB of iopage memory. Any phase whose cost scales with buffer count is in scope.
- **Storage is bind mount / Docker volume**, not overlayfs. First-write I/O penalty is NOT the bottleneck; code-internal CPU/memory work is.
- **Recent optimizations already merged on current branch and present in the measured binary:**
  - `68e6821fd` log buffer lazy init — skip memset of pages_area in log buffer
  - `ddadbd51d` page buffer pool idempotent check + `madvise(MADV_HUGEPAGE)` on iopage_table *(the madvise portion was reverted by `780f61f82`; only the idempotent check remains)*
  - `780f61f82` revert mmap, THP — removed the `madvise(MADV_HUGEPAGE)` call entirely
  - `19e542c5b` lazy initialization of iopage fields (prv, prv2)
  - `f3fc182f1` little-endian optimized improvements
- **THP (Transparent Huge Pages) is explicitly rejected as a solution path.** Not because THP is merely "unavailable" in the container, but because the user has observed **runtime performance degradation** when THP is active for the database buffer pool (typical causes: khugepaged compaction latency, tail-latency spikes on write-heavy workloads, NUMA imbalance). The `madvise(MADV_HUGEPAGE)` that was added in commit `ddadbd51d` has **already been reverted** by commit `780f61f82 "revert mmap, THP"` — no THP code currently exists in `src/storage/page_buffer.c` (grep confirms zero matches). The startup plan must therefore achieve <10s on 4KB pages, with the ~4M page-fault baseline (16GB / 4KB) treated as a real cost to mitigate by other means (deferral, first-access lazy init, smaller structures, avoiding O(N) walks), NOT by reintroducing huge pages.
- **No per-phase timing data exists yet.** 35s is a single total. Any plan must start with instrumentation; speculating where the time goes without measurement is out of scope.
- **Measurement boundary:** `cub_server` process launch → "ready"/"SERVER STARTED" log line. Excludes Docker runtime, entrypoint.sh, broker startup, and network readiness. Scope is purely inside `boot_restart_server()` at `src/transaction/boot_sr.c:1969`.

## Non-Goals
- Reducing `data_buffer_size` or any other configuration parameter.
- Optimizing crash-recovery, populated-DB-restart, or SA_MODE standalone boot — different scenarios, different dominant phases.
- Docker image, entrypoint script, broker, or network-readiness optimizations.
- Relying on THP / huge pages in any form. THP is rejected by the user due to **runtime performance degradation** on the buffer pool — not merely because it's unavailable. Any optimization that requires huge pages, `MADV_HUGEPAGE`, `MAP_HUGETLB`, or hugetlbfs backing is out of scope. Note: the `madvise(MADV_HUGEPAGE)` that existed briefly in ddadbd51d has already been removed by commit `780f61f82 "revert mmap, THP"` — no THP hint currently lives in `src/storage/page_buffer.c`. Reintroducing it in any form is out of scope.
- Multithreaded / multi-core speedups (user has 1 core).
- Touching already-optimized code paths (`68e6821fd`, `ddadbd51d`, `19e542c5b`) unless profiling proves they still dominate.
- Splitting large source files or other structural refactors unrelated to boot time.
- Modifying `src/heaplayers/lea_heap.c` (3rd-party).

## Acceptance Criteria
- [ ] Per-phase boot timing instrumentation added to `boot_restart_server()` in `src/transaction/boot_sr.c` using existing TSC infrastructure (`tsc_init()` already at boot_sr.c:2209; `perf_monitor.h` already included at boot_sr.c:62). Each phase from Phase 1 (config/language init) through Phase 10 (query-engine init) has a start/end checkpoint. Instrumentation overhead must be <1% of total boot time and gated behind a build flag or config parameter so it can be disabled in release.
- [ ] Baseline per-phase breakdown of the 35s is produced, in the 1-core Docker container with 16GB `data_buffer_size`, bind-mount storage, fresh DB. Output is a table showing phase name → seconds → % of total. This data drives the rest of the work.
- [ ] Top N phases (where cumulative time > 70% of total) are identified and each gets a targeted optimization. "Targeted" means: the change is justified by pointing at a specific O(buffer_pool_size) or O(large_alloc) loop/structure and either (a) eliminating the work, (b) deferring it beyond the "ready" log line, or (c) making it amortized (lazy first-access).
- [ ] Final measured wall time from `cub_server` launch to the "ready" log line is **<10 seconds**, on the same fresh-DB / 1-core / 16GB-buffer / bind-mount configuration.
- [ ] All existing boot correctness preserved: server accepts connections, catalog is queryable, `csql -u dba <dbname> <<<"SELECT 1"` succeeds immediately after the "ready" line, `pgbuf_daemons` and `dwb_daemons` are running, vacuum is initialized. If any work was deferred past the "ready" line, there is a documented mechanism (background completion, first-access fault-in) that ensures correctness under the next query.
- [ ] No regression on the three other startup scenarios: clean restart of populated DB, crash recovery, SA_MODE boot. Measured on representative fixtures.
- [ ] No `pragma once`, no `free()` (use `free_and_init`), `memory_wrapper.hpp` remains last include. No C++ exceptions in engine code. No splitting of large files.

## Assumptions Exposed & Resolved
| Assumption | Challenge | Resolution |
|------------|-----------|------------|
| "Startup" means a single thing | What scenario? | Fresh/empty DB first boot, not crash recovery / populated restart / Docker cold start. |
| 35s includes Docker and broker overhead | Where does the clock start and stop? | `cub_server` launch → "ready" log line. Docker/broker/network are excluded. |
| We should start optimizing | Are recent commits already in the measured binary? | Yes — `68e6821fd` and `ddadbd51d` are both present. Easy wins exhausted; need per-phase data. |
| The problem is inside boot code | What if the config is wrong for this container? (Contrarian) | Partially validated: `data_buffer_size = 16GB` on a 1-core container is extreme but NOT negotiable. Plan must absorb it, not reduce it. |
| ddadbd51d already prevents ~4M page faults via THP | Is THP actually active? | No. THP is not considered effective in this environment. The `madvise` is advisory-only and silently ignored. Page-fault cost when the 16GB iopage buffer is first touched is still at non-THP levels. |
| We know where the time goes | Do we have per-phase timing? | No. Only a single 35s total. Instrumentation is a hard prerequisite — first deliverable of the plan. |
| Buffer size can be reduced for small containers | Is 16GB fixed? | Yes, 16GB is a requirement. Must optimize code to boot it fast, not shrink config. |

## Technical Context
**Entry points and phase map (from `explore` agent, cited file:line):**

- `src/executables/server.c:276` — `main()` → `net_server_start(database_name)`
- `src/transaction/boot_sr.c:1969` — `boot_restart_server()` — scope root for this work

Phases in order (all live inside `boot_restart_server`):

| # | Phase | Where (boot_sr.c) | Relevance for fresh-DB / 16GB |
|---|-------|-------------------|-------------------------------|
| 1 | Config & language init: `lang_init`, `tz_load`, `msgcat_init`, `sysprm_load_and_init`, `pl_server_init` | 2195–2248 | Constant-cost regardless of DB/buffer; needs instrumenting to rule out. |
| 2 | Volume discovery, `log_get_io_page_size`, read database.txt, mount log volume | 2255–2289 | Small for fresh DB. |
| 3 | `logtb_define_trantable()` → transaction descriptors + **page buffer pool allocation** | 2269 | **Prime suspect.** Allocates 16GB iopage + per-BCB structures. `pgbuf_initialize` at `src/storage/page_buffer.c:1520` runs here. Commit ddadbd51d's THP hint is at `page_buffer.c:5395` but ineffective. |
| 4 | `boot_mount`, `boot_get_db_parm`, `heap_cache_class_info` | 2305–2340 | Catalog probe; may touch buffer pages. |
| 5 | Multi-volume mount, `disk_manager_init`, `catalog_initialize` | 2360–2386 | Catalog init allocates structures; may iterate buffer. |
| 6 | `vacuum_initialize`, `dwb_load_and_recover_pages` | 2388–2408 | DWB sized with buffer pool; still runs for fresh DB. |
| 7 | `pgbuf_daemons_init`, `dwb_daemons_init`, parallel query worker init | 2410–2416 | **Prime suspect.** Daemons may iterate/seed buffer-sized structures. |
| 8 | `log_initialize()` → recovery analysis + redo | 2423 | Trivial for fresh DB (no redo). Should be fast — verify. |
| 9 | `boot_after_copydb`, flush daemons, `vacuum_boot` | 2425–2446 | Mostly fresh-DB trivial; verify. |
| 10 | Query engine: `locator_initialize`, `xcache_initialize`, `qmgr_initialize`, query/function caches | 2455–2493 | **Prime suspect.** Cache structures may be sized proportionally. |

**Leading hypothesis (to be confirmed by instrumentation):**
The dominant cost is O(`data_buffer_size`) work paid when *some* phase first walks the 16GB iopage buffer or per-BCB control blocks, generating ~4M 4KB page faults plus per-BCB initialization on a single CPU. THP would have collapsed that to ~8K faults, but THP is not effective here, so the full cost is paid. Candidate culprits in priority order:
1. `pgbuf_initialize_bcb_table` loop iterating ~1M BCBs (page_buffer.c:~5400) — even with THP hint, the per-BCB initialization is O(N) on one core.
2. `pgbuf_daemons_init` and related daemon setup that may scan the buffer pool (boot_sr.c:2410–2416).
3. DWB sizing / seeding in `dwb_load_and_recover_pages` even for fresh DB if DWB is proportional to buffer pool.
4. Catalog/locator/xcache/qmgr init if any allocate structures proportional to buffer pool or iterate all volumes.

**Instrumentation plan:**
Use existing `tsc_init()` at `boot_sr.c:2209` and `tsc_getticks`/`tsc_elapsed` from `perf_monitor`. Add a compact `boot_perf_checkpoint(const char *phase)` helper that records (phase_name, tsc_ticks) into a small array; dump the cumulative table at the "ready" log line. Gate behind `#if !defined(NDEBUG)` or a new sysparam so release builds are unaffected.

## Ontology (Key Entities)
| Entity | Type | Fields | Relationships |
|--------|------|--------|---------------|
| CUBRID server | core domain | cub_server binary, SERVER_MODE | hosts `boot_restart_server()` |
| `boot_restart_server()` | core domain | file: boot_sr.c:1969, 10 phases | runs all startup phases |
| Fresh DB first boot scenario | core domain | no redo, no DWB recovery | selects which phases dominate |
| Docker 1-core environment | core constraint | 1 CPU, limited memory | forbids multicore speedups |
| Ready log line | measurement boundary | emitted at end of boot_restart_server | stop-clock for the 35s metric |
| 10s target | success threshold | hard cutoff, ~3.5× speedup | pass/fail of acceptance criteria |
| `data_buffer_size = 16GB` | fixed config | ~1M BCBs, 16GB iopage memory | drives O(N) work in init phases |
| Bind-mount storage | environment | non-overlayfs | rules out first-write I/O penalty |
| Prior optimizations (already applied) | historical context | 68e6821fd, ddadbd51d, 19e542c5b, f3fc182f1 | baseline binary; easy wins exhausted |
| Per-phase instrumentation | deliverable | TSC checkpoints per phase | first work item, precondition for all others |
| TSC checkpoint timing | supporting | tsc_init already at boot_sr.c:2209 | mechanism for instrumentation |
| THP (rejected) | hard constraint | user rejects due to runtime perf degradation on buffer pool | forbids any huge-page-based mitigation; page-fault cost must be addressed via deferral/lazy-init/smaller-structures |
| O(buffer_pool_size) work hypothesis | working hypothesis | to be confirmed by data | shapes optimization strategy toward lazy/deferred work |

## Ontology Convergence
| Round | Entity Count | New | Changed | Stable | Stability |
|-------|--------------|-----|---------|--------|-----------|
| 1 | 4 | 4 | 0 | 0 | N/A |
| 2 | 6 | 2 | 1 | 3 | 67% |
| 3 | 7 | 1 | 0 | 6 | 86% |
| 4 | 9 | 2 | 0 | 7 | 78% |
| 5 | 12 | 3 | 0 | 9 | 75% |
| 6 | 13 | 1 | 0 | 12 | 92% |

Stability dipped slightly at rounds 4–5 because the Contrarian-mode question legitimately expanded the scope (revealing the 16GB buffer and THP-ineffectiveness facts). It reconverged in round 6 once the "16GB fixed" decision locked the plan shape.

## Interview Transcript
<details>
<summary>Full Q&A (6 rounds)</summary>

### Round 1 — Goal Clarity
**Q:** Which startup scenario is the one taking 35 seconds?
**A:** Fresh/empty DB first boot.
**Ambiguity:** 62% (Goal: 0.55, Constraints: 0.25, Criteria: 0.10, Context: 0.65)

### Round 2 — Success Criteria / Goal Boundary
**Q:** How is the 35 seconds measured, and what's the target?
**A:** cub_server start → "ready" log line, target <10s.
**Ambiguity:** 37% (Goal: 0.80, Constraints: 0.25, Criteria: 0.75, Context: 0.70)

### Round 3 — Constraints (prior opts)
**Q:** Are recent startup-perf commits (68e6821fd, ddadbd51d) active in the measured binary?
**A:** Both already in, 35s is after.
**Ambiguity:** 30% (Goal: 0.80, Constraints: 0.50, Criteria: 0.75, Context: 0.75)

### Round 4 — Contrarian (config vs code)
**Q:** What if 35s is a config/environment problem, not boot code? What's `data_buffer_size` and storage driver?
**A:** `data_buffer_size = 16GB`, Docker volume / bind mount.
**Ambiguity:** 21% (Goal: 0.85, Constraints: 0.70, Criteria: 0.80, Context: 0.80)

### Round 5 — Constraints (decision on 16GB)
**Q:** Is reducing `data_buffer_size` an acceptable path, or must 16GB be preserved?
**A:** 16GB is fixed — optimize code to boot it fast.
**Ambiguity:** 14% (Goal: 0.90, Constraints: 0.85, Criteria: 0.80, Context: 0.85)

### Round 6 — User correction (post-answer)
**Q:** (Implicit) Does THP mitigate the page-fault cost from `madvise(MADV_HUGEPAGE)` in ddadbd51d?
**A:** User correction #1: "THP is not considered."
**A:** User correction #2: "i dont want to THP considering because of runtime perf degration" → THP is not merely unavailable in the environment; it is **rejected as a solution path** because the user has observed runtime performance degradation when THP is active for the buffer pool. Non-Goals updated to forbid any THP / huge-page reliance.
**Ambiguity:** 9.5% (Goal: 0.95, Constraints: 0.90, Criteria: 0.85, Context: 0.90)

</details>
