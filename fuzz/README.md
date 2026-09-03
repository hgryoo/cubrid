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
