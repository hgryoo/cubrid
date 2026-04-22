# Consensus Plan — CUBRID Fresh-Boot <10s (16GB buffer, 1 core, no THP)

## Provenance
- **Source spec:** `.omc/specs/deep-interview-cubrid-startup-fresh-boot-16gb.md` (deep-interview; 6 rounds; final ambiguity 9.5%)
- **Ralplan consensus:** 2 iterations; Pass 2 reached Architect APPROVE + Critic APPROVE_WITH_RESERVATIONS
- **Agents:** oh-my-claudecode:planner (opus), oh-my-claudecode:architect (opus), oh-my-claudecode:critic (opus)
- **Repo:** `/home/hgryoo/dev/cubrid_dev`, branch `cubvec/m1-f4-cp`

## Hard Invariants (violation invalidates the plan)
1. `data_buffer_size = 16GB` is FIXED — no reduction.
2. THP / huge pages REJECTED — user observed runtime perf degradation; commit `780f61f82` already reverted `madvise(MADV_HUGEPAGE)`; do NOT reintroduce in any form.
3. 1 CPU core — no multicore speedups; only deferral / lazy-init / smaller-structures.
4. Scenario is fresh-DB first boot only. Other scenarios are regression-gated, not optimized.
5. Instrumentation must precede optimization. No Step 3+ code change merges until Step 2 produces baseline.
6. CUBRID coding rules: `free_and_init` (never bare `free`), `memory_wrapper.hpp` last include with XXX comment, `#ifndef _FILENAME_H_` guards (never `#pragma once`), no C++ exceptions in engine code, 2-space indent / 120-col / GNU braces, C files use `/* */` comments only.

## Target
`cub_server` launch → "ready"/"SERVER STARTED" log line:
- **Acceptance:** median < **10.0s** AND all 10 runs < **12.0s** on the acceptance rig.
- Current baseline: 35s total (no per-phase data).

## RALPLAN-DR Summary

### Principles
1. Instrumentation before optimization.
2. Fixed-buffer invariant: 16GB inviolable.
3. No THP / huge pages.
4. Fresh-DB first-boot only (other scenarios regression-gated).
5. Release builds pay zero for instrumentation (`#if !defined(NDEBUG)`).
6. Single-core discipline: deferred work on background cubthreads must not contend on boot-critical core during the <10s window.
7. Deferred ≠ dropped: every first-use site of a deferred subsystem blocks on a waitable CV primitive — no observational "probably ready in 500ms" gates.

### Decision Drivers
1. Evidence of dominator (Step 2 decides Steps 3/4 or HALT).
2. Correctness envelope of laziness (BCB scan audit + CV primitives).
3. Fresh-boot coverage only — no degradation on populated/crash/SA_MODE beyond ±5%.

