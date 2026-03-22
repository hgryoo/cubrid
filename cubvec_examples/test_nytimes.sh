#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
DB_NAME="${DB_NAME:-demodb}"
DB_USER="${DB_USER:-dba}"

TOPK="${TOPK:-10}"
QUERY_LIMIT="${QUERY_LIMIT:-0}"
PROGRESS_EVERY="${PROGRESS_EVERY:-100}"

HNSW_M="${HNSW_M:-24}"
HNSW_EF_CONSTRUCTION="${HNSW_EF_CONSTRUCTION:-200}"
HNSW_EF_SEARCH="${HNSW_EF_SEARCH:-40}"
HNSW_EF_SEARCH_VALUES="${HNSW_EF_SEARCH_VALUES:-10 20 40 80 120 200 400 800}"

RESET_DB="${RESET_DB:-1}"
LOAD_DATA="${LOAD_DATA:-1}"
BUILD_INDEX="${BUILD_INDEX:-1}"

DATASET_HDF5="${DATASET_HDF5:-$SCRIPT_DIR/nytimes-256-angular.hdf5}"
SCHEMA_FILE="${SCHEMA_FILE:-$SCRIPT_DIR/nytimes_256_angular_schema}"
OBJECT_FILE="${OBJECT_FILE:-$SCRIPT_DIR/nytimes_256_angular_object}"
OBJECT_NAN_MARKER="${OBJECT_NAN_MARKER:-${OBJECT_FILE}.nan_sanitized}"
OBJECT_LOAD_MARKER="${OBJECT_LOAD_MARKER:-${OBJECT_FILE}.load_sanitized}"
RESULT_CSV="${RESULT_CSV:-$SCRIPT_DIR/nytimes_ef_search_results.csv}"
RESULT_SVG="${RESULT_SVG:-$SCRIPT_DIR/nytimes_ef_search_results.svg}"
EXCLUDED_QUERY_FILE="${EXCLUDED_QUERY_FILE:-$SCRIPT_DIR/nytimes_excluded_queries.txt}"

log() {
  printf '[%s] %s\n' "$(date '+%F %T')" "$*"
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    printf 'missing command: %s\n' "$1" >&2
    exit 1
  }
}

sanitize_schema_file() {
  if [[ ! -f "$SCHEMA_FILE" ]]; then
    return
  fi

  if grep -q "/nytimes_256_angular_" "$SCHEMA_FILE"; then
    log "normalizing schema file: $SCHEMA_FILE"
    perl -0pi -e '
      s{CREATE TABLE \S*nytimes_256_angular_train\b}{CREATE TABLE nytimes_256_angular_train}g;
      s{CREATE TABLE \S*nytimes_256_angular_test\b}{CREATE TABLE nytimes_256_angular_test}g;
      s{CREATE TABLE \S*nytimes_256_angular_answer\b}{CREATE TABLE nytimes_256_angular_answer}g;
    ' "$SCHEMA_FILE"
  fi
}

