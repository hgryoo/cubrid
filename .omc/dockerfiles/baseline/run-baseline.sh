#!/usr/bin/env bash
# Step 2 baseline measurement loop.
#
# Runs N fresh-DB cold boots of cub_server, measuring wall-clock from
# `cubrid server start` invocation to the "SERVER STARTED" log line,
# and extracts the per-phase dump emitted by the Step 1 boot_perf_trace
# instrumentation into a markdown report.
#
# Expected container runtime flags:
#   --cpus=1           single-core constraint (matches target rig)
#   --memory=20g       headroom for 16GB data_buffer_size
#   --cap-add=SYS_ADMIN  required for drop_caches between runs
#   -v host:/data      bind-mount for DB files and results

set -eu

RUNS="${BASELINE_RUNS:-10}"
OUTFILE="/data/baseline-phase-timing.md"
DB_NAME="boot_baseline"
DB_ROOT="/data/databases"
CONF_SRC="/opt/cubrid/conf/cubrid.conf.template"
READY_MARK="SERVER STARTED"
TIMEOUT_SEC="${BASELINE_BOOT_TIMEOUT:-120}"

mkdir -p "$DB_ROOT/conf" "$DB_ROOT/db" "$DB_ROOT/log"
cp "$CONF_SRC" "$DB_ROOT/conf/cubrid.conf"
export CUBRID_DATABASES="$DB_ROOT/db"

drop_caches () {
  sync
  if [ -w /proc/sys/vm/drop_caches ]; then
    echo 3 > /proc/sys/vm/drop_caches 2>/dev/null \
      || echo "WARN: drop_caches failed (need --cap-add=SYS_ADMIN)" >&2
  else
    echo "WARN: /proc/sys/vm/drop_caches not writable (need --cap-add=SYS_ADMIN)" >&2
  fi
}

reset_db () {
  # Stop any stray server, wipe the databases directory, recreate fresh.
  cubrid server stop "$DB_NAME" >/dev/null 2>&1 || true
  rm -rf "$DB_ROOT/db" "$DB_ROOT/log"
  mkdir -p "$DB_ROOT/db" "$DB_ROOT/log"
  (
    cd "$DB_ROOT/db"
    cubrid createdb --db-volume-size=512M --log-volume-size=512M \
                    "$DB_NAME" en_US.utf8 >/dev/null
  )
}

wait_for_ready () {
  local logfile="$1" start_ts="$2" end_ts deadline
  deadline=$(( start_ts + TIMEOUT_SEC ))
  while :; do
    if [ -f "$logfile" ] && grep -q "$READY_MARK" "$logfile" 2>/dev/null; then
      end_ts=$(date +%s.%N)
      echo "$end_ts"
      return 0
    fi
    if [ "$(date +%s)" -ge "$deadline" ]; then
      echo "TIMEOUT" >&2
      return 1
    fi
    sleep 0.05
  done
}

extract_phase_dump () {
  local logfile="$1"
  # Dump header is the first "BOOT_PHASE" block emitted before SERVER STARTED.
  # Print from the last BOOT_PERF line back through its contiguous table rows.
  awk '
    /boot_perf_trace|BOOT_PHASE|boot_phase_dump/ { capture=1 }
    capture { print }
    /SERVER STARTED/ { capture=0 }
  ' "$logfile" 2>/dev/null || true
}

{
  echo "# CUBRID Fresh-Boot Baseline (Step 2)"
  echo
  echo "- Runs: $RUNS"
  echo "- Container: 1 vCPU, 20G mem, --cap-add=SYS_ADMIN"
  echo "- data_buffer_size=16G (fixed); er_log_debug=yes; boot_perf_trace=yes"
  echo "- Scenario: fresh DB, cold page cache (drop_caches between runs)"
  echo "- Timer: wall-clock from \`cubrid server start\` to \"$READY_MARK\" log line"
  echo "- Generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo
  echo "## Runs"
  echo
} > "$OUTFILE"

for i in $(seq 1 "$RUNS"); do
  echo "[baseline] run $i / $RUNS" >&2

  reset_db
  drop_caches

  LOGFILE="$DB_ROOT/log/${DB_NAME}_server_boot.err"
  rm -f "$LOGFILE"

  START=$(date +%s.%N)
  cubrid server start "$DB_NAME" >/dev/null 2>&1 &
  START_PID=$!

  if END=$(wait_for_ready "$LOGFILE" "${START%.*}"); then
    ELAPSED=$(awk -v s="$START" -v e="$END" 'BEGIN { printf "%.3f", e - s }')
    STATUS="ok"
  else
    ELAPSED="timeout"
    STATUS="timeout"
  fi

  wait "$START_PID" 2>/dev/null || true

  {
    echo "### Run $i — elapsed: ${ELAPSED}s (${STATUS})"
    echo
    echo '```'
    extract_phase_dump "$LOGFILE"
    echo '```'
    echo
  } >> "$OUTFILE"

  cubrid server stop "$DB_NAME" >/dev/null 2>&1 || true
  sleep 1
done

{
  echo "## Classification hint"
  echo
  echo "Inspect the sorted phase dumps above and tag the bottleneck:"
  echo "- **single-dominator**: one phase > 50% of total elapsed → Step 4 target"
  echo "- **multi-dominator**: 2-3 phases together > 60% → Steps 3a + 3b"
  echo "- **long-tail-HALT**: no single phase > 30%, many > 200ms → re-scope"
} >> "$OUTFILE"

echo "[baseline] wrote $OUTFILE" >&2
