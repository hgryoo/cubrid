# Boot hotspots — 1 CPU (taskset -c 0), 16G data_buffer, no PL

Run: `perfboot_20260420_1142.err` (fresh DB, one-shot, server left running long enough for DUMP).

## Phase table (elapsed us)

| Phase | us | Source |
|---|---:|---|
| **20_connection_pool** | **1,013,019** | server_support.c:596–598 (`connections.initialize`) |
| 14_log_initialize | 198,004 | boot_sr.c |
| 04_log_config_and_trantable | 52,001 | boot_sr.c |
| pgbuf_initialize | 44,001 | page_buffer.c |
| pgbuf_initialize_bcb_table | 32,001 | page_buffer.c |
| 12_dwb_recover | 13,000 | boot_sr.c |
| 17_caches_init | 7,000 | boot_sr.c |
| 18_lang_tran_and_finalize | 5,000 | boot_sr.c |
| 07_mount_and_db_parm | 4,000 | boot_sr.c |
| 02_thread_init | 2,000 | boot_sr.c |
| 13_daemons_pre_log / 16_vacuum_boot_xhnsw | 1,000 each | boot_sr.c |
| 19_request_worker_pool | ~0 | server_support.c:578–586 |
| 21_master_handshake | ~0 (<1 ms) | master_connector.cpp (connect→established) |

Total instrumented ≈ 1.37 s cub_server side. Wrapper wall = 4.25 s — the rest is `cubrid server start`'s `sleep(1)` poll in `util_service.c:1585` (still present; my `usleep(100ms)` edit is in place but the wrapper's own 1-s barrier elsewhere dominates).

## Dominant: `connection_pool::initialize` (1.013 s, 74% of cub_server time)

Call chain inside `pool::initialize`:
1. `initialize_topology` — NIC-to-core mapping
2. `initialize_freelist` — `110 × new freelist(32 KB)` ≈ 3.5 MB, fast
3. `initialize_coordinator` — spawns coordinator thread
4. `initialize_workers` — spawns `max_connection_workers` threads AND pre-warms each worker with START messages using `enqueue_and_notify(... -1 /* infinite */)` (blocks until worker consumes)
5. `start_coordinator` — sends start signal

On 1 CPU the pre-warm loop (step 4) serializes: main thread enqueues START and waits for each worker to get scheduled and ACK. Each context-switch costs wall time.

Likely fixes (candidates):
- Lazy-spawn connection workers on first client (amortize into connection path rather than boot)
- Skip the blocking pre-warm ACK — `enqueue` without wait, verify with a single post-loop barrier
- Reduce `max_connection_workers` default (currently `PRM_ID_CSS_MAX_CONNECTION_WORKER`) for small-core setups

## Secondary: `14_log_initialize` (198 ms)

- Log recovery ANALYSIS→REDO→UNDO completes in ~70 ms
- Checkpoint runs during init: ~50 ms (flushes 2 pages without logging)
- lgar_t volume destroy + recreate: ~7 ms
- Remaining ~70 ms unaccounted (finer sub-phase instrumentation would help)

## Master handshake (21_master_handshake)

Effectively free once reached: connect→SERVER_REQUEST_ACCEPTED→switch_to_unix_socket all inside 0 ms on the same timestamp (11:42:58.719). Moving the DUMP site into `master_connector.cpp::switch_to_unix_socket` confirmed the handshake itself is not a bottleneck on this machine.

## Fixed during this investigation
- `boot_perf_find_or_append` used pointer-equality for phase names; BEGIN/END across translation units produced `(incomplete)` entries. Switched to `strcmp` fallback (`boot_perf_trace.c`).
- `PRM_ID_BOOT_PERF_TRACE` was `PRM_FOR_SERVER` only → SA_MODE `cubrid createdb` errored "Unrecognized keyword". Widened to `PRM_FOR_CLIENT | PRM_FOR_SERVER`; moved config into `[@perfboot]` section as a belt-and-braces workaround.
- `cubrid.conf` is overwritten by `ninja install` — must be restored after every install.
