# lock_manager Performance Test and Isolated Loading Design Notes

This document summarizes a recommended direction for loading the lock manager implemented in
`src/transaction/lock_manager.c` and `src/transaction/lock_manager.h` more independently in the
`unit_tests/` style, and for measuring its performance without coupling it to real
DB/storage/query module load.

## Goals

- It should be possible to generate large numbers of lock resources using mock `OID` values.
- The lock manager should be measured in isolation from the real load of surrounding modules such as storage,
  locator, query, and boot.
- Like other `unit_tests/` modules, it should be runnable through an explicit test binary (`test_lock_manager`).
- Functional verification and performance measurement should be separated, while sharing the same fixture and
  scenario definitions.

## Why a dedicated harness is needed

At the moment, `lock_manager.c` is difficult to test in isolation by simply calling a few functions directly,
because it has the following characteristics:

- It is tightly coupled to `THREAD_ENTRY`, the transaction table, the deadlock/wait-for graph,
  and thread suspend/resume flows.
- It indirectly references global state from the boot/log/query/monitor layers.
- Its internal state is built around a global lock table, daemon threads, and hash buckets, so fixture setup is heavy.
- From a performance perspective, what we usually want to observe is lock hash collisions, wait queue length,
  lock conversion, and deadlock detection overhead, all of which can be reproduced independently of DB page I/O.

Because of that, it is better to build a lock-manager-specific seam and load the module through
`unit_tests/lock_manager/` than to start by booting the entire server and then stressing the lock manager.

At the current code stage, a good first step is an **initialization seam** based on `LK_CONFIG`,
`lock_initialize_default_config()`, and `lock_initialize_with_config()`, so object lock table sizing,
block partitioning, and daemon startup can be controlled externally. The runtime should also prebuild
an OID pool so class/object locality and collision patterns are explicit benchmark inputs rather than
ad-hoc values scattered through each scenario.

## Recommended architecture

### 1. Add a lock-manager-specific runtime seam

It is recommended to gather the external dependencies that `lock_manager.c` accesses directly into one small
structure. The core idea is to separate the locking algorithm from server runtime services.

Example:

```c
/* conceptual example */
typedef struct lk_runtime lk_runtime;
struct lk_runtime
{
  THREAD_ENTRY *(*find_thread_by_tran_index) (int tran_index);
  int (*get_current_tran_index) (THREAD_ENTRY *thread_p);
  void (*suspend_thread) (THREAD_ENTRY *thread_p);
  void (*resume_thread) (THREAD_ENTRY *thread_p, int wait_state);
  INT64 (*get_clock_msec) (void);
  void *(*alloc_fn) (size_t size);
  void (*free_fn) (void *ptr);
  void (*event_log_fn) (const char *msg);
};
```

The production server would provide a `server_runtime` wrapper around the existing implementation,
while unit tests would provide a `mock_runtime` to control:

- transaction-index-to-thread mapping
- suspend/resume events
- time progression
- memory instrumentation
- wakeup ordering observation for deadlock victim selection

### 2. Split the initialization path into two layers

Keep the current `lock_initialize()` / `lock_finalize()` entry points, but add test-oriented initialization APIs such as:

```c
void lock_initialize_default_config (LK_CONFIG *config);
int lock_initialize_with_config (const LK_CONFIG *config);
```

`LK_CONFIG` should provide at least the following controls:

- hash bucket sizing
- resource freelist preallocation sizing
- entry freelist preallocation sizing
- deadlock detector on/off
- detector execution interval
- lock escalation threshold override

This allows unit tests and benchmarks to exercise the same code path without being pinned to server defaults.

### 3. Let the test module own a prebuilt mock OID pool

Even without the real object module, lock resource keys can be created as long as the `OID` values are filled in
consistently. Recommended rules:

- Use a fixed `volid` as a workload namespace, for example `1`.
- Use `pageid` to control hot/cold sets.
- Use `slotid` to control row fan-out.
- Use a different `pageid` range for class OIDs than for instance OIDs to make collision analysis easier.

Example:

```c
static OID
make_mock_oid (int pageid, short slotid)
{
  OID oid;
  oid.volid = 1;
  oid.pageid = pageid;
  oid.slotid = slotid;
  return oid;
}
```