sanitize_object_file() {
  if [[ ! -f "$OBJECT_FILE" ]]; then
    return
  fi

  if grep -q "/nytimes_256_angular_" "$OBJECT_FILE"; then
    log "normalizing object file header: $OBJECT_FILE"
    perl -0pi -e '
      s{^%id \S*nytimes_256_angular_train\b}{%id nytimes_256_angular_train}m;
      s{^%id \S*nytimes_256_angular_test\b}{%id nytimes_256_angular_test}m;
      s{^%id \S*nytimes_256_angular_answer\b}{%id nytimes_256_angular_answer}m;
      s{^%class \S*nytimes_256_angular_train\b}{%class nytimes_256_angular_train}m;
      s{^%class \S*nytimes_256_angular_test\b}{%class nytimes_256_angular_test}m;
      s{^%class \S*nytimes_256_angular_answer\b}{%class nytimes_256_angular_answer}m;
    ' "$OBJECT_FILE"
  fi

  if [[ ! -f "$OBJECT_NAN_MARKER" || "$OBJECT_NAN_MARKER" -ot "$OBJECT_FILE" ]]; then
    if rg -q '(^|[^[:alpha:]_])-?nan([^[:alpha:]_]|$)' "$OBJECT_FILE"; then
      log "replacing nan values with NULL in $OBJECT_FILE"
      perl -0pi -e 's{(?<![[:alpha:]_])-?nan(?![[:alpha:]_])}{NULL}gi' "$OBJECT_FILE"
    fi
    touch "$OBJECT_NAN_MARKER"
  fi

  if [[ ! -f "$OBJECT_LOAD_MARKER" || "$OBJECT_LOAD_MARKER" -ot "$OBJECT_FILE" ]]; then
    log "dropping malformed vector rows in $OBJECT_FILE"
    perl -i -ne '
      if (/^%class\s+(\S+)/) {
        $class = $1;
        print;
        next;
      }

      if (($class eq q{nytimes_256_angular_train} || $class eq q{nytimes_256_angular_test})
          && (/^(\d+)\s+'\[.*NULL.*\]'$/ || /^(\d+)\s+NULL$/)) {
        next;
      }

      print;
    ' "$OBJECT_FILE"
    touch "$OBJECT_LOAD_MARKER"
  fi
}

sql_capture() {
  local query="$1"
  csql -u "$DB_USER" -q -N --delimiter='|' "$DB_NAME" -c "$query"
}

sql_exec() {
  local query="$1"
  csql -u "$DB_USER" "$DB_NAME" -c "$query"
}

setup_demo_db() {
  if [[ -z "${CUBRID:-}" ]]; then
    printf 'CUBRID environment variable is not set.\n' >&2
    exit 1
  fi

  if ! grep -qx 'stored_procedure=no' "$CUBRID/conf/cubrid.conf"; then
    echo 'stored_procedure=no' >> "$CUBRID/conf/cubrid.conf"
  fi

  cubrid server stop "$DB_NAME" || true
  cubrid deletedb "$DB_NAME" || true

  (
    cd "$CUBRID/demo"
    ./make_cubrid_demo.sh
  )

  cubrid server start "$DB_NAME"
}

load_dataset() {
  if [[ -f "$SCHEMA_FILE" && -f "$OBJECT_FILE" ]]; then
    sanitize_schema_file
    sanitize_object_file
    log "loading dataset from $SCHEMA_FILE / $OBJECT_FILE"
    cubrid loaddb -s "$SCHEMA_FILE" -d "$OBJECT_FILE" -C -u "$DB_USER" "$DB_NAME" -v --no-statistics
    return
  fi

  if [[ -f "$DATASET_HDF5" ]]; then
    log "loading dataset from $DATASET_HDF5"
    cubrid loaddb -h "$DATASET_HDF5" -C -u "$DB_USER" "$DB_NAME" --no-statistics
    return
  fi

  printf 'dataset not found. checked: %s, %s, %s\n' \
    "$DATASET_HDF5" "$SCHEMA_FILE" "$OBJECT_FILE" >&2
  exit 1
}

build_index() {
  log "creating vector index (M=$HNSW_M, ef_construction=$HNSW_EF_CONSTRUCTION)"
  sql_exec "CREATE VECTOR INDEX vidx_nytimes_train ON nytimes_256_angular_train (vec COSINE) WITH (M = $HNSW_M, ef_construction = $HNSW_EF_CONSTRUCTION);"
}

