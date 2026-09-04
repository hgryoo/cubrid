#!/usr/bin/env bash
#
# soak.sh - run the Tier 1 harness until it finds something, or until told to stop.
#
# §7.2 measured that the OS saturates the ordering space on its own: repeating a
# fixed input covers every permutation, near-uniformly.  So for Tier 1 time *is*
# the search -- there is nothing to steer, only sessions to run.  This runs them,
# and stops at the first thing worth a person's attention.
#
# A session is one boot, one before-check, the workload, one after-check, one
# shutdown.  Sessions rather than one long run because the full consistency
# check costs 7 s and only belongs at a boundary (roadmap N66 §9.2), and
# because a check that runs while four threads mutate would report a state that
# is torn rather than wrong.
#
#   ./soak.sh <binary> <db> [threads] [iters] [minutes]
#
#   ./soak.sh ./concurrency_spike fuzzdb 4 50 60
#   ./soak.sh ./build_fuzz_tsan/fuzz/concurrency_spike fuzzdb 4 20 120
#
# Stops on: a non-zero exit, an oracle failure, or any sanitizer report the
# baselines do not already cover.  Each of those is a finding; everything else
# is silence, which is the point of the baselines.
#
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
bin="${1:?usage: soak.sh <binary> <db> [threads] [iters] [minutes]}"
db="${2:?need a database name}"
threads="${3:-4}"
iters="${4:-50}"
minutes="${5:-60}"

[ -x "$bin" ] || { echo "no such binary: $bin" >&2; exit 2; }
[ -n "${CUBRID:-}" ] || { echo "\$CUBRID is not set" >&2; exit 2; }

out=$(mktemp -d "${TMPDIR:-/tmp}/soak.XXXXXX")
supp_tsan="$here/tsan-baseline.supp"
supp_ubsan="$here/ubsan-baseline.supp"

# The full check is a session-boundary check by cost, and a session here is
# short, so it runs on every one.
export FUZZ_FULL_CHECK=1
# Empty, and not unset: without it the first sanitizer report blocks in
# llvm-symbolizer fetching debug info over HTTP, and the whole process stalls
# with zero CPU time.  See fuzz/README.md.
export DEBUGINFOD_URLS=
export ASAN_OPTIONS="detect_leaks=0 alloc_dealloc_mismatch=0 halt_on_error=0"
export UBSAN_OPTIONS="print_stacktrace=1 suppressions=$supp_ubsan"
export TSAN_OPTIONS="halt_on_error=0 history_size=4 suppressions=$supp_tsan"

# A carriage return keeps the progress on one line in a terminal; redirected to
# a file it would produce one unreadable line, so use a newline there instead.
if [ -t 1 ]; then eol='\r'; else eol='\n'; fi

deadline=$(( $(date +%s) + minutes * 60 ))
session=0
records=0
heaps=0

printf 'soak  %s  db=%s  threads=%d  iters=%d  for %d min\n' \
       "$(basename "$bin")" "$db" "$threads" "$iters" "$minutes"
printf 'logs  %s\n\n' "$out"

while [ "$(date +%s)" -lt "$deadline" ]; do
  session=$(( session + 1 ))
  log="$out/session-$session.log"
  "$bin" "$db" "$threads" "$iters" 0 > "$log" 2>&1
  rc=$?

  new=$("$here/sanitizer_triage.py" "$log" 2>/dev/null | sed -n '1s/ reports.*//p')
  new=${new:-0}
  h=$(sed -n 's/^heaps created *//p' "$log")
  r=$(sed -n 's/^records *\([0-9]*\).*/\1/p' "$log")
  heaps=$(( heaps + ${h:-0} ))
  records=$(( records + ${r:-0} ))

  printf "  session %-5d  heaps %-8d records %-9d  %s left$eol" \
         "$session" "$heaps" "$records" \
         "$(date -u -d "@$(( deadline - $(date +%s) ))" +%Hh%Mm 2>/dev/null || echo '?')"

  if [ "$rc" -ne 0 ] || [ "$new" -ne 0 ] || grep -q '^FINDING' "$log"; then
    echo
    echo
    echo "=== session $session stopped the soak: exit=$rc, $new sanitizer report(s) ==="
    grep -E '^(FINDING|  (before|after) |heaps|records|failures|shutdown)' "$log"
    [ "$new" -ne 0 ] && "$here/sanitizer_triage.py" "$log"
    echo
    echo "full log: $log"
    exit 1
  fi
done

echo
echo
printf 'clean.  %d sessions, %d heaps, %d records, nothing the baselines did not cover.\n' \
       "$session" "$heaps" "$records"
