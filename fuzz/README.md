# fuzz/ — coverage-guided fuzz targets

libFuzzer targets built against the engine.  The harnesses live here; the
corpus, the replay loop and crash triage are `cubrid-testkit`'s job
(roadmap C-055).

## Requirements

**clang.**  libFuzzer is a clang facility.  GCC has `-fsanitize=address` and
`-fsanitize=undefined` but no `-fsanitize=fuzzer`, so configuring with GCC and
`-DENABLE_FUZZING=ON` fails immediately with a message that says so.

```sh
sudo apt install clang-18          # or any clang with compiler-rt
```

**The `cubrid-cci` submodule.**  `src/query/dblink_2pc.c` and
`src/query/dblink_scan.c` include `<cas_cci.h>` unconditionally, so the engine
does not build without it and `-DWITH_CCI=OFF` does not help -- that flag gates
building the driver, not the engine's dependency on its header.  A `git
worktree` starts with no submodules checked out, so in a fresh worktree:

```sh
git submodule update --init cubrid-cci
```

## Build

```sh
cmake -S . -B build_fuzz \
  -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_FUZZING=ON \
  -DWITH_JDBC=OFF -DWITH_CMSERVER=OFF
cmake --build build_fuzz --target fuzz_record -j $(nproc)
```

`ENABLE_FUZZING` instruments **the whole engine**, not only the harness --
libFuzzer needs coverage counters in the code under test.  The engine is
compiled with `-fsanitize=fuzzer-no-link,<FUZZ_SANITIZERS>` and each target
links `-fsanitize=fuzzer,<FUZZ_SANITIZERS>`.  `FUZZ_SANITIZERS` defaults to
`address;undefined`; MSan is deliberately not offered, because it would require
every third-party dependency rebuilt instrumented.

`WITH_JDBC=OFF` and `WITH_CMSERVER=OFF` are not required by fuzzing; they are
here only because neither is in any fuzz target's link closure and both cost
build time.  `WITH_CCI` has to stay **on** -- see above.

## Run

```sh
export LD_LIBRARY_PATH=$PWD/build_fuzz/cubrid:$PWD/build_fuzz/cubrid-cci
export ASAN_SYMBOLIZER_PATH=/usr/lib/llvm-18/bin/llvm-symbolizer
export ASAN_OPTIONS=detect_leaks=0:symbolize=1:alloc_dealloc_mismatch=0

mkdir -p fuzz/corpus/record
./build_fuzz/bin/fuzz_record fuzz/corpus/record -max_total_time=60
```

Re-run the binary with a single file argument to replay that input.

**`alloc_dealloc_mismatch=0` is required, and is not papering over a defect.**
Under `SERVER_MODE`, `src/base/memory_wrapper.hpp` replaces `operator delete`
with one that calls `cub_free ()`, and `#define new new(__FILE__, __LINE__)`
routes every `new` to `cub_alloc ()`.  Both bottom out in plain `malloc ()` and
`free ()` (`src/base/memory_cwrapper.h`).  That is self-consistent, but ASan
intercepts `operator new` and `malloc` as separate allocators and enforces the
pairing, so it reports `malloc vs operator delete` (and the reverse) on
allocations that never left the engine's own scheme.  Leaving the check on
means every run stops on the allocator, before reaching anything about the code
under test.

Related, and worth fixing separately: clang reports those replacements with
`-Winline-new-delete`, because a replacement `operator delete` may not be
declared `inline`.  GCC does not diagnose it.

## Two build shapes, and why both matter

`or_get_int ()` and 28 sibling accessors in `object_representation.h` enforce
the `OR_BUF` bounds with a debug `assert` over a read that happens
unconditionally, and they always return `NO_ERROR`.  So the same input behaves
differently depending on `NDEBUG`:

| Build | `printf '\x00' | fuzz_record` |
|---|---|
| `Debug` (asserts on) | `Assertion 'buf->ptr + OR_INT_SIZE <= buf->endptr' failed` |
| `RelWithDebInfo` (asserts off) | `AddressSanitizer: heap-buffer-overflow, READ of size 4` |