The test harness should build this pool before generating any operations, and scenarios should only draw
from that pool. This keeps the distribution stable and makes it possible to tune:

- number of classes in the workload
- number of rows per class
- hot-row versus cold-row skew
- hash-collision pressure

With this approach, the following cases can be reproduced without descending into storage/object/locator:

- hotspot contention on the same row
- many row locks under the same class
- mixed class-lock and instance-lock behavior
- biased hash bucket collisions

## Workload scenarios to measure first

The scenarios below are useful for observing the lock manager structure itself, independent of full DB engine load.

### A. Single hot-row contention

- `N` workers repeatedly request `X_LOCK` on the same `(class_oid, oid)`
- Purpose:
  - acquire latency growth as waiter queue length increases
  - suspend/resume cost
  - lock entry reuse efficiency
- Metrics:
  - throughput (ops/sec)
  - p50/p95/p99 acquire latency
  - average queue depth
  - confirmation that no timeout/deadlock occurs in the intended scenario

### B. Hot class + cold rows mix

- 10% class-level `IX`/`X`
- 90% instance-level `S`/`X` on different rows within the same class
- Purpose:
  - conflict cost between class locks and instance locks
  - update cost of total holder/waiter modes
  - behavior near the lock escalation threshold

### C. Lock-conversion-heavy workload

- The same transaction repeatedly performs `S -> X`, `IS -> IX`, or `IX -> X` conversion
- Purpose:
  - conversion cost inside the holder list
  - fairness when new waiters arrive while a conversion is waiting

### D. Deadlock detector stress

- Two or more worker groups lock rows in cross order to intentionally create cycles
- Purpose:
  - wait-for graph update cost
  - scan cost per detector run
  - recovery time after victim selection
- Variants:
  - 2-cycle, 3-cycle, long chain + single back edge

### E. Hash collision stress

- Intentionally map mock OIDs to the same bucket
- Purpose:
  - performance degradation as resource hash chains grow
  - bucket mutex contention measurement
- Required support:
  - hash size should be overridable during test initialization

### F. Many transactions / low contention

- There are many workers, but each uses a distinct OID set
- Purpose:
  - baseline acquire/release cost when contention is minimal
  - separation of entry allocation/free cost and transaction-list linking cost

### G. Escalation threshold sweep

- Gradually increase the number of row locks under a single class and sweep the threshold
- Purpose:
  - performance difference before and after row-to-class escalation
  - sensitivity of the threshold value

## Metric design

Performance testing should not rely only on total execution time; it also needs counters aligned with the internal
structure of the lock manager. Recommended counters:

- total acquire attempts / successes / timeouts / deadlock victims
- immediate grant ratio
- conversion success count
- maximum resource hash bucket chain length
- average and maximum holder/waiter lengths
- deadlock detector run count / total elapsed time
- suspend/resume count
- freelist hit/miss count

If possible, internal instrumentation hooks such as `#if defined (UNIT_TEST_LOCK_MANAGER_INTERNALS)` should be used,
and disabled in production builds.

## Recommended file layout for `unit_tests/lock_manager`

```text
unit_tests/lock_manager/
├── CMakeLists.txt
├── test_main.cpp
├── test_lock_manager_functional.cpp
├── test_lock_manager_benchmark.cpp
├── test_lock_manager_mock_runtime.hpp
├── test_lock_manager_mock_runtime.cpp
├── test_lock_manager_scenarios.hpp
└── test_lock_manager_scenarios.cpp
```

### Responsibility split

- `test_lock_manager_functional.cpp`
  - wait queue ordering
  - conversion correctness
  - timeout/deadlock recovery correctness
- `test_lock_manager_benchmark.cpp`
  - run scenarios A-G above
  - sweep thread count / hash size / hotspot ratio
- `test_lock_manager_mock_runtime.*`
  - transaction-to-thread execution runtime
  - `cubthread::entry_workpool` based worker-thread scheduling and barriers
  - wait/timeout hooks for blocked lock requests
- `test_lock_manager_scenarios.*`
  - prebuild class/object OID pools
  - define workload scripts

## Current runner usage

The current `test_lock_manager` binary can already list and execute deterministic scenario plans.
It drives the exported lock-manager API through a lightweight server-style harness and executes worker actions
on a `cubthread::entry_workpool`, so each worker runs with a real `THREAD_ENTRY` on an actual worker thread.