### Viable Options
- **A (chosen)**: daemon deferral + cache-init deferral + lazy BCB init.
- **B**: right-size N-scaled structures only — insufficient alone; used as Step 5 complement.
- **C (fallback trigger)**: long-tail broad attack — refused; triggers HALT + ralplan redesign.
- **D (standing alternative)**: iopage first-touch attack (D-1 `MAP_POPULATE`, D-2 background fault-prewarmer, D-3 kernel default). Gated on Step 2 data; re-enter ralplan with D as first-class if **iopage first-touch median > 30% of post-Step-4.1 remaining boot time** (Reservation #1).

---

## Code Anchors

| Anchor | Role |
|---|---|
| `src/transaction/boot_sr.c:1969` | `boot_restart_server` entry — scope root |
| `src/transaction/boot_sr.c:62` | existing `perf_monitor.h` include |
| `src/transaction/boot_sr.c:2209` | existing `tsc_init()` |
| `src/transaction/boot_sr.c:2216` | `cubthread::initialize` — background worker safe to spawn after this point |
| `src/transaction/boot_sr.c:1637` | SA_MODE boot path reference |
| `src/transaction/boot_sr.c:2269` | Phase 3 `logtb_define_trantable` → `pgbuf_initialize` |
| `src/transaction/boot_sr.c:2410–2416` | **Step 3a target**: `pgbuf_daemons_init`, `dwb_daemons_init`, parallel_query worker_manager init |
| `src/transaction/boot_sr.c:2423` | `log_initialize()` (trivial for fresh DB; verify via instrumentation) |
| `src/transaction/boot_sr.c:2468–2492` | **Step 3b target**: `xcache_initialize`, `qmgr_initialize`, `qfile_initialize_list_cache`, `fpcache_initialize` |
| `src/storage/page_buffer.c:1520` | `pgbuf_initialize` entry (already idempotent since ddadbd51d) |
| `src/storage/page_buffer.c:5383` | 16GB `malloc(iopage_table)` — NOT modified in Step 4 (see Step 4.2 iopage risk note) |
| `src/storage/page_buffer.c:5395–5446` | **Step 4.1 target**: O(~1M) BCB init loop |
| `src/storage/page_buffer.c:16612–16615` | `pgbuf_daemons_init` entry (spawn-only, no buffer walk) |

Verified with grep (2026-04-18): zero `madvise` / `MADV_HUGEPAGE` matches in `src/storage/page_buffer.c` — commit `780f61f82` revert is fully effective. Pass-1 plan's "remove madvise" sub-bullet is therefore a no-op and is deleted from this Pass-2 plan.

---

## Step 1 — Per-phase TSC checkpoint infrastructure (release-zero-cost)

**Files:**
- **New:** `src/base/boot_perf_trace.h` (`_BOOT_PERF_TRACE_H_` guard; `memory_wrapper.hpp` last).
- `src/transaction/boot_sr.c` — insert `BOOT_PHASE_BEGIN(name)` / `BOOT_PHASE_END(name)` around each phase in `boot_restart_server` (~lines 2195, 2255, 2269, 2292, 2305, 2360, 2386, 2388, 2403, 2410, 2423, 2425, 2441, 2455, 2468, 2475, 2481, 2487).
- `src/storage/page_buffer.c` — wrap `pgbuf_initialize`, `pgbuf_initialize_bcb_table`, BCB loop.
- `src/base/system_parameter.{c,h}` — register `PRM_ID_BOOT_PERF_TRACE` (bool, default false).

**Gating:**
```c
#if !defined(NDEBUG)
  #define BOOT_PHASE_BEGIN(name)  boot_phase_begin_(name)
  #define BOOT_PHASE_END(name)    boot_phase_end_(name)
#else
  #define BOOT_PHASE_BEGIN(name)  ((void) 0)
  #define BOOT_PHASE_END(name)    ((void) 0)
#endif
```
Inside debug, runtime-gated on `prm_get_bool_value(PRM_ID_BOOT_PERF_TRACE)`.

**Reservation #3 directive:** In addition to the debug-only TSC trace, add a minimal 5-phase release-safe trace — one `er_log_debug` per major phase (Phase 1/3/6/7/10), no TSC read, low verbosity, suppressed in SA_MODE utility invocations. Keeps production customer-reported regressions diagnosable.

**Verification:**
- Debug + flag on: all ~18 checkpoints print with μs precision; dump table emitted before "ready" log line.
- Release: `nm cub_server | grep boot_phase_` → empty (or inlined no-op); 5-run wall-clock delta vs. trace-disabled <1%.
- No `#pragma once`; no bare `free`; no exceptions.

**Effort:** S

---

## Step 2 — Baseline + 3-way decision gate

**Methodology (fixed):**
- `data_buffer_size = 16GB`, SERVER_MODE, fresh DB re-created per run.
- 1-core cgroup (`cpuset.cpus=0`), bind-mount storage, cold page cache between runs: `sync && echo 3 | sudo tee /proc/sys/vm/drop_caches`.
- **N = 10** boots. Report **median** and **p95** per phase.
- Debug build with `PRM_ID_BOOT_PERF_TRACE=true`; release smoke run to confirm total-boot parity.

**Output:** `.omc/plans/baseline-phase-timing.md` — {phase, median_ms, p95_ms, %_of_total} sorted desc, variance per phase, raw log excerpts.

**Decision matrix:**
| Case | Condition | Action |
|---|---|---|
| (a) single-phase dominator | any phase ≥ 40% | Execute Step 3 (one relevant sub-step) + Step 4 targeting that phase |
| (b) multi-phase dominators | 2–3 phases each 15–40% | Execute Steps 3a + 3b + 4 in parallel |
| (c) long-tail | ≥5 phases each 5–15%, none ≥ 20% | **HALT**. Write `.omc/plans/ralplan-redesign-trigger.md`; do NOT execute Steps 3/4; re-invoke ralplan. |

**Verification:**
- `.omc/plans/baseline-phase-timing.md` exists with 10 runs, median + p95, named verdict (a)/(b)/(c), targeted phase(s) named verbatim.

**Effort:** S

---

## Step 3a — Daemon deferral past "ready"

**Targets** (`boot_sr.c:2410–2416`): `pgbuf_daemons_init`, `dwb_daemons_init`, parallel_query worker_manager init.

**Mechanism:** Spawn background cubthread worker after `cubthread::initialize` (safe — `2216` precedes `2413`). Worker runs the three inits in their existing order.

**Completion primitive:** Per-daemon cubsync condition variables (`g_pgbuf_daemons_ready`, `g_dwb_daemons_ready`, `g_pq_workmgr_ready`). Every first-use path checks the flag and blocks on the CV. No observational gates.

**Sysparam:** `PRM_ID_BOOT_DEFER_DAEMON_INIT` (bool, default **false** at merge; flip to true post-Step-6 pass — Reservation #5: flip is part of Step 6 deliverables).

**SA_MODE:** SERVER_MODE-gated; SA_MODE init path (`boot_sr.c:1637`) unchanged.

**Crash-recovery:** Forced false at runtime on unclean shutdown; logged: "BOOT_DEFER_DAEMON_INIT overridden due to unclean shutdown."

**Verification:**
- Phase 6/7 elapsed drops toward zero on critical path.
- Unit test: page-flush request within ε of "ready" blocks on CV, unblocks cleanly.
- Unclean-shutdown DB: override fires and is logged.
- SA_MODE smoke: sysparam inert.

**Effort:** M

---

## Step 3b — Cache-init deferral past "ready"

**Targets** (`boot_sr.c:2468–2492`): `xcache_initialize`, `qmgr_initialize`, `qfile_initialize_list_cache`, `fpcache_initialize`.

**Mechanism:** Same background-cubthread pattern. **CV completion primitive is mandatory** — first `SELECT 1` reads xcache immediately; missing gate = use-before-init crash.

**First-use sites** (each must wrap entry in "block until ready" helper):
- `src/query/xasl_cache.c` — xcache entry points.
- `src/query/query_manager.c` — qmgr entry points.
- `src/query/fpcache.c` (or equivalent) — fpcache entry points.
- Query-file list cache entry points.

**Sysparam:** `PRM_ID_BOOT_DEFER_CACHE_INIT` (bool, default **false** at merge; flip per Reservation #5).

**SA_MODE:** SERVER_MODE-gated. SA_MODE may init caches on the calling thread (verify during implementation); sysparam inert in SA_MODE.

**Crash-recovery:** Forced false at runtime on unclean shutdown.

**Verification:**
- `SELECT 1` within 100ms of "ready" succeeds (blocks on CV, no crash).
- Negative test: CV stubbed to never signal → first query deadlocks with clear diagnostic (proves the gate is the only synchronization).
- Phase 10 elapsed drops toward zero on critical path.
- SA_MODE smoke + populated-restart regression pass.

**Effort:** M

---

## Step 4 — Lazy-init dominant O(buffer_pool_size) phase

**Prerequisite:** Step 2 selected this phase (case a or b).

### Step 4.0 — BCB-scan audit (HARD GATE)

**Scope:** Enumerate every read/write of BCB fields across **all server-side files** under `src/` (not just `page_buffer.c`):
- Fields: `mutex`, `atomic_latch`, `hash_next`, `prev_BCB`, `count_fix_and_avoid_dealloc`, `hit_age`, `oldest_unflush_lsa`, `tick_lru3`, `tick_lru_list`, plus any additional field discovered during enumeration.
- Tools: `Grep` for `bcb->`, `BCB_`, `pgbuf_bcb_`. Baseline: ~649 refs in `page_buffer.c` alone; cross-file surface is larger.
- Classify each site: (i) LAZY-tolerant, (ii) requires init-before-read, (iii) writer-participating.

**Output:** `.omc/plans/bcb-scan-audit.md` — table {file, line, symbol, fields accessed, classification, notes}.

**Enforcement (Reservation #4):** Gate owner = Planner sign-off + code-reviewer checklist item. CI does not enforce; PR template includes "BCB audit ≥ 100% coverage confirmed" checkbox. No 4.1+ merge until audit artifact exists and covers 100%.

### Step 4.1 — BCB loop split + on-demand init

- Split `pgbuf_initialize_bcb_table` loop into (a) fast pre-pass: single `LAZY_INIT_PENDING` sentinel per BCB; (b) on-demand `pgbuf_bcb_ensure_initialized(bcb)` called at every (ii) / (iii) site from audit.
- Sysparam: `PRM_ID_PB_LAZY_BCB_INIT` (bool, default **false**; flip per Reservation #5).

### Step 4.2 — Iopage first-touch risk

**Risk:** 16GB iopage region first-touch produces ~4M 4KB page faults on 1 core. Step 4.1 does not address this region.

**Pivot clause (Reservation #1):** If Step 6a fails with iopage first-touch median > **30%** of post-Step-4.1 remaining boot time, re-enter ralplan with Option D as first-class. Do NOT execute D-1/D-2 outside ralplan.

**Verification:**
- 4.0 audit published, 100% coverage, reviewer-confirmed.
- With 4.1 sysparam true: `pgbuf_initialize_bcb_table` phase time drops to O(N) sentinel-write only.
- Random `pgbuf_fix` stress test: no crash, no uninitialized reads (ASan/UBSan clean in debug).
- Populated-restart regression ≤ +5%.

**Effort:** L

---

## Step 5 — Right-size O(N) + sysparam combination matrix

**Right-sizing targets (from Step 2 long-tail):** lock table hash buckets, MVCC snapshot arrays, session table, any structure proportional to `data_buffer_size` / `max_clients` over-provisioned for 1-core fresh-boot rig. Change defaults only where Step 2 shows top-5 phase impact.

**Sysparam combination matrix** (`.omc/plans/sysparam-matrix.md`):

| Sysparam | Default (ship) | Tested | Untested / Unsupported |
|---|---|---|---|
| `PRM_ID_BOOT_PERF_TRACE` | false | {any} × all others | — |
| `PRM_ID_BOOT_DEFER_DAEMON_INIT` | false | true × fresh-boot SERVER_MODE | true × unclean shutdown (forced false at runtime); true × SA_MODE (inert) |
| `PRM_ID_BOOT_DEFER_CACHE_INIT` | false | true × fresh-boot SERVER_MODE; true × populated-restart SERVER_MODE | true × unclean shutdown (forced false); true × SA_MODE (inert) |
| `PRM_ID_PB_LAZY_BCB_INIT` | false | true × fresh-boot SERVER_MODE; true × populated-restart SERVER_MODE | true × SA_MODE (inert until explicit audit extension) |

**Reservation #2 directive:** Untested / unsupported combinations must emit a boot-time warning (`er_set(ER_WARNING_SEVERITY, ...)`) — not undefined behavior. Add `boot_check_sysparam_combination()` helper invoked once per boot before `boot_restart_server` dispatches phases.

**Interactions:**
- **Crash recovery:** Any deferral sysparam true → forced false at runtime on unclean shutdown. Logged.
- **SA_MODE:** All four sysparams SERVER_MODE-gated. SA_MODE boot path unchanged.

**Effort:** S–M

---

## Step 6 — Acceptance + regression gate

### 6a — Acceptance run (<10s target, PRIMARY AC)
- Rig: 16GB, cold page cache, SERVER_MODE, 1-core cgroup, bind-mount, fresh DB per run.
- N = 10 runs.
- **Pass:** median < 10.0s AND all 10 < 12.0s.
- **Fail:** any run ≥ 12.0s OR median ≥ 10.0s.
- **Evidence:** `.omc/plans/acceptance-fresh-boot.md`.

### 6b — Regression gate (±5%)
- **Populated-restart:** representative DB, all four sysparams true, SERVER_MODE. ±5% vs. baseline.
- **Crash-recovery:** force unclean shutdown; verify sysparam runtime-override; ±5% vs. baseline.
- **SA_MODE:** boot + smoke query; ±5% (expected inert).

### 6c — Default flip (part of this step's deliverables — Reservation #5)
On 6a + 6b green: flip `PRM_ID_BOOT_DEFER_DAEMON_INIT`, `PRM_ID_BOOT_DEFER_CACHE_INIT`, `PRM_ID_PB_LAZY_BCB_INIT` defaults to true in `src/base/system_parameter.c`. Commit as part of the Step 6 merge.

**Fail branches:**
- 6a fails with iopage dominating > 30% → Option D ralplan re-entry (Reservation #1).
- 6b fails → diagnose / fix / re-run; do NOT flip defaults until green.

**Effort:** M

---

## Acceptance-Criteria ↔ Step Map

| AC from spec | Step(s) |
|---|---|
| AC1 Per-phase TSC instrumentation with <1% overhead, gated | 1 |
| AC2 Baseline per-phase breakdown table | 2 |
| AC3 Top-N phases each get targeted optimization (defer / lazy / right-size) | 3a, 3b, 4, 5 |
| AC4 Final wall time <10s on same config | 6a |
| AC5 Correctness preserved post-ready (connections, catalog, daemons, vacuum) | 3a/3b verification + 6a |
| AC6 No regression populated/crash/SA_MODE | 6b |
| AC7 CUBRID coding rules | All steps + CI gate |

## Reservations (captured as directives, not blockers)

1. **Option D trigger threshold:** "iopage first-touch median > 30% of post-Step-4.1 remaining boot time" triggers ralplan re-entry — Step 4.2 pivot clause.
2. **Sysparam combination assert:** `boot_check_sysparam_combination()` emits warning for untested combinations — Step 5.
3. **Release-build diagnostic trace:** 5-phase `er_log_debug` trace, SA_MODE-suppressed — Step 1.
4. **BCB audit gate owner:** Planner sign-off + PR checklist; CI does not enforce — Step 4.0.
5. **Default flip scoping:** Part of Step 6 deliverables, not a follow-up — Step 6c.

## ADR
- **Decision:** Execute Option A (daemon + cache deferral + lazy BCB init) gated on Step 2 evidence; Step 5 right-sizing complementary; Step 1 release-zero instrumentation prerequisite; Step 6 explicit <10s acceptance + ±5% regression as ship gate.
- **Drivers:** Evidence before code; correctness of lazy/deferred paths (audit + CV); fresh-boot-only scope with regression fence.
- **Alternatives considered:** B (right-size only) insufficient; C (long-tail broad attack) replaced by HALT + ralplan redesign; D (iopage) standing alternative with concrete re-entry threshold.
- **Why chosen:** Highest evidence-per-risk ratio given known file:line anchors and O(N) shape of BCB init; retains honest escape hatch for iopage-dominator case.
- **Consequences:** +4 sysparams; +1 header `boot_perf_trace.h`; +1 background cubthread for deferred init; CV wrappers at xcache/qmgr/qfile/fpcache first-use sites; 3 `.omc/plans/*.md` artifacts (baseline, bcb-audit, sysparam-matrix, acceptance).
- **Follow-ups:** Quarterly acceptance-rig re-run; Option D ralplan cycle if triggered.
