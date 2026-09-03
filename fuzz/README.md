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

## Build

```sh
cmake -S . -B build_fuzz \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_FUZZING=ON \
  -DWITH_CCI=OFF -DWITH_JDBC=OFF -DWITH_CMSERVER=OFF
cmake --build build_fuzz --target fuzz_record
```

`ENABLE_FUZZING` instruments **the whole engine**, not only the harness --
libFuzzer needs coverage counters in the code under test.  The engine is
compiled with `-fsanitize=fuzzer-no-link,<FUZZ_SANITIZERS>` and each target
links `-fsanitize=fuzzer,<FUZZ_SANITIZERS>`.  `FUZZ_SANITIZERS` defaults to
`address;undefined`; MSan is deliberately not offered, because it would require
every third-party dependency rebuilt instrumented.

The three `WITH_*=OFF` flags are not required by fuzzing.  They are here because
the drivers and the manager server are not in any fuzz target's link closure,
and because a `git worktree` has no submodules checked out, so leaving `WITH_CCI`
on fails configuration with "Could not find a CCI VERSION file".

## Run

```sh
mkdir -p corpus/record
./build_fuzz/bin/fuzz_record corpus/record -max_total_time=60
```

A crash writes `crash-<sha1>` in the working directory; re-run the binary with
that file as its only argument to replay it.

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