The debug build says the invariant was violated; the release build says what
that costs.  Configure the release shape with `-DCMAKE_BUILD_TYPE=RelWithDebInfo
-DENABLE_SYSTEMTAP=OFF` (systemtap defaults on there and needs `sys/sdt.h`).

## Targets

| Target | Entry point | State |
|---|---|---|
| `fuzz_record` | `or_get_value ()` -- the packed-value decode path | none beyond the domain system |

`or_get_value ()` is the bounded form.  `or_unpack_value ()` calls
`or_init (buf, data, 0)`, and `or_init ()` turns a length of 0 into
`OR_INFINITE_POINTER`, so it reads unbounded *by contract*; fuzzing it would
report a buffer overrun for every malformed input and say nothing about the
decoder.  Real disk and network callers bound the `OR_BUF`, and that is the only
shape in which an overrun is a defect rather than misuse.

## Spikes

`fuzz/spike/` holds measurement programs rather than fuzz targets: each one is a
`main ()` that boots the engine in-process, does something, and prints numbers.
They answer a design question and are then kept as the record of the answer.

| Spike | Question | Answer |
|---|---|---|
| `server_boot_spike` | can `SERVER_MODE` boot in one process, with no listener? | yes — 15 threads, 1.22 s boot+shutdown |
| `noise_floor_spike` | does the OS alone cover the ordering space? | yes — 24/24 permutations, near-uniform, with and without engine work |
| `concurrency_spike` | Tier 1: concurrent storage work under sanitizers, with an oracle | see roadmap N66 §9.2 |
| `reset_spike` | SA-mode state reset determinism | superseded — SA is single-threaded, out of scope |

They are not CMake targets: they are single translation units linked against
whatever `libcubrid.so` a build directory produced. `build.sh` reads the flags
back out of that build's own `flags.make` rather than restating them, because a
spike compiled with different defines than the library it links (`SERVER_MODE`,
`_FILE_OFFSET_BITS`) sees different struct layouts and then fails in ways that
look like engine defects.

```sh
./fuzz/spike/build.sh build_fuzz                        # -> build_fuzz/fuzz/
./fuzz/spike/build.sh build_fuzz_tsan                   # TSan, same sources
./fuzz/spike/build.sh build_fuzz . concurrency_spike    # one, into cwd
```

The rpath is a `DT_RPATH`, not a `DT_RUNPATH`, and that is deliberate:
`LD_LIBRARY_PATH` is searched *before* a `DT_RUNPATH`, so a spike run with
`$CUBRID/lib` on the path — which anything calling the `cubrid` CLI needs —
would silently load the installed engine instead of the instrumented one it was
built against. A spike must run against its own engine build or it is measuring
something else.

`reset_spike` is skipped by the default enumeration: it is `SA_MODE` and
includes `dbi.h`, which `#error`s under `SERVER_MODE`. Name it explicitly to
build it, against an SA build directory.

### Running a spike

A spike boots a real database, so it needs a real environment:

```sh
export CUBRID=$PWD/install.out CUBRID_DATABASES=$CUBRID/databases
cubrid createdb --db-volume-size=64M --log-volume-size=64M fuzzdb en_US
./build_fuzz/fuzz/concurrency_spike fuzzdb 4 10 0     # db threads iters hold_s
```

**`DEBUGINFOD_URLS=` must be set empty.** Without it the first sanitizer report
hangs the whole process, with *zero* CPU time and no output: `llvm-symbolizer`
does not find local debug info for a frame, falls back to fetching it over HTTP
from `debuginfod.ubuntu.com`, and blocks in `curl_multi_poll` inside
`getOrFindDebugBinary ()`. TSan holds its trace-part semaphore across
symbolization, so every other thread stalls behind it in `TraceSwitchPartImpl`
and the run is indistinguishable from a deadlock in the engine. `gdb` prompts
about the same service; the symbolizer just waits.