Examples:

```bash
test_lock_manager --list
test_lock_manager --scenario hot_row --transaction 8 --iterations 1000 --class-count 4 --objects-per-class 64 --hot-ratio 80 --sample 10
test_lock_manager --scenario deadlock_detector --transaction 2 --iterations 20 --class-count 1 --objects-per-class 8 --hot-ratio 100 --sample 12
test_lock_manager --scenario escalation_sweep --transaction 8 --iterations 50 --class-count 8 --objects-per-class 64 --hot-ratio 60 --collision-ratio 20 --hotset 16
```

### How to run each test mode

#### 1. Scenario inspection mode

Use this mode to generate a deterministic mock-OID workload and inspect its composition.

```bash
test_lock_manager --scenario hot_row --transaction 8 --iterations 1000 --class-count 4 --objects-per-class 64 --hot-ratio 80 --sample 10
```

This prints:

- the scenario name and description
- transaction count / iteration count / hotset size
- class count / objects per class / hot ratio / collision ratio
- total operation count
- distinct class count / distinct OID count
- operation-kind and lock-mode distributions
- a sample of generated operations

#### 2. Functional test mode

Use this mode to run deterministic checks for a subset of scenarios against the lock manager API.

```bash
test_lock_manager --functional
```

The current functional suite validates:
- `lock_manager_api` calls `lock_initialize_default_config()`, then performs a smoke initialization/finalization
  cycle through `lock_initialize_with_config()` / `lock_finalize()`; it also issues real `lock_object()` and
  `lock_unlock_all()` calls against two explicit `tran_index` values to validate shared-grant and conflict paths
- `hot_row` uses exactly one target OID
- `lock_conversion` produces engine `Num_object_locks_converted` events
- `deadlock_detector` drives real blocking `X_LOCK` acquisition on worker threads and validates that deadlock
  detection increments engine `Num_lock_deadlocks_detected`
- `escalation_sweep` drives real escalation and validates engine `Num_object_lock_escalations`

Each passing scenario is printed as a separate `[functional] passed: ...` line.

#### 3. Benchmark mode

Use this mode to run all implemented scenarios repeatedly and print CSV-style performance output.

```bash
test_lock_manager --benchmark --loops 100 --transaction 8 --class-count 8 --objects-per-class 64 --hot-ratio 75 --collision-ratio 20 --deadlock-detection-interval 0.25 --lock-escalation-at 16 --benchmark-format both
```

The output columns are:

- `scenario`
- `loops`
- `ops`
- `total_us`
- `ops_per_sec`
- `deadlock_interval_secs`
- `conflicts`
- `lock_waits`
- `lock_conversions`
- `lock_escalations`
- `deadlocks_detected`
- `lock_wait_time_usec`

These values now come from the engine perf/stat layer instead of harness-side bookkeeping. The benchmark also prints
an `Engine Lock Stats` section in a statdump-like format so `Num_object_locks_acquired`, `Num_object_locks_waits`,
`Num_object_locks_converted`, `Num_object_lock_escalations`, and `Num_lock_deadlocks_detected` can be read with the
same naming style used by the server-side statistics dump.
Deadlock-heavy runs can also emit detector and timeout log lines from the underlying engine while still completing.
When `--benchmark-format pretty` or `--benchmark-format both` is used, the runner also prints a human-friendly
summary with ASCII throughput/conflict/deadlock bars so scenario differences are easier to scan.

### Benchmark parameters

The benchmark runner reuses the same base configuration for every scenario in the suite. That means the CLI
parameters are best understood as "workload shape controls" rather than as one-off options for a single case.

- `--transaction`
  - Number of concurrent worker transactions scheduled into the benchmark run.
  - Higher values usually increase lock-table concurrency, waiter-list pressure, and deadlock exposure.
- `--iterations`
  - Number of scripted operation rounds per transaction.
  - Higher values increase total lock acquire/convert/commit volume.
- `--class-count`
  - Number of logical class OID buckets prebuilt in the OID pool.
  - Lower values force more transactions into the same table-level lock domain; higher values spread load across more tables.
- `--objects-per-class`
  - Number of object OIDs created under each class bucket.
  - Low values make row-level reuse and collision more likely; high values spread row locks out.