prepare_query_ids() {
  local ids
  local excluded_ids

  # Queries whose vector is effectively invalid/null must be excluded.
  # They do not participate in vector indexing, and they should not be used
  # as ANN queries either. In this dataset, those cases appear as answer rows
  # with NULL neighbor_distance, so we keep only ids with at least one valid
  # ground-truth distance and record the excluded ids separately.
  excluded_ids="$(
    sql_capture "
      SELECT t.id
        FROM nytimes_256_angular_test t
       WHERE NOT EXISTS (
             SELECT 1
               FROM nytimes_256_angular_answer a
              WHERE a.id = t.id
                AND a.neighbor_distance IS NOT NULL
       )
       ORDER BY t.id;
    " \
      | awk -F'|' '/^-?[0-9]+([|].*)?$/ {print $1}'
  )"

  {
    printf '# Excluded nytimes queries\n'
    printf '# reason: null/invalid vector queries are excluded from indexing and should also be excluded from ANN recall evaluation.\n'
    printf '# criterion: no non-NULL ground-truth neighbor_distance exists in nytimes_256_angular_answer.\n'
    if [[ -n "$excluded_ids" ]]; then
      printf '%s\n' "$excluded_ids"
    fi
  } > "$EXCLUDED_QUERY_FILE"

  ids="$(
    sql_capture "
      SELECT t.id
        FROM nytimes_256_angular_test t
       WHERE EXISTS (
             SELECT 1
               FROM nytimes_256_angular_answer a
              WHERE a.id = t.id
                AND a.neighbor_distance IS NOT NULL
       )
       ORDER BY t.id;
    " \
      | awk -F'|' '/^-?[0-9]+([|].*)?$/ {print $1}'
  )"

  if [[ -z "$ids" ]]; then
    printf 'failed to fetch query ids from nytimes_256_angular_test\n' >&2
    exit 1
  fi

  EXCLUDED_QUERY_COUNT="$(grep -vc '^#' "$EXCLUDED_QUERY_FILE" || true)"
  log "excluded queries: ${EXCLUDED_QUERY_COUNT} (saved to $EXCLUDED_QUERY_FILE)"

  mapfile -t QUERY_IDS < <(printf '%s\n' "$ids")

  if (( QUERY_LIMIT > 0 )); then
    mapfile -t QUERY_IDS < <(printf '%s\n' "${QUERY_IDS[@]}" | head -n "$QUERY_LIMIT")
  fi

  if (( ${#QUERY_IDS[@]} == 0 )); then
    printf 'no query ids to process\n' >&2
    exit 1
  fi

  log "valid queries selected for recall: ${#QUERY_IDS[@]}"
}

measure_recall() {
  local total_hits=0
  local processed=0
  local start_ns
  local end_ns
  local elapsed_ns
  local elapsed_sec
  local recall
  local qps
  local sql_file
  local out_file
  local summary

  log "measuring recall@${TOPK} for ${#QUERY_IDS[@]} queries (hnsw_ef_search=$HNSW_EF_SEARCH)"
  start_ns="$(date +%s%N)"
  sql_file="$(mktemp /tmp/nytimes_measure_sql.XXXXXX)"
  out_file="$(mktemp /tmp/nytimes_measure_out.XXXXXX)"

  {
    printf "SET SYSTEM PARAMETERS 'hnsw_ef_search=%s';\n" "$HNSW_EF_SEARCH"
    printf "SET @k = %s;\n" "$TOPK"
    cat <<'EOF'
PREPARE ann FROM '
  SELECT 1, ?:0, id
    FROM (
      SELECT /*+ recompile no_parallel_heap_scan */ id
        FROM nytimes_256_angular_train
       ORDER BY vec <c> ?:1
       LIMIT ?:2
    ) ann
';
PREPARE gt FROM '
  SELECT 2, ?:0, neighbor_id
    FROM (
      SELECT neighbor_id
        FROM nytimes_256_angular_answer
       WHERE id = ?:0
       ORDER BY neighbor_distance
       LIMIT ?:1
    ) gt
';
EOF

    for qid in "${QUERY_IDS[@]}"; do
      printf "SET @qid = %s;\n" "$qid"
      printf "SET @v = (SELECT vec FROM nytimes_256_angular_test WHERE id = @qid);\n"
      printf "EXECUTE ann USING @qid, @v, @k;\n"
      printf "EXECUTE gt USING @qid, @k;\n"
    done
  } > "$sql_file"

  csql -u "$DB_USER" -q -N --delimiter='|' "$DB_NAME" -i "$sql_file" > "$out_file"

  summary="$(
    awk -F'|' -v topk="$TOPK" -v progress_every="$PROGRESS_EVERY" -v total_queries="${#QUERY_IDS[@]}" '
      function flush_query(   hits, i, n, partial, now, cmd) {
        if (curr_qid == "") {
          return
        }

        hits = 0
        n = split(ann_list, ann_arr, " ")
        for (i = 1; i <= n; i++) {
          if (ann_arr[i] != "" && (ann_arr[i] in gt_set)) {
            hits++
          }
        }

        total_hits += hits
        processed++

        if (progress_every > 0 && processed % progress_every == 0) {
          partial = total_hits / (processed * topk)
          cmd = "date \"+%F %T\""
          cmd | getline now
          close(cmd)
          printf "[%s] progress: %d/%d queries, partial recall@%d=%.6f\n", now, processed, total_queries, topk, partial > "/dev/stderr"
        }

        delete gt_set
        ann_list = ""
        curr_qid = ""
      }

      $1 ~ /^[12]$/ && $2 ~ /^-?[0-9]+$/ && $3 ~ /^-?[0-9]+$/ {
        qid = $2 + 0
        rid = $3 + 0

        if (curr_qid != "" && qid != curr_qid) {
          flush_query()
        }

        if (curr_qid == "") {
          curr_qid = qid
        }

        if ($1 == 1) {
          ann_list = ann_list " " rid
        } else {
          gt_set[rid] = 1
        }
      }

      END {
        flush_query()
        printf "%d|%d\n", processed, total_hits
      }
    ' "$out_file"
  )"

  processed="${summary%%|*}"
  total_hits="${summary##*|}"

  rm -f "$sql_file" "$out_file"

  end_ns="$(date +%s%N)"
  elapsed_ns=$((end_ns - start_ns))
  elapsed_sec="$(awk -v ns="$elapsed_ns" 'BEGIN { printf "%.6f", ns / 1000000000 }')"
  recall="$(awk -v hits="$total_hits" -v total="$((processed * TOPK))" 'BEGIN { printf "%.6f", hits / total }')"
  qps="$(awk -v queries="$processed" -v ns="$elapsed_ns" 'BEGIN { printf "%.3f", queries / (ns / 1000000000) }')"

  LAST_QUERIES="$processed"
  LAST_TOTAL_HITS="$total_hits"
  LAST_RECALL="$recall"
  LAST_ELAPSED_SEC="$elapsed_sec"
  LAST_QPS="$qps"

  printf '\n'
  printf 'dataset=nytimes-256-angular\n'
  printf 'queries=%d\n' "$processed"
  printf 'topk=%d\n' "$TOPK"
  printf 'hnsw_m=%d\n' "$HNSW_M"
  printf 'hnsw_ef_construction=%d\n' "$HNSW_EF_CONSTRUCTION"
  printf 'hnsw_ef_search=%d\n' "$HNSW_EF_SEARCH"
  printf 'excluded_queries=%s\n' "${EXCLUDED_QUERY_COUNT:-0}"
  printf 'total_hits=%d\n' "$total_hits"
  printf 'recall@%d=%s\n' "$TOPK" "$recall"
  printf 'elapsed_sec=%s\n' "$elapsed_sec"
  printf 'qps=%s\n' "$qps"
}

write_result_csv_header() {
  cat > "$RESULT_CSV" <<EOF
dataset,queries,excluded_queries,topk,hnsw_m,hnsw_ef_construction,hnsw_ef_search,total_hits,recall,elapsed_sec,qps
EOF
}

append_result_csv() {
  printf 'nytimes-256-angular,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
    "$LAST_QUERIES" \
    "${EXCLUDED_QUERY_COUNT:-0}" \
    "$TOPK" \
    "$HNSW_M" \
    "$HNSW_EF_CONSTRUCTION" \
    "$HNSW_EF_SEARCH" \
    "$LAST_TOTAL_HITS" \
    "$LAST_RECALL" \
    "$LAST_ELAPSED_SEC" \
    "$LAST_QPS" >> "$RESULT_CSV"
}

render_result_svg() {
  python3 - "$RESULT_CSV" "$RESULT_SVG" <<'PY'
import csv
import math
import sys

csv_path, svg_path = sys.argv[1], sys.argv[2]
rows = []
with open(csv_path, newline="") as f:
    for row in csv.DictReader(f):
        rows.append({
            "ef_search": int(row["hnsw_ef_search"]),
            "recall": float(row["recall"]),
            "qps": float(row["qps"]),
        })

if not rows:
    raise SystemExit("no rows to plot")

rows.sort(key=lambda r: r["ef_search"])
efs = [r["ef_search"] for r in rows]
recalls = [r["recall"] for r in rows]
qpss = [r["qps"] for r in rows]

width, height = 980, 620
left, right, top, bottom = 90, 90, 50, 70
plot_w = width - left - right
plot_h = height - top - bottom

min_x = min(efs)
max_x = max(efs)
min_recall = min(recalls)
max_recall = max(recalls)
min_qps = min(qpss)
max_qps = max(qpss)

if min_x == max_x:
    max_x += 1
if min_recall == max_recall:
    max_recall += 0.01
if min_qps == max_qps:
    max_qps += 1.0

recall_pad = max(0.005, (max_recall - min_recall) * 0.08)
qps_pad = max(1.0, (max_qps - min_qps) * 0.08)
min_recall = max(0.0, min_recall - recall_pad)
max_recall = min(1.0, max_recall + recall_pad)
min_qps = max(0.0, min_qps - qps_pad)
max_qps = max_qps + qps_pad

def x_of(v):
    return left + (v - min_x) / (max_x - min_x) * plot_w

def y_recall(v):
    return top + plot_h - (v - min_recall) / (max_recall - min_recall) * plot_h

def y_qps(v):
    return top + plot_h - (v - min_qps) / (max_qps - min_qps) * plot_h

def line_path(values, y_fn):
    return " ".join(
        ("M" if i == 0 else "L") + f" {x_of(x):.2f} {y_fn(y):.2f}"
        for i, (x, y) in enumerate(values)
    )

recall_path = line_path(list(zip(efs, recalls)), y_recall)
qps_path = line_path(list(zip(efs, qpss)), y_qps)

grid_lines = []
for i in range(6):
    y = top + plot_h * i / 5
    grid_lines.append(f'<line x1="{left}" y1="{y:.2f}" x2="{left + plot_w}" y2="{y:.2f}" stroke="#d7dde5" stroke-width="1"/>')

x_ticks = []
for ef in efs:
    x = x_of(ef)
    x_ticks.append(f'<line x1="{x:.2f}" y1="{top + plot_h}" x2="{x:.2f}" y2="{top + plot_h + 6}" stroke="#333"/>')
    x_ticks.append(f'<text x="{x:.2f}" y="{top + plot_h + 24}" text-anchor="middle" font-size="12" fill="#222">{ef}</text>')

left_ticks = []
for i in range(6):
    value = min_recall + (max_recall - min_recall) * (5 - i) / 5
    y = top + plot_h * i / 5
    left_ticks.append(f'<line x1="{left - 6}" y1="{y:.2f}" x2="{left}" y2="{y:.2f}" stroke="#333"/>')
    left_ticks.append(f'<text x="{left - 10}" y="{y + 4:.2f}" text-anchor="end" font-size="12" fill="#0f5c2e">{value:.3f}</text>')

right_ticks = []
for i in range(6):
    value = min_qps + (max_qps - min_qps) * (5 - i) / 5
    y = top + plot_h * i / 5
    right_ticks.append(f'<line x1="{left + plot_w}" y1="{y:.2f}" x2="{left + plot_w + 6}" y2="{y:.2f}" stroke="#333"/>')
    right_ticks.append(f'<text x="{left + plot_w + 10}" y="{y + 4:.2f}" text-anchor="start" font-size="12" fill="#8a4b00">{value:.1f}</text>')

recall_points = "\n".join(
    f'<circle cx="{x_of(r["ef_search"]):.2f}" cy="{y_recall(r["recall"]):.2f}" r="4" fill="#17803d"/>'
    for r in rows
)
qps_points = "\n".join(
    f'<circle cx="{x_of(r["ef_search"]):.2f}" cy="{y_qps(r["qps"]):.2f}" r="4" fill="#d97706"/>'
    for r in rows
)

svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
<rect width="100%" height="100%" fill="#fcfcfb"/>
<text x="{left}" y="28" font-size="22" font-weight="700" fill="#111">nytimes-256-angular ef_search sweep</text>
<text x="{left}" y="46" font-size="13" fill="#555">Recall and QPS by hnsw_ef_search</text>
{''.join(grid_lines)}
<line x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_h}" stroke="#333" stroke-width="1.2"/>
<line x1="{left + plot_w}" y1="{top}" x2="{left + plot_w}" y2="{top + plot_h}" stroke="#333" stroke-width="1.2"/>
<line x1="{left}" y1="{top + plot_h}" x2="{left + plot_w}" y2="{top + plot_h}" stroke="#333" stroke-width="1.2"/>
{''.join(x_ticks)}
{''.join(left_ticks)}
{''.join(right_ticks)}
<path d="{recall_path}" fill="none" stroke="#17803d" stroke-width="3"/>
<path d="{qps_path}" fill="none" stroke="#d97706" stroke-width="3"/>
{recall_points}
{qps_points}
<text x="{left + plot_w / 2:.2f}" y="{height - 20}" text-anchor="middle" font-size="14" fill="#222">hnsw_ef_search</text>
<text x="24" y="{top + plot_h / 2:.2f}" text-anchor="middle" font-size="14" fill="#0f5c2e" transform="rotate(-90 24 {top + plot_h / 2:.2f})">Recall@K</text>
<text x="{width - 20}" y="{top + plot_h / 2:.2f}" text-anchor="middle" font-size="14" fill="#8a4b00" transform="rotate(90 {width - 20} {top + plot_h / 2:.2f})">QPS</text>
<rect x="{left + 10}" y="{top + 10}" width="14" height="14" fill="#17803d"/>
<text x="{left + 32}" y="{top + 22}" font-size="13" fill="#222">Recall</text>
<rect x="{left + 110}" y="{top + 10}" width="14" height="14" fill="#d97706"/>
<text x="{left + 132}" y="{top + 22}" font-size="13" fill="#222">QPS</text>
</svg>'''

with open(svg_path, "w", encoding="utf-8") as f:
    f.write(svg)
PY
}

run_ef_search_experiments() {
  local ef

  write_result_csv_header

  for ef in $HNSW_EF_SEARCH_VALUES; do
    HNSW_EF_SEARCH="$ef"
    measure_recall
    append_result_csv
  done

  render_result_svg
  log "saved ef_search results to $RESULT_CSV"
  log "saved ef_search graph to $RESULT_SVG"
  log "saved excluded query list to $EXCLUDED_QUERY_FILE"
}

main() {
  require_cmd cubrid
  require_cmd csql
  require_cmd awk
  require_cmd perl
  require_cmd rg
  require_cmd python3

  if (( RESET_DB == 1 )); then
    log "resetting $DB_NAME"
    setup_demo_db
  fi

  if (( LOAD_DATA == 1 )); then
    load_dataset
  fi

  if (( BUILD_INDEX == 1 )); then
    build_index
  fi

  prepare_query_ids
  run_ef_search_experiments
}

main "$@"