**`stored_procedure=no` in `cubrid.conf`, for a smaller reason.** The PL server
is a child process the engine forks during boot (`pl_sr.cpp:365`, from a
`pl-monitor` daemon that then `sleep (1)`s). The harness neither drives it nor
needs it, and it costs a second of every boot. It is *not* what caused the hang
above -- that was measured, and the hang survived turning it off.

**A database is reusable across runs.** One iteration is a heap's whole life --
create, fill, then drop or roll back -- so the workload returns the database to
where it found it and the *before* half of the oracle passes on the second and
third run of the same database. That was not true of the first version, which
created heaps and never dropped them; every run then needed a database of its
own, and the leftovers were what made the oracle fail.

### Soaking

Tier 1's search *is* time. §7.2 measured that the OS saturates the ordering
space on its own — repeating one fixed input covers every permutation,
near-uniformly — so there is nothing to steer, only sessions to run.

```sh
export CUBRID=$PWD/install.out CUBRID_DATABASES=$CUBRID/databases
./fuzz/spike/soak.sh $PWD/concurrency_spike soakdb 4 200 60   # 60 minutes
```

A session is one boot, one before-check, the workload, one after-check, one
shutdown. Sessions rather than one long run for two reasons: the full
consistency check costs 7 s and only belongs at a boundary, and a check that ran
while four threads mutated would report a state that is torn rather than wrong.

The soak stops at the first thing worth a person's attention — a non-zero exit,
an oracle failure, or any sanitizer report the baselines do not already cover —
and prints that session's triage table and log path. Everything else is silence,
which is what the baselines are for.

**Running out of room is not a finding, and the runner says so differently.**
The first 32-session soak ended with the filesystem full and reported it as
though the engine had failed, which is how an unattended runner becomes a boy
who cried wolf. Three things now separate the two:

| | |
|---|---|
| exit 0 | clean |
| exit 1 | a finding — read the triage table |
| exit 2 | out of room, or the database outgrew its ceiling. Not a finding. |
| exit 3 (harness) | the storage layer refused for want of space; the runner turns this into exit 2 |

The harness classifies the storage layer's own out-of-space errors
(`ER_IO_FORMAT_FAIL`, `ER_IO_FORMAT_OUT_OF_SPACE`, `ER_BO_CANNOT_CREATE_VOL`
and their siblings) into a separate `out of room` count, and the runner checks
free space and database size *before* each session rather than discovering the
problem afterwards:

```sh
SOAK_MIN_FREE_MB=4096    # stop with this much still free (default)
SOAK_MAX_DB_MB=8192      # recreate the database past this size (default)
SOAK_RECYCLE=0           # stop at the ceiling instead of recreating
```

**The workload grows the database, so a long soak recreates it.** Dropping a
heap defers the reclaim, and the first soak measured roughly 200 MB a session
that never came back — 256 MB became 7.1 GB over 32 sessions, and
`file_tracker_check` went from 0.006 s to 0.774 s with it, linearly. Past the
ceiling a run measures the disk rather than the engine, so the runner recreates
the database and reports how many times it had to: **that count is the growth
rate**, and it is the one number to watch across soaks.

### Triage, and the baselines

Every Tier 1 run reports something, in both build shapes: TSan a few hundred
warnings, UBSan ten. Raw, that is not an oracle -- nobody reads it per run, so
nobody notices the one report that is new. `sanitizer_triage.py` groups reports
by kind and site and emits the group as a suppression file, which is the
difference between a log and an oracle:

```sh
./fuzz/spike/sanitizer_triage.py run.log                      # the table
./fuzz/spike/sanitizer_triage.py run.log --show page_buffer   # full text
./fuzz/spike/sanitizer_triage.py run.log --suppress > new.supp
```