- `--hotset`
  - Number of objects per class that belong to the hot subset.
  - If omitted, the runner derives it as roughly `objects_per_class / 4`.
- `--hot-ratio`
  - Percentage of accesses routed to the hot subset.
  - High values repeatedly revisit the same hot rows; low values spend more requests on the cold remainder.
- `--collision-ratio`
  - Percentage of accesses routed to the prebuilt hash-collision OID set.
  - This matters most for `hash_collision`, where the collision set is generated to land in the same lock-hash bucket under the default lock-table sizing.
- `--loops`
  - Number of times the full benchmark suite is repeated for measurement.
  - Use small values while tuning a workload shape, then increase once the pattern looks right.
- `--deadlock-detection-interval`
  - Deadlock detector polling interval in seconds.
  - Lower values make deadlock detection react faster, but can also add more detector overhead and reduce throughput in deadlock-heavy workloads.
- `--lock-escalation-at`
  - Engine `lock_escalation` threshold applied before each simulated run.
  - Lower values make `escalation_sweep` and mixed class/object scenarios escalate sooner.
- `--benchmark-format`
  - `csv`: machine-friendly output only
  - `pretty`: human-friendly summary only
  - `both`: CSV plus summary/ASCII bars

### Practical tuning guidance

Use the parameters as a way to control where contention appears.

- More table-level contention:
  - reduce `--class-count`
  - increase `--transaction`
  - increase `--iterations`
- More row hotspot contention:
  - reduce `--hotset`
  - reduce `--objects-per-class`
  - increase `--hot-ratio`
  - use scenarios like `hot_row` or `hot_class_cold_rows`
- More hash bucket pressure:
  - increase `--collision-ratio`
  - use `hash_collision`
- More lock conversion pressure:
  - keep `--transaction` moderate
  - keep `--class-count` low-to-medium
  - use `lock_conversion`
- More escalation-style pressure:
  - keep `--class-count` low
  - keep `--objects-per-class` moderate
  - use `escalation_sweep`
- More deadlock pressure:
  - keep `--transaction` small but at least 2
  - keep `--class-count` low
  - keep `--hot-ratio` high
  - reduce `--deadlock-detection-interval`
  - use `deadlock_detector`

### Example workload families

The current harness is not a full TPC-C or YCSB implementation, but it can approximate their lock-shape tendencies.
The goal is not SQL realism; the goal is to borrow their contention patterns and reproduce similar lock-manager stress.

### Recommended presets

Use this table when you want a quick starting point instead of tuning each parameter from scratch.
The values are intentionally opinionated; treat them as baseline presets and adjust from there.

| Goal | Approximation | `--transaction` | `--iterations` | `--class-count` | `--objects-per-class` | `--hot-ratio` | `--collision-ratio` | `--deadlock-detection-interval` | `--lock-escalation-at` | `--loops` | Watch first |
|------|---------------|-----------------|----------------|-----------------|-----------------------|---------------|---------------------|---------------------------------|--------------------------|-----------|-------------|
| Broad low-contention OLTP | YCSB uniform | 16 | 100 | 16 | 256 | 20 | 5 | 1.0 | 1000 | 20 | `low_contention`, `lock_conversion` |
| Hot-key skew | YCSB zipfian | 16 | 100 | 4 | 64 | 85 | 10 | 1.0 | 1000 | 20 | `hot_row`, `hot_class_cold_rows` |
| Multi-table write hotspot | TPC-C new-order style | 10 | 80 | 8 | 48 | 70 | 5 | 1.0 | 1000 | 20 | `hot_class_cold_rows`, `lock_conversion` |
| Partition or warehouse skew | TPC-C warehouse skew | 24 | 60 | 2 | 96 | 75 | 10 | 1.0 | 8 | 20 | `hot_row`, `escalation_sweep` |
| Internal lock-table stress | Hash collision stress | 12 | 80 | 4 | 32 | 50 | 80 | 1.0 | 1000 | 20 | `hash_collision` |
| Deadlock-heavy regression check | Wait-for graph stress | 2 | 20 | 1 | 8 | 100 | 25 | 0.1 | 1000 | 10 | `deadlock_detector`, `deadlocks_detected`, `lock_wait_time_usec` |
| Escalation sensitivity check | Row accumulation under one class | 8 | 50 | 8 | 64 | 60 | 20 | 1.0 | 4 | 20 | `escalation_sweep`, `lock_escalations` |

