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
#
# The database is recreated whenever it outgrows SOAK_MAX_DB_MB, so a soak runs
# as long as it is given.  How often that happens is the growth rate, and it is
# reported at the end.
#   ./soak.sh ./build_fuzz_tsan/fuzz/concurrency_spike fuzzdb 4 20 120
#
# Stops on a finding: a non-zero exit, an oracle failure, or any sanitizer
# report the baselines do not already cover.
#
# It also stops when the *machine* runs out of room, and says so differently.
# That distinction is not pedantry -- the first 32-session soak ended with the
# filesystem full and reported it as though the engine had failed, which is how
# an unattended runner becomes a boy who cried wolf.  The harness classifies the
# storage layer's out-of-space errors and exits 3 for them; this watches free
# space and the database's own growth besides, because the run should stop
# before it fills a disk rather than after.
#
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
bin="${1:?usage: soak.sh <binary> <db> [threads] [iters] [minutes]}"
db="${2:?need a database name}"
threads="${3:-4}"
iters="${4:-50}"
minutes="${5:-60}"

# Stop with this much room still free.  A soak that fills a shared disk has done
# more harm than the defect it was looking for.
min_free_mb="${SOAK_MIN_FREE_MB:-4096}"
# A database that grows without bound would eventually end the soak, and this
# workload does grow -- roughly 200 MB a session, because dropping a heap defers
# the reclaim.  Past this size the run measures the disk rather than the engine,
# so the database is recreated and the count reported: how often that happens
# *is* the growth rate, which is worth knowing.  SOAK_RECYCLE=0 stops instead.
max_db_mb="${SOAK_MAX_DB_MB:-8192}"
recycle="${SOAK_RECYCLE:-1}"
vol_size="${SOAK_DB_VOLUME_SIZE:-256M}"
log_size="${SOAK_LOG_VOLUME_SIZE:-128M}"

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

dbdir="${CUBRID_DATABASES:-$CUBRID/databases}"

room () {   # free MB on the filesystem holding the databases
  df -Pm "$dbdir" 2>/dev/null | awk 'NR==2 {print $4}'
}
dbsize () { # MB the database occupies, its extra volumes included
  du -sm "$dbdir"/"$db"* 2>/dev/null | awk '{t+=$1} END {print t+0}'
}
recreate () {
  ( cd "$dbdir" \
    && cubrid deletedb "$db" \
    && cubrid createdb --db-volume-size="$vol_size" --log-volume-size="$log_size" "$db" en_US
  ) >/dev/null 2>&1
}

recycles=0

while [ "$(date +%s)" -lt "$deadline" ]; do
  free=$(room); used=$(dbsize)
  if [ -n "$free" ] && [ "$free" -lt "$min_free_mb" ]; then
    echo; echo
    echo "=== stopping: ${free} MB free, below the ${min_free_mb} MB floor ==="
    echo "Not a finding.  Free space or lower SOAK_MIN_FREE_MB and run again."
    exit 2
  fi
  if [ "$used" -gt "$max_db_mb" ]; then
    if [ "$recycle" = 0 ]; then
      echo; echo
      echo "=== stopping: $db is ${used} MB, past the ${max_db_mb} MB ceiling ==="
      echo "Not a finding, but worth a look: the workload is not reclaiming what"
      echo "it takes.  Recreate the database, or raise SOAK_MAX_DB_MB."
      exit 2
    fi
    if ! recreate; then
      echo; echo
      echo "=== stopping: could not recreate $db (needs cubrid on PATH) ==="
      exit 2
    fi
    recycles=$(( recycles + 1 ))
    used=$(dbsize)
  fi

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

  left=$(( deadline - $(date +%s) ))
  [ "$left" -lt 0 ] && left=0
  printf "  session %-5d  heaps %-8d records %-9d  db %5d MB  %02dh%02dm left$eol" \
         "$session" "$heaps" "$records" "$used" "$(( left / 3600 ))" "$(( left % 3600 / 60 ))"

  if [ "$rc" -eq 3 ] && [ "$new" -eq 0 ]; then
    echo; echo
    echo "=== session $session ran out of room ==="
    grep -E '^(out of room|heaps|records|failures)' "$log"
    echo "Not a finding: the harness classified the storage layer's own"
    echo "out-of-space errors.  $(room) MB free, $db is $(dbsize) MB."
    echo "full log: $log"
    exit 2
  fi

  if [ "$rc" -ne 0 ] || [ "$new" -ne 0 ] || grep -q '^FINDING' "$log"; then
    echo
    echo
    echo "=== session $session stopped the soak: exit=$rc, $new sanitizer report(s) ==="
    grep -E '^(FINDING|  (before|after) |heaps|records|failures|out of room|shutdown)' "$log"
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
printf '        %s is %d MB, %d MB free, recreated %d time(s).\n' \
       "$db" "$(dbsize)" "$(room)" "$recycles"