It reads all three producers. TSan reports are grouped by the top *engine*
frame, dropping the `std::thread` and `std::__invoke` scaffolding every daemon
stack goes through. UBSan reports are grouped by check and source line, with the
check name recovered from the message text -- UBSan prints
`undefined-behavior` in every `SUMMARY` line regardless of which check fired,
and the check name is what a suppression is keyed on. **ASan findings are listed
but never baselined**: an ASan report is a memory error that stops the run, so
suppressing one hides a defect rather than a noisy site.

Two baselines are checked in, both measured 2026-09-04 on a clean four-thread,
ten-iteration run:

| File | Rules | A clean run with it |
|---|---:|---|
| `tsan-baseline.supp` | 42 | 222 reports -> 0 |
| `ubsan-baseline.supp` | 3 | 10 reports -> 0 |

Rules 39-42 are what a baseline is *for*, and each arrived the same way: the
harness reached code it had not reached before, and against a silent baseline
the new report was the only thing on the page.

- Dropping the heaps it creates reached `vacuum_add_dropped_file ()`, and TSan
  reported a race on `tdes->interrupt` (`log_tran_table.c:2967`) 40 times.
  Against the 179 the create-only workload already produced, that would have
  been invisible.
- Giving the harness threads real sessions reached the session layer, and TSan
  reported three more — two data races and a lock-order-inversion, all inside
  `session_state_verify_ref_count ()` and `session_set_conn_entry_data ()`. The
  verifier is `#if !defined (NDEBUG)` only, and the "cycle" is one thread's
  lock and unlock of the same hand-rolled `sync_rwlock`.

**A lock-order-inversion rule needs care that a race rule does not.** TSan's
suppression type is `deadlock`, and the top engine frame for such a report is
always the locking primitive — so keying the rule on it (`deadlock:rwlock_read_lock`)
would silence *every* future deadlock report in the engine. The tool skips lock
primitives for this report kind and keys on the caller instead.

```sh
DEBUGINFOD_URLS= TSAN_OPTIONS="halt_on_error=0 history_size=4 \
  suppressions=$PWD/fuzz/spike/tsan-baseline.supp" \
  ./build_fuzz_tsan/fuzz/concurrency_spike fuzzdb 4 10 0

DEBUGINFOD_URLS= FUZZ_FULL_CHECK=1 \
  ASAN_OPTIONS="detect_leaks=0 alloc_dealloc_mismatch=0" \
  UBSAN_OPTIONS="print_stacktrace=1 \
  suppressions=$PWD/fuzz/spike/ubsan-baseline.supp" \
  ./build_fuzz/fuzz/concurrency_spike fuzzdb 4 10 0
```

**They are baselines, not verdicts.** No rule in either has been adjudicated.
The TSan rules sit mostly on `page_buffer.c` and `log_append.cpp`, which use
hand-rolled atomics TSan has no reason to trust; the UBSan rules cover
misaligned loads on packed catalog records, a `bindex_t` shift in the bundled
dlmalloc, and one call through a mismatched function-pointer type. Their only
job is to make silence mean something.

Two format traps, both of which fail silently:

- `#` is honoured only at the *start* of a line. A trailing comment becomes part
  of the pattern, which then matches nothing.
- **UBSan has no line granularity.** A rule is `<check>:<file pattern>`, so one
  rule silences that check for the whole file -- `alignment:*object_primitive.c`
  covers all eight sites and any ninth that appears later. TSan's
  `race:<function>` is tighter. Where that is too blunt, the precise instrument
  is a compile-time `-fsanitize-ignorelist`, not a suppression file.

## What is not here yet

The ladder this follows is in cubrid-testkit's `docs/ROADMAP.md` (§6a appendix):
SQL parser, protocol decoder, record serialize/unpack, then storage operation
sequences.  Only the third exists so far.

The storage-operation-sequence target is a different problem, not a bigger one:
libFuzzer reuses one process for tens of thousands of inputs, so the engine has
to return to a known state at every input boundary or crashes stop reproducing.
That question is tracked as roadmap `N66-fuzz-target-infrastructure` §9 Q1, and
its entry gate is a measurement -- 10,000 runs reproducing deterministically --
not an argument.