Example commands:

```bash
test_lock_manager --benchmark --transaction 16 --iterations 100 --class-count 16 --objects-per-class 256 --hot-ratio 20 --collision-ratio 5 --deadlock-detection-interval 1.0 --lock-escalation-at 1000 --loops 20 --benchmark-format both
test_lock_manager --benchmark --transaction 16 --iterations 100 --class-count 4 --objects-per-class 64 --hot-ratio 85 --collision-ratio 10 --deadlock-detection-interval 1.0 --lock-escalation-at 1000 --loops 20 --benchmark-format both
test_lock_manager --benchmark --transaction 10 --iterations 80 --class-count 8 --objects-per-class 48 --hot-ratio 70 --collision-ratio 5 --deadlock-detection-interval 1.0 --lock-escalation-at 1000 --loops 20 --benchmark-format both
```

#### 1. YCSB-like uniform read/update mix

This approximates a broad key-value workload where requests are spread fairly evenly.

Recommended settings:

```bash
test_lock_manager --benchmark \
  --transaction 16 \
  --iterations 100 \
  --class-count 16 \
  --objects-per-class 256 \
  --hot-ratio 20 \
  --collision-ratio 5 \
  --deadlock-detection-interval 1.0 \
  --loops 20 \
  --benchmark-format both
```

Interpretation:

- large `class-count` and `objects-per-class` spread locks widely
- low `hot-ratio` keeps hotspots weak
- low `collision-ratio` avoids artificial hash stress
- `low_contention` and `lock_conversion` become the most representative rows in the output

This is closest to:

- YCSB Workload A/B with a large keyspace
- applications where rows are mostly independent and conflict is incidental

#### 2. YCSB-like zipfian hotspot

This approximates a skewed key-value workload where a small fraction of rows takes most of the traffic.

Recommended settings:

```bash
test_lock_manager --benchmark \
  --transaction 16 \
  --iterations 100 \
  --class-count 4 \
  --objects-per-class 64 \
  --hot-ratio 85 \
  --collision-ratio 10 \
  --deadlock-detection-interval 1.0 \
  --loops 20 \
  --benchmark-format both
```

Interpretation:

- fewer classes and fewer rows per class make the working set smaller
- high `hot-ratio` forces repeated access to the same small row subset
- `hot_row` and `hot_class_cold_rows` should show strong conflict growth

This is closest to:

- YCSB Workload A/F under skewed popularity
- cache-miss fallback paths where many requests converge on a small key set

#### 3. TPC-C new-order style hotspot

TPC-C mixes many tables, but lock pressure often concentrates on a few high-traffic logical entities such as
district/order counters and hot warehouse-local rows. To approximate that pattern:

```bash
test_lock_manager --benchmark \
  --transaction 10 \
  --iterations 80 \
  --class-count 8 \
  --objects-per-class 48 \
  --hot-ratio 70 \
  --collision-ratio 5 \
  --deadlock-detection-interval 1.0 \
  --loops 20 \
  --benchmark-format both
```

Interpretation:

- medium `class-count` reflects multiple tables participating in a transaction
- medium/high `hot-ratio` creates repeated access to hot rows inside each table
- `hot_class_cold_rows` is the best proxy here because it mixes class and instance pressure

This is closest to:

- TPC-C `NEW_ORDER` / `PAYMENT`-like write-heavy flows
- OLTP systems where several related tables are touched per business action

#### 4. TPC-C warehouse skew / partition imbalance

This approximates many transactions targeting a small subset of partitions or warehouses.

```bash
test_lock_manager --benchmark \
  --transaction 24 \
  --iterations 60 \
  --class-count 2 \
  --objects-per-class 96 \
  --hot-ratio 75 \
  --collision-ratio 10 \
  --deadlock-detection-interval 1.0 \
  --loops 20 \
  --benchmark-format both
```

Interpretation:

- very low `class-count` forces more work into the same table domains
- high concurrency with moderate row cardinality increases lock waits
- `hot_class_cold_rows`, `hot_row`, and `escalation_sweep` become useful signals

This is closest to:

- a TPC-C deployment where a few warehouses dominate traffic
- tenant imbalance in multi-tenant OLTP systems

#### 5. Secondary-index / lock-hash stress

