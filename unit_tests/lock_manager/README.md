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

At the current code stage, a good first step is an **initialization seam** based on `LK_INIT_CONFIG`,
`lock_initialize_default_config()`, and `lock_initialize_with_config()`, so object lock table sizing,
block partitioning, and daemon startup can be controlled externally.

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
void lock_initialize_default_config (LK_INIT_CONFIG *config);
int lock_initialize_with_config (const LK_INIT_CONFIG *config);
```

`LK_INIT_CONFIG` should provide at least the following controls:

- hash bucket sizing
- resource freelist preallocation sizing
- entry freelist preallocation sizing
- deadlock detector on/off
- detector execution interval
- lock escalation threshold override

This allows unit tests and benchmarks to exercise the same code path without being pinned to server defaults.

### 3. Let the test module own mock OID generation

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
  - mock thread registry
  - mock suspend/resume
  - fake clock / event hooks
- `test_lock_manager_scenarios.*`
  - generate mock OID sets
  - define workload scripts

## Current runner usage

The current `test_lock_manager` binary can already list and execute deterministic scenario plans.
It does not yet drive the production lock manager directly, but it does let you run the planned workloads,
inspect operation mixes, and validate that mock OID generation and scenario composition match expectations.

Examples:

```bash
test_lock_manager --list
test_lock_manager --scenario hot_row --transaction 8 --iterations 1000 --sample 10
test_lock_manager --scenario deadlock_detector --transaction 4 --iterations 20 --sample 12
test_lock_manager --scenario escalation_sweep --transaction 8 --iterations 50 --hotset 16
```

### How to run each test mode

#### 1. Scenario inspection mode

Use this mode to generate a deterministic mock-OID workload and inspect its composition.

```bash
test_lock_manager --scenario hot_row --transaction 8 --iterations 1000 --sample 10
```

This prints:

- the scenario name and description
- transaction count / iteration count / hotset size
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
- `lock_conversion` produces conversion events
- `deadlock_detector` produces at least one conflicting `X_LOCK` denial along the deadlock-prone lock order
- `escalation_sweep` produces escalation candidates

Each passing scenario is printed as a separate `[functional] passed: ...` line.

#### 3. Benchmark mode

Use this mode to run all implemented scenarios repeatedly and print CSV-style performance output.

```bash
test_lock_manager --benchmark --loops 100
```

The output columns are:

- `scenario`
- `loops`
- `ops`
- `total_us`
- `ops_per_sec`
- `conflicts`
- `deadlock_pairs`
- `conversions`
- `escalation_candidates`

This is useful for comparing relative workload pressure even before the harness is connected to the production lock manager.

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
