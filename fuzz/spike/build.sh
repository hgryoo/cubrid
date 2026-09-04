#!/usr/bin/env bash
#
# build.sh - compile the spike programs against an already-built engine.
#
# The spikes are not CMake targets on purpose: they are throwaway measurement
# programs, and each one is a single translation unit linked against whatever
# libcubrid.so a given build directory produced.  What they must NOT do is guess
# at compile flags, because a spike compiled with different defines than the
# library it links (_FILE_OFFSET_BITS, SERVER_MODE) sees different struct
# layouts and fails in ways that look like engine defects.  So every flag is
# read back out of the engine's own flags.make.
#
# That also makes a second sanitizer configuration free: point it at a build
# directory configured with -DFUZZ_SANITIZERS=thread and the spikes come out
# under TSan, with no edit here.
#
# --disable-new-dtags makes the rpath a DT_RPATH rather than a DT_RUNPATH, and
# that is not a detail: LD_LIBRARY_PATH is searched *before* DT_RUNPATH, so a
# spike run with $CUBRID/lib on LD_LIBRARY_PATH -- which anything calling the
# cubrid CLI needs -- would silently load the installed engine instead of the
# instrumented one it was built against, and die on the first symbol only the
# latter has.  DT_RPATH wins over LD_LIBRARY_PATH, which is what a spike needs:
# it must run against its own engine build or it is measuring something else.
#
#   ./build.sh <build_dir> [outdir] [spike ...]
#
#   ./build.sh ../../build_fuzz                       # all spikes, ASan+UBSan
#   ./build.sh ../../build_fuzz_tsan /tmp/tsan        # all spikes, TSan
#   ./build.sh ../../build_fuzz . concurrency_spike   # just one
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${1:?usage: build.sh <build_dir> [outdir] [spike ...]}"
build_dir="$(cd "$build_dir" && pwd)"
outdir="${2:-$build_dir/fuzz}"
shift $(( $# > 1 ? 2 : $# ))

flags="$build_dir/cubrid/CMakeFiles/cubrid.dir/flags.make"
[ -f "$flags" ] || { echo "no engine build in $build_dir ($flags missing)" >&2; exit 1; }

lib="$build_dir/cubrid/libcubrid.so"
[ -f "$lib" ] || { echo "libcubrid.so not built in $build_dir" >&2; exit 1; }

# -Dcubrid_EXPORTS is the CMake per-target macro; everything else in the line
# (SERVER_MODE, _FILE_OFFSET_BITS, LINUX, ...) has to match or the ABI does not.
defines=$(sed -n 's/^CXX_DEFINES = //p' "$flags" | sed 's/-Dcubrid_EXPORTS//')
includes=$(sed -n 's/^CXX_INCLUDES = //p' "$flags")
# Only the sanitizer selection is taken from CXX_FLAGS; the engine's warning
# flags are its business, not the spikes'.  fuzzer-no-link is dropped because a
# spike is a main(), not a libFuzzer target.
sanitize=$(sed -n 's/^CXX_FLAGS = //p' "$flags" | tr ' ' '\n' \
  | grep '^-fsanitize=' | sed 's/fuzzer-no-link,//' | head -1)

cxx=$(sed -n 's|^# compile CXX with ||p' "$flags" | head -1)
: "${cxx:=clang++}"

mkdir -p "$outdir"
spikes=( "$@" )
if [ ${#spikes[@]} -eq 0 ]; then
  for f in "$here"/*_spike.cpp; do
    s=$(basename "$f" .cpp)
    # reset_spike is the SA-mode state-reset spike: it includes dbi.h, which
    # #errors under SERVER_MODE, and links libcubridsa.  It is superseded --
    # E9 requirements.md Â§1.0 rules single-threaded SA out of scope -- and is
    # kept only as a record.  Naming it explicitly still builds it, against an
    # SA build directory.
    [ "$s" = reset_spike ] && continue
    spikes+=( "$s" )
  done
fi

echo "engine    $build_dir"
echo "sanitize  ${sanitize:-none}"
echo "out       $outdir"

for s in "${spikes[@]}"; do
  src="$here/${s%.cpp}.cpp"
  [ -f "$src" ] || { echo "  $s: no such spike ($src)" >&2; exit 1; }
  printf '  %-24s' "$s"
  # shellcheck disable=SC2086
  "$cxx" -std=c++17 -g -fno-omit-frame-pointer $sanitize \
    $defines $includes \
    -o "$outdir/${s%.cpp}" "$src" \
    -L"$build_dir/cubrid" -Wl,--disable-new-dtags,-rpath,"$build_dir/cubrid" \
    -lcubrid -latomic -pthread
  echo "ok"
done