This does not correspond directly to a standard benchmark, but it is useful when you want to isolate internal
lock-table behavior instead of business workload realism.

```bash
test_lock_manager --benchmark \
  --transaction 12 \
  --iterations 80 \
  --class-count 4 \
  --objects-per-class 32 \
  --hot-ratio 50 \
  --collision-ratio 80 \
  --deadlock-detection-interval 1.0 \
  --loops 20 \
  --benchmark-format both
```

Interpretation:

- high `collision-ratio` intentionally stresses hash-chain and bucket-mutex behavior
- `hash_collision` becomes the key row to inspect

### How to read the output

The pretty summary is most useful when read comparatively across scenarios.

- If `hot_row` is much slower than `low_contention`:
  - row-level waiter management is dominating
- If `hot_class_cold_rows` is much slower than `hot_row`:
  - class/instance interaction cost is high
- If `lock_conversion` shows high conversions but low conflicts:
  - conversion path is active without severe contention
- If `deadlock_detector` shows very low throughput and non-zero `deadlocks_detected`:
  - the wait-for graph and victim resolution path is actively engaged
- If `hash_collision` drops sharply while `low_contention` remains healthy:
  - internal lock-table/hash structure is the bottleneck, not logical contention
- If `escalation_sweep` shows many `lock_escalations`:
  - row accumulation under the same class is high enough that escalation policy deserves attention

For manual analysis, a good workflow is:

1. Start with `--benchmark-format pretty` and a small `--loops` value.
2. Adjust one parameter at a time, especially `class-count`, `objects-per-class`, and `hot-ratio`.
3. Once the shape looks right, rerun with `--benchmark-format both`.
4. Save the CSV portion for diffing across commits or branches.

## Incremental rollout order

### Step 1: add the seam, no functional change

- Wrap the external dependencies of `lock_manager.c` with small wrapper functions.
- Keep the default runtime bound to the existing server implementation.
- Preserve current behavior by making `lock_initialize()` use the default runtime internally.

### Step 2: secure deterministic functional unit tests with real lock-manager API calls

- Start with deterministic 1-thread, 2-thread, and 3-thread scenarios.
- Control deadlock and timeout behavior using a fake clock so tests stay non-flaky.

### Step 3: add a benchmark binary

- Build `test_lock_manager` only when `UNIT_TEST_LOCK_MANAGER=ON`.
- Accept runtime arguments such as scenario/transaction_count/seconds/hash_size.
- Print results to stdout in table form, and optionally also export CSV.

### Step 4: separate CI from performance runs

- Only correctness tests should run in CI.
- Benchmarks should run locally or on dedicated machines.
- Raw performance numbers should not be used as CI pass/fail criteria; instead, maintain a separate baseline
  comparison script for regression tracking.

## Implementation cautions

- `lock_manager.c` is large and heavily coupled, so avoid large refactors at the beginning.
  It is safer to start by wrapping only the external call boundaries.
- Do not try to mock the full `THREAD_ENTRY` immediately.
  It is better to design a lightweight fixture around the fields that the lock manager actually reads.
- Instead of always running the deadlock detector as a real daemon thread in tests,
  it is better for reproducibility to support an explicit `tick_detector()`-style call.
- Benchmarks should keep OID generation rules under a fixed seed so runs are comparable.

## Recommended first minimum deliverable

Instead of trying to build a fully standalone lock manager immediately, the following sequence is more realistic:

1. inventory the external dependencies of `lock_manager`
2. add `lock_initialize_with_config()`
3. add `unit_tests/lock_manager/test_lock_manager_mock_runtime.*`
4. implement only scenarios A, D, and E first
5. add internal counter dumps

Even these five steps are enough to answer practical questions such as:

- How much does increasing hash bucket count improve hotspot-row contention?
- How do throughput and victim detection time change when the deadlock detector interval is reduced?
- Which bottleneck appears first: row-level hotspot contention or class-level contention?

## Conclusion

For lock manager performance testing, independently loading `lock_manager` with mock OID workloads and driving it
through the exported lock-manager API is far more efficient and analytically useful than trying to reproduce full-DB
workloads first.

In one sentence, the recommended direction is this:

> To run `lock_manager` as a dedicated unit-tests-style binary,
> separate server dependencies through a runtime seam and measure mock-OID-based scenarios A-G first.
