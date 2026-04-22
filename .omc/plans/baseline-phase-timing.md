# Step-2 Baseline: boot_restart_server phase timing

**Date:** 2026-04-20
**Host:** Linux x86_64, 16 cores, 30GB RAM (bare-metal, not Docker)
**Build:** debug (`build_preset_debug`), commit ~62bd76598 + WIP on cubvec/m1-f4-cp
**DB:** fresh `perfboot` per run, 20MB data vol, 20MB log vol
**Config:** `data_buffer_size=16G`, `boot_perf_trace=yes`, `er_log_debug=yes`
**Runs:** N=5, `cubrid server start` wrapper, no core pinning

## Summary

| Metric | Value |
|---|---|
| Wall clock (`cubrid server start` wrapper) | **3.020 s ± 1 ms** |
| Instrumented `boot_restart_server` | **~260 ms** |
| Instrumented / wall | **~8.6 %** |
| Unaccounted (post-dump → wrapper return) | **~2.76 s** |

## Per-phase (representative run)

| Rank | Phase | Time | % of instrumented | % of wall |
|---|---|---:|---:|---:|
| 1 | 14_log_initialize | 73 ms | 28 % | 2.4 % |
| 2 | 04_log_config_and_trantable | 51 ms | 20 % | 1.7 % |
| 3 | pgbuf_initialize (sub) | 45 ms | 17 % | 1.5 % |
| 3a | &nbsp;&nbsp;pgbuf_initialize_bcb_table | 33 ms | 13 % | 1.1 % |
| 3b | &nbsp;&nbsp;pgbuf_bcb_init_loop | 33 ms | 13 % | 1.1 % |
| 4 | 07_mount_and_db_parm | 9 ms | 3.5 % | 0.3 % |
| 5 | 17_caches_init | 8 ms | 3.1 % | 0.3 % |
| 6 | 12_dwb_recover | 6 ms | 2.3 % | 0.2 % |
| 7 | 18_lang_tran_and_finalize | 6 ms | 2.3 % | 0.2 % |
| 8 | 02_thread_init | 2 ms | 0.8 % | <0.1 % |
| 9–21 | (14 others) | ≤1 ms each | <5 % combined | negligible |

## Run-to-run stability

Wall clock across 5 back-to-back runs: 3.020, 3.020, 3.020, 3.021, 3.020 s.
Variance is below instrument precision — indicates the 3 s is a **deterministic wrapper-level wait**, not boot work subject to OS/scheduler noise. Phase timings within `boot_restart_server` also stable (73/51/45/33 ms top-4 reproduced).

## Scope gap — the critical finding

The `BOOT_PHASE_*` instrumentation covers `boot_restart_server` only. That function returns after ~260 ms. Wall-clock stops ~2.76 s later.

Timeline evidence from `perfboot_<ts>.err`:

```
10:06:04.460  phase 1 begin
10:06:04.720  BOOT_PHASE_DUMP()            ← instrumentation window ends (+260ms)
10:06:04.720  Server status is UP          (boot_sr.c:234)
10:06:05.740  "successfully connected to master"  (master_connector.cpp:744, +1.02s)
...           [server idle in epoll on master socket]
10:06:07.500  "cub_server received N as request from master"  (master_connector.cpp:897)
              ← wrapper releases, T1 fires
```

The 2.76 s post-dump is *not* server internal init. It is:
- cubrid_master cold fork + unix-socket setup (~1 s)
- wrapper-level registration acknowledge loop (~1.7 s, appears to be a poll interval hard-coded in the `cubrid` utility, not in server code)

### Implication for the optimization thesis

The plan's working assumption was "35 s boot is 16 GB buffer init dominated; lazy-init will move the needle." The evidence here does not support that framing on this host:

1. **`data_buffer_size=16G` applied**, confirmed in conf. `pgbuf_initialize` completed in 45 ms. Virtual allocation of 16 GB is ~0 cost; the THP-rejected optimization concern was about *runtime* first-touch cost — boot time is unaffected because physical pages are not faulted at init.
2. **No phase inside `boot_restart_server` approaches seconds**, let alone 35 s. The largest (log_initialize) is 73 ms and is I/O-bound on log volume init/open, not buffer-related.
3. **The 35 s the user quoted is likely from a different measurement context** — candidates: (a) Docker cold start (image load + master bootstrap on a 1-core CPU quota), (b) createdb included in the budget, (c) HA/replication catchup, (d) crash recovery with a large log, (e) PL server JVM bootstrap on a cold JVM.

## Decision matrix outcome

| Option | Evidence | Recommendation |
|---|---|---|
| (a) Proceed to optimize largest instrumented phase (log_init) | 73 ms phase saving = 2.4 % of wall | **Reject** — tiny absolute gain |
| (b) Re-scope instrumentation to cover post-`boot_restart_server` work | 92 % of wall unaccounted | **Proceed** for bare-metal scenario |
| (c) Reproduce the 35 s scenario before any optimization work | Cannot explain the 35 s gap with current data | **Proceed first** — source of truth unclear |

### Recommended next step

Before Step 3 (optimization), reproduce the original 35 s measurement and identify what it included. Two concrete asks for the user:

1. **Paste the exact command / Docker config that produced "~35 s".** If it was Docker `--cpuset-cpus=0 --memory=16g`, the wrapper-level wait + master fork on a single-core CPU quota can plausibly stretch to 35 s without any boot work getting slower.
2. **If the target is genuinely minimizing `cubrid server start` wall-clock**, the instrumentation must be extended past `BOOT_PHASE_DUMP()` into `net_server_start → master_connector handshake → wrapper-return`. The current `BOOT_PHASE_*` framework is correctly placed for tracking server-internal init, but is *not* the bottleneck on this host.

## Artifacts

- Raw logs: `.omc/plans/baseline-raw/run_{1..5}.start.log`, `summary.txt`
- Server err (last run): `/home/hgryoo/cubrid/install.out/log/server/perfboot_20260420_1006.err`
- Rig script: `/tmp/measure_boot.sh`
