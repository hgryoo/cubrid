#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DATA_DIR="$SCRIPT_DIR"

# User-facing settings
DB_NAME="${DB_NAME:-demodb}"
DB_USER="${DB_USER:-dba}"
DB_PASS="${DB_PASS:-}"
DATASET_HDF5="${DATASET_HDF5:-$SCRIPT_DIR/nytimes-256-angular.hdf5}"
DATASET_DOWNLOAD_URL="${DATASET_DOWNLOAD_URL:-}"

TOPK="${TOPK:-10}"
PROGRESS_EVERY="${PROGRESS_EVERY:-100}"
TRAIN_ROW_LIMIT="${TRAIN_ROW_LIMIT:-}"

HNSW_M="${HNSW_M:-24}"
HNSW_EF_CONSTRUCTION="${HNSW_EF_CONSTRUCTION:-200}"
HNSW_EF_SEARCH_VALUES="${HNSW_EF_SEARCH_VALUES:-200 400}"
ANN_SINGLE_CORE_EXPERIMENT="${ANN_SINGLE_CORE_EXPERIMENT:-0}"
ANN_EXPERIMENT_MAX_CLIENTS="${ANN_EXPERIMENT_MAX_CLIENTS:-1}"
ANN_EXPERIMENT_THREAD_CORE_COUNT="${ANN_EXPERIMENT_THREAD_CORE_COUNT:-1}"
ANN_EXPERIMENT_PARALLELISM="${ANN_EXPERIMENT_PARALLELISM:-0}"
ANN_EXPERIMENT_MAX_PARALLEL_WORKERS="${ANN_EXPERIMENT_MAX_PARALLEL_WORKERS:-0}"
ANN_TASKSET_ENABLE="${ANN_TASKSET_ENABLE:-0}"
ANN_TASKSET_CPU="${ANN_TASKSET_CPU:-0}"
BROKER_NAME="${BROKER_NAME:-broker1}"
BROKER_PORT="${BROKER_PORT:-33000}"
BROKER_SINGLE_CAS_ENABLE="${BROKER_SINGLE_CAS_ENABLE:-1}"
ANN_HNSW_DEBUG_ENABLE="${ANN_HNSW_DEBUG_ENABLE:-1}"

# Internal derived paths and per-run state
DATASET_FILENAME="$(basename -- "$DATASET_HDF5")"
DATASET_NAME="${DATASET_FILENAME%.hdf5}"
DATASET_FILE_STEM="${DATASET_NAME//-/_}"
SCHEMA_FILE="$DATA_DIR/${DATASET_FILE_STEM}_schema"
OBJECT_FILE="$DATA_DIR/${DATASET_FILE_STEM}_object"
OBJECT_NAN_MARKER="$SCRIPT_DIR/${DATASET_FILE_STEM}_object.nan_sanitized"
OBJECT_LOAD_MARKER="$SCRIPT_DIR/${DATASET_FILE_STEM}_object.load_sanitized"
RESULT_CSV="$SCRIPT_DIR/${DATASET_NAME}_ef_search_results_cci.csv"
RESULT_SVG="$SCRIPT_DIR/${DATASET_NAME}_ef_search_results_cci.svg"
EXCLUDED_QUERY_FILE="$SCRIPT_DIR/${DATASET_NAME}_excluded_queries.txt"
QUERY_ID_FILE="$SCRIPT_DIR/${DATASET_NAME}_query_ids.txt"
QUERY_ID_CACHE_MARKER="$SCRIPT_DIR/${DATASET_NAME}_query_ids.cache_ready"
GT_CACHE_FILE="$SCRIPT_DIR/${DATASET_NAME}_gt_topk${TOPK}.out"
GT_CACHE_MARKER="$SCRIPT_DIR/${DATASET_NAME}_gt_topk${TOPK}.cache_ready"
PERF_OUTPUT_DIR="$SCRIPT_DIR/perf_${DATASET_FILE_STEM}"
CCI_RUNNER_SRC="$SCRIPT_DIR/cci_ann_runner.c"
CCI_RUNNER_BIN="$SCRIPT_DIR/cci_ann_runner"
HNSW_EF_SEARCH=""
LAST_QUERY_LABEL=""

PERF_ENABLE="${PERF_ENABLE:-0}"
PERF_STAT_ENABLE="${PERF_STAT_ENABLE:-1}"
PERF_TARGETS="${PERF_TARGETS:-cci cub_cas cub_server}"
PERF_STAT_EVENTS="${PERF_STAT_EVENTS:-task-clock,cycles,instructions,topdown-fe-bound,branches,branch-misses,L1-dcache-loads,L1-dcache-load-misses,L1-icache-loads,L1-icache-load-misses,dTLB-loads,dTLB-load-misses,iTLB-loads,iTLB-load-misses,context-switches,cpu-migrations,page-faults}"
PERF_RECORD_TARGETS="${PERF_RECORD_TARGETS:-}"
PERF_RECORD_PROFILE="${PERF_RECORD_PROFILE:-}"
PERF_PROFILES="${PERF_PROFILES:-}"
PERF_RECORD_EVENT="${PERF_RECORD_EVENT:-}"
PERF_RECORD_FREQ="${PERF_RECORD_FREQ:-99}"
PERF_RECORD_PERIOD="${PERF_RECORD_PERIOD:-}"
PERF_CALL_GRAPH="${PERF_CALL_GRAPH:-fp}"
PERF_FLAMEGRAPH="${PERF_FLAMEGRAPH:-1}"
FINAL_RESULTS_ENABLE="${FINAL_RESULTS_ENABLE:-1}"
FINAL_RESULTS_DIR="${FINAL_RESULTS_DIR:-$SCRIPT_DIR/final_results_${DATASET_FILE_STEM}_cci}"
SEGMENT_PROFILE_ENABLE="${SEGMENT_PROFILE_ENABLE:-0}"
SEGMENT_PROFILE_DIR="${SEGMENT_PROFILE_DIR:-$SCRIPT_DIR/segment_profiles_cci_${DATASET_FILE_STEM}}"
CAS_TIMESTAMP_PROFILE_ENABLE="${CAS_TIMESTAMP_PROFILE_ENABLE:-0}"
CAS_TIMESTAMP_PROFILE_DIR="${CAS_TIMESTAMP_PROFILE_DIR:-$SEGMENT_PROFILE_DIR/cub_cas_timestamps}"
QUERY_SAMPLE_LIMIT="${QUERY_SAMPLE_LIMIT:-0}"
FLAMEGRAPH_DIR="${FLAMEGRAPH_DIR:-$SCRIPT_DIR/flame_graph}"
STACKCOLLAPSE_PERF="${STACKCOLLAPSE_PERF:-}"
FLAMEGRAPH_PL="${FLAMEGRAPH_PL:-}"
FLAMEGRAPH_REPO_URL="${FLAMEGRAPH_REPO_URL:-https://github.com/brendangregg/FlameGraph.git}"
FLAMEGRAPH_TARBALL_URL="${FLAMEGRAPH_TARBALL_URL:-https://github.com/brendangregg/FlameGraph/archive/refs/heads/master.tar.gz}"
PERF_STAT_EVENTS_RESOLVED=""

log() {
  printf '[%s] %s\n' "$(date '+%F %T')" "$*"
}

is_hnsw_debug_enabled_in_conf() {
  local conf_file="${CUBRID}/conf/cubrid.conf"

  if [[ ! -f "$conf_file" ]]; then
    return 1
  fi

  awk -F'=' '
    /^[[:space:]]*#/ { next }
    /^[[:space:]]*hnsw_debug[[:space:]]*=/ {
      value=$2
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
      value=tolower(value)
      if (value == "yes" || value == "on" || value == "true" || value == "1") {
        found=1
      }
    }
    END { exit found ? 0 : 1 }
  ' "$conf_file"
}

shell_quote() {
  printf '%q' "$1"
}

git_branch_or_detached() {
  local repo_dir="$1"
  local branch=""

  branch="$(git -C "$repo_dir" branch --show-current 2>/dev/null || true)"
  if [[ -n "$branch" ]]; then
    printf '%s\n' "$branch"
    return
  fi

  printf 'detached\n'
}

git_dirty_flag() {
  local repo_dir="$1"

  if [[ -n "$(git -C "$repo_dir" status --porcelain 2>/dev/null || true)" ]]; then
    printf 'yes\n'
    return
  fi

  printf 'no\n'
}

append_repro_assignment_if_changed() {
  local -n parts_ref="$1"
  local name="$2"
  local value="$3"
  local default="$4"
  local quoted=""

  if [[ "$value" == "$default" ]]; then
    return
  fi

  printf -v quoted '%q' "$value"
  parts_ref+=("${name}=${quoted}")
}

build_reproduction_command() {
  local parts=()
  local script_rel="./ann_benchmarks_local/test_ann_cci.sh"

  append_repro_assignment_if_changed parts "DB_NAME" "$DB_NAME" "demodb"
  append_repro_assignment_if_changed parts "DB_USER" "$DB_USER" "dba"
  append_repro_assignment_if_changed parts "DB_PASS" "$DB_PASS" ""
  append_repro_assignment_if_changed parts "DATASET_HDF5" "$DATASET_HDF5" "$SCRIPT_DIR/nytimes-256-angular.hdf5"
  append_repro_assignment_if_changed parts "DATASET_DOWNLOAD_URL" "$DATASET_DOWNLOAD_URL" ""
  append_repro_assignment_if_changed parts "TOPK" "$TOPK" "10"
  append_repro_assignment_if_changed parts "PROGRESS_EVERY" "$PROGRESS_EVERY" "100"
  append_repro_assignment_if_changed parts "TRAIN_ROW_LIMIT" "$TRAIN_ROW_LIMIT" ""
  append_repro_assignment_if_changed parts "HNSW_M" "$HNSW_M" "24"
  append_repro_assignment_if_changed parts "HNSW_EF_CONSTRUCTION" "$HNSW_EF_CONSTRUCTION" "200"
  append_repro_assignment_if_changed parts "HNSW_EF_SEARCH_VALUES" "$HNSW_EF_SEARCH_VALUES" "200 400"
  append_repro_assignment_if_changed parts "ANN_SINGLE_CORE_EXPERIMENT" "$ANN_SINGLE_CORE_EXPERIMENT" "0"
  append_repro_assignment_if_changed parts "ANN_EXPERIMENT_MAX_CLIENTS" "$ANN_EXPERIMENT_MAX_CLIENTS" "1"
  append_repro_assignment_if_changed parts "ANN_EXPERIMENT_THREAD_CORE_COUNT" "$ANN_EXPERIMENT_THREAD_CORE_COUNT" "1"
  append_repro_assignment_if_changed parts "ANN_EXPERIMENT_PARALLELISM" "$ANN_EXPERIMENT_PARALLELISM" "0"
  append_repro_assignment_if_changed parts "ANN_EXPERIMENT_MAX_PARALLEL_WORKERS" "$ANN_EXPERIMENT_MAX_PARALLEL_WORKERS" "0"
  append_repro_assignment_if_changed parts "ANN_TASKSET_ENABLE" "$ANN_TASKSET_ENABLE" "0"
  append_repro_assignment_if_changed parts "ANN_TASKSET_CPU" "$ANN_TASKSET_CPU" "0"
  append_repro_assignment_if_changed parts "BROKER_NAME" "$BROKER_NAME" "broker1"
  append_repro_assignment_if_changed parts "BROKER_PORT" "$BROKER_PORT" "33000"
  append_repro_assignment_if_changed parts "BROKER_SINGLE_CAS_ENABLE" "$BROKER_SINGLE_CAS_ENABLE" "1"
  append_repro_assignment_if_changed parts "ANN_HNSW_DEBUG_ENABLE" "$ANN_HNSW_DEBUG_ENABLE" "1"
append_repro_assignment_if_changed parts "PERF_ENABLE" "$PERF_ENABLE" "0"
  append_repro_assignment_if_changed parts "PERF_STAT_ENABLE" "$PERF_STAT_ENABLE" "1"
  append_repro_assignment_if_changed parts "PERF_TARGETS" "$PERF_TARGETS" "cci cub_cas cub_server"
  append_repro_assignment_if_changed parts "PERF_STAT_EVENTS" "$PERF_STAT_EVENTS" "task-clock,cycles,instructions,topdown-fe-bound,branches,branch-misses,L1-dcache-loads,L1-dcache-load-misses,L1-icache-loads,L1-icache-load-misses,dTLB-loads,dTLB-load-misses,iTLB-loads,iTLB-load-misses,context-switches,cpu-migrations,page-faults"
  append_repro_assignment_if_changed parts "PERF_RECORD_TARGETS" "$PERF_RECORD_TARGETS" ""
  append_repro_assignment_if_changed parts "PERF_RECORD_PROFILE" "$PERF_RECORD_PROFILE" ""
  append_repro_assignment_if_changed parts "PERF_PROFILES" "$PERF_PROFILES" ""
  append_repro_assignment_if_changed parts "PERF_RECORD_EVENT" "$PERF_RECORD_EVENT" ""
  append_repro_assignment_if_changed parts "PERF_RECORD_FREQ" "$PERF_RECORD_FREQ" "99"
  append_repro_assignment_if_changed parts "PERF_RECORD_PERIOD" "$PERF_RECORD_PERIOD" ""
  append_repro_assignment_if_changed parts "PERF_CALL_GRAPH" "$PERF_CALL_GRAPH" "fp"
  append_repro_assignment_if_changed parts "PERF_FLAMEGRAPH" "$PERF_FLAMEGRAPH" "1"
  append_repro_assignment_if_changed parts "FINAL_RESULTS_ENABLE" "$FINAL_RESULTS_ENABLE" "1"
  append_repro_assignment_if_changed parts "FINAL_RESULTS_DIR" "$FINAL_RESULTS_DIR" "$SCRIPT_DIR/final_results_${DATASET_FILE_STEM}_cci"
  append_repro_assignment_if_changed parts "SEGMENT_PROFILE_ENABLE" "$SEGMENT_PROFILE_ENABLE" "0"
  append_repro_assignment_if_changed parts "SEGMENT_PROFILE_DIR" "$SEGMENT_PROFILE_DIR" "$SCRIPT_DIR/segment_profiles_cci_${DATASET_FILE_STEM}"
  append_repro_assignment_if_changed parts "CAS_TIMESTAMP_PROFILE_ENABLE" "$CAS_TIMESTAMP_PROFILE_ENABLE" "0"
  append_repro_assignment_if_changed parts "CAS_TIMESTAMP_PROFILE_DIR" "$CAS_TIMESTAMP_PROFILE_DIR" "$SEGMENT_PROFILE_DIR/cub_cas_timestamps"
  append_repro_assignment_if_changed parts "QUERY_SAMPLE_LIMIT" "$QUERY_SAMPLE_LIMIT" "0"
  append_repro_assignment_if_changed parts "FLAMEGRAPH_DIR" "$FLAMEGRAPH_DIR" "$SCRIPT_DIR/flame_graph"
  append_repro_assignment_if_changed parts "STACKCOLLAPSE_PERF" "$STACKCOLLAPSE_PERF" ""
  append_repro_assignment_if_changed parts "FLAMEGRAPH_PL" "$FLAMEGRAPH_PL" ""
  append_repro_assignment_if_changed parts "FLAMEGRAPH_REPO_URL" "$FLAMEGRAPH_REPO_URL" "https://github.com/brendangregg/FlameGraph.git"
  append_repro_assignment_if_changed parts "FLAMEGRAPH_TARBALL_URL" "$FLAMEGRAPH_TARBALL_URL" "https://github.com/brendangregg/FlameGraph/archive/refs/heads/master.tar.gz"

  if ((${#parts[@]} > 0)); then
    printf '%s ' "${parts[@]}"
  fi
  printf '%s\n' "$script_rel"
}

write_env_manifest_tsv() {
  local out_file="$1"

  cat >"$out_file" <<EOF
name	default	value	description
DB_NAME	demodb	$DB_NAME	Database name
DB_USER	dba	$DB_USER	Database user
DB_PASS		$DB_PASS	Database password
DATASET_HDF5	$SCRIPT_DIR/nytimes-256-angular.hdf5	$DATASET_HDF5	Input HDF5 dataset path
DATASET_DOWNLOAD_URL		$DATASET_DOWNLOAD_URL	Optional dataset download URL override
TOPK	10	$TOPK	Top-k used for ANN search and recall
PROGRESS_EVERY	100	$PROGRESS_EVERY	Progress log interval in query loop
TRAIN_ROW_LIMIT		$TRAIN_ROW_LIMIT	Optional train row limit for reduced runs
HNSW_M	24	$HNSW_M	HNSW m parameter
HNSW_EF_CONSTRUCTION	200	$HNSW_EF_CONSTRUCTION	HNSW ef_construction parameter
HNSW_EF_SEARCH_VALUES	200 400	$HNSW_EF_SEARCH_VALUES	Space-separated ef_search sweep values
ANN_SINGLE_CORE_EXPERIMENT	0	$ANN_SINGLE_CORE_EXPERIMENT	Restart DB with single-core experiment settings before measurement
ANN_EXPERIMENT_MAX_CLIENTS	1	$ANN_EXPERIMENT_MAX_CLIENTS	max_clients override for single-core experiment
ANN_EXPERIMENT_THREAD_CORE_COUNT	1	$ANN_EXPERIMENT_THREAD_CORE_COUNT	thread_core_count override for single-core experiment
ANN_EXPERIMENT_PARALLELISM	0	$ANN_EXPERIMENT_PARALLELISM	parallelism override for single-core experiment
ANN_EXPERIMENT_MAX_PARALLEL_WORKERS	0	$ANN_EXPERIMENT_MAX_PARALLEL_WORKERS	max_parallel_workers override for single-core experiment
ANN_TASKSET_ENABLE	0	$ANN_TASKSET_ENABLE	Run CUBRID-related commands under taskset when enabled
ANN_TASKSET_CPU	0	$ANN_TASKSET_CPU	CPU core used by taskset
BROKER_NAME	broker1	$BROKER_NAME	CCI broker name
BROKER_PORT	33000	$BROKER_PORT	CCI broker port
BROKER_SINGLE_CAS_ENABLE	1	$BROKER_SINGLE_CAS_ENABLE	Force broker1 to use exactly one CAS for profiling
ANN_HNSW_DEBUG_ENABLE	1	$ANN_HNSW_DEBUG_ENABLE	Set hnsw_debug=1 for profiling runs when enabled
PERF_ENABLE	0	$PERF_ENABLE	Enable perf collection
PERF_STAT_ENABLE	1	$PERF_STAT_ENABLE	Enable perf stat collection
PERF_TARGETS	cci cub_cas cub_server	$PERF_TARGETS	Targets for perf stat collection
PERF_STAT_EVENTS	task-clock,cycles,instructions,topdown-fe-bound,branches,branch-misses,L1-dcache-loads,L1-dcache-load-misses,L1-icache-loads,L1-icache-load-misses,dTLB-loads,dTLB-load-misses,iTLB-loads,iTLB-load-misses,context-switches,cpu-migrations,page-faults	$PERF_STAT_EVENTS	perf stat event list
PERF_RECORD_TARGETS		$PERF_RECORD_TARGETS	Targets for perf record collection
PERF_RECORD_PROFILE		$PERF_RECORD_PROFILE	Single perf record profile selector
PERF_PROFILES		$PERF_PROFILES	Space-separated perf record profiles
PERF_RECORD_EVENT		$PERF_RECORD_EVENT	perf record event override
PERF_RECORD_FREQ	99	$PERF_RECORD_FREQ	perf record frequency
PERF_RECORD_PERIOD		$PERF_RECORD_PERIOD	perf record period override
PERF_CALL_GRAPH	fp	$PERF_CALL_GRAPH	perf call graph mode
PERF_FLAMEGRAPH	1	$PERF_FLAMEGRAPH	Generate flamegraphs from perf data
FINAL_RESULTS_ENABLE	1	$FINAL_RESULTS_ENABLE	Generate the final results bundle
FINAL_RESULTS_DIR	$SCRIPT_DIR/final_results_${DATASET_FILE_STEM}_cci	$FINAL_RESULTS_DIR	Output directory for final bundled artifacts
SEGMENT_PROFILE_ENABLE	0	$SEGMENT_PROFILE_ENABLE	Enable CCI segment breakdown collection
SEGMENT_PROFILE_DIR	$SCRIPT_DIR/segment_profiles_cci_${DATASET_FILE_STEM}	$SEGMENT_PROFILE_DIR	Output directory for segment profile artifacts
CAS_TIMESTAMP_PROFILE_ENABLE	0	$CAS_TIMESTAMP_PROFILE_ENABLE	Enable cub_cas timestamp profiling
CAS_TIMESTAMP_PROFILE_DIR	$SEGMENT_PROFILE_DIR/cub_cas_timestamps	$CAS_TIMESTAMP_PROFILE_DIR	Output directory for cub_cas timestamp logs
QUERY_SAMPLE_LIMIT	0	$QUERY_SAMPLE_LIMIT	Limit number of query IDs for sample validation runs
FLAMEGRAPH_DIR	$SCRIPT_DIR/flame_graph	$FLAMEGRAPH_DIR	Installed FlameGraph directory
STACKCOLLAPSE_PERF		$STACKCOLLAPSE_PERF	Optional path override for stackcollapse-perf.pl
FLAMEGRAPH_PL		$FLAMEGRAPH_PL	Optional path override for flamegraph.pl
FLAMEGRAPH_REPO_URL	https://github.com/brendangregg/FlameGraph.git	$FLAMEGRAPH_REPO_URL	FlameGraph git repository URL
FLAMEGRAPH_TARBALL_URL	https://github.com/brendangregg/FlameGraph/archive/refs/heads/master.tar.gz	$FLAMEGRAPH_TARBALL_URL	FlameGraph tarball URL
EOF
}

write_git_manifest_tsv() {
  local out_file="$1"
  local main_branch=""
  local main_commit=""
  local main_dirty=""
  local cci_branch=""
  local cci_commit=""
  local cci_dirty=""

  main_branch="$(git_branch_or_detached "$REPO_ROOT")"
  main_commit="$(git -C "$REPO_ROOT" rev-parse HEAD)"
  main_dirty="$(git_dirty_flag "$REPO_ROOT")"
  cci_branch="$(git_branch_or_detached "$REPO_ROOT/cubrid-cci")"
  cci_commit="$(git -C "$REPO_ROOT/cubrid-cci" rev-parse HEAD)"
  cci_dirty="$(git_dirty_flag "$REPO_ROOT/cubrid-cci")"

  cat >"$out_file" <<EOF
component	path	branch	commit	dirty
cubrid	$REPO_ROOT	$main_branch	$main_commit	$main_dirty
cubrid-cci	$REPO_ROOT/cubrid-cci	$cci_branch	$cci_commit	$cci_dirty
EOF
}

get_dataset_download_url() {
  if [[ -n "$DATASET_DOWNLOAD_URL" ]]; then
    printf '%s\n' "$DATASET_DOWNLOAD_URL"
    return
  fi

  printf 'https://ann-benchmarks.com/%s\n' "$DATASET_FILENAME"
}

run_stage() {
  local stage_name="$1"
  shift
  local start_ts
  local end_ts
  local elapsed_sec

  log "stage start: $stage_name"
  start_ts="$(date +%s)"
  "$@"
  end_ts="$(date +%s)"
  elapsed_sec="$((end_ts - start_ts))"
  log "stage done: $stage_name (${elapsed_sec}s)"
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    printf 'missing command: %s\n' "$1" >&2
    exit 1
  }
}

run_with_optional_taskset() {
  if (( ANN_TASKSET_ENABLE == 1 )); then
    taskset -c "$ANN_TASKSET_CPU" "$@"
    return
  fi

  "$@"
}

run_cubrid() {
  run_with_optional_taskset cubrid "$@"
}

run_cubrid_server_start() {
  if (( CAS_TIMESTAMP_PROFILE_ENABLE == 1 )); then
    CUBRID_CAS_PROFILE_DIR="$CAS_TIMESTAMP_PROFILE_DIR" run_cubrid server start "$1"
    return
  fi

  run_cubrid server start "$1"
}

run_csql() {
  run_with_optional_taskset csql "$@"
}

run_make_cubrid_demo() {
  run_with_optional_taskset ./make_cubrid_demo.sh
}

run_csql_timed_to_file() {
  local time_file="$1"
  local out_file="$2"
  shift 2

  run_with_optional_taskset \
    /usr/bin/time \
    -f 'wall_sec=%e\nuser_sec=%U\nsys_sec=%S\nmax_rss_kb=%M\nvol_cs=%w\ninvol_cs=%c' \
    -o "$time_file" \
    csql "$@" > "$out_file"
}

run_cci_runner_timed_to_file() {
  local time_file="$1"
  local out_file="$2"
  local query_profile_file="${3:-}"
  if (( $# >= 3 )); then
    shift 3
  else
    shift 2
  fi

  if [[ -n "$query_profile_file" ]]; then
    CCI_QUERY_PROFILE_FILE="$query_profile_file" \
      run_with_optional_taskset \
      /usr/bin/time \
      -f 'wall_sec=%e\nuser_sec=%U\nsys_sec=%S\nmax_rss_kb=%M\nvol_cs=%w\ninvol_cs=%c' \
      -o "$time_file" \
      "$CCI_RUNNER_BIN" "$@" > "$out_file"
  else
    run_with_optional_taskset \
      /usr/bin/time \
      -f 'wall_sec=%e\nuser_sec=%U\nsys_sec=%S\nmax_rss_kb=%M\nvol_cs=%w\ninvol_cs=%c' \
      -o "$time_file" \
      "$CCI_RUNNER_BIN" "$@" > "$out_file"
  fi
}

set_cubrid_conf_param() {
  local key="$1"
  local value="$2"
  local conf_file="$3"
  local tmp_file

  tmp_file="$(mktemp "${conf_file}.XXXXXX")"
  awk -v key="$key" -v value="$value" '
    BEGIN {
      pattern = "^[[:space:]]*" key "[[:space:]]*="
      replaced = 0
    }
    $0 ~ pattern {
      if (replaced == 0) {
        print key "=" value
        replaced = 1
      }
      next
    }
    {
      print
    }
    END {
      if (replaced == 0) {
        print key "=" value
      }
    }
  ' "$conf_file" > "$tmp_file"
  mv "$tmp_file" "$conf_file"
}

remove_cubrid_conf_param() {
  local key="$1"
  local conf_file="$2"
  local tmp_file

  tmp_file="$(mktemp "${conf_file}.XXXXXX")"
  awk -v key="$key" '
    BEGIN {
      pattern = "^[[:space:]]*" key "[[:space:]]*="
    }
    $0 ~ pattern {
      next
    }
    {
      print
    }
  ' "$conf_file" > "$tmp_file"
  mv "$tmp_file" "$conf_file"
}

ensure_segment_profile_dir() {
  if (( SEGMENT_PROFILE_ENABLE != 1 )); then
    return
  fi

  mkdir -p "$SEGMENT_PROFILE_DIR"
}

snapshot_process_group_stats() {
  local group_name="$1"
  local out_file="$2"

  python3 - "$group_name" "$DB_NAME" "$BROKER_NAME" "$out_file" <<'PY'
import json
import os
import sys

group_name, db_name, broker_name, out_file = sys.argv[1:5]

def parse_status_file(status_path):
    values = {}
    try:
        with open(status_path, encoding="utf-8", errors="replace") as f:
            for line in f:
                if ":" not in line:
                    continue
                key, value = line.split(":", 1)
                values[key.strip()] = value.strip()
    except OSError:
        return {}
    return values

def status_int(status_map, key):
    raw = status_map.get(key, "0").split()[0]
    try:
        return int(raw)
    except ValueError:
        return 0

stats = {
    "group_name": group_name,
    "pid_count": 0,
    "pids": [],
    "utime_ticks": 0,
    "stime_ticks": 0,
    "rss_kb": 0,
    "vol_cs": 0,
    "invol_cs": 0,
    "clk_tck": os.sysconf(os.sysconf_names["SC_CLK_TCK"]),
}

for entry in os.listdir("/proc"):
    if not entry.isdigit():
        continue

    pid = int(entry)
    proc_dir = os.path.join("/proc", entry)
    try:
        with open(os.path.join(proc_dir, "comm"), encoding="utf-8", errors="replace") as f:
            comm = f.read().strip()
        with open(os.path.join(proc_dir, "cmdline"), "rb") as f:
            cmdline = f.read().replace(b"\0", b" ").decode("utf-8", errors="replace").strip()
        with open(os.path.join(proc_dir, "stat"), encoding="utf-8", errors="replace") as f:
            stat_line = f.read().strip()
    except OSError:
        continue

    include = False
    if group_name == "cub_server":
      include = comm == "cub_server" and db_name in cmdline
    elif group_name == "cub_cas":
      include = comm == "cub_cas" and cmdline.startswith(f"{broker_name}_cub_cas_")

    if not include:
        continue

    status_map = parse_status_file(os.path.join(proc_dir, "status"))
    fields = stat_line.split()
    if len(fields) < 24:
        continue

    stats["pid_count"] += 1
    stats["pids"].append(pid)
    stats["utime_ticks"] += int(fields[13])
    stats["stime_ticks"] += int(fields[14])
    stats["rss_kb"] += status_int(status_map, "VmRSS")
    stats["vol_cs"] += status_int(status_map, "voluntary_ctxt_switches")
    stats["invol_cs"] += status_int(status_map, "nonvoluntary_ctxt_switches")

with open(out_file, "w", encoding="utf-8") as f:
    json.dump(stats, f, sort_keys=True)
PY
}

write_segment_profile_summary() {
  local label="$1"
  local client_component="$2"
  local client_time_file="$3"
  local server_before_file="$4"
  local server_after_file="$5"
  local cas_before_file="$6"
  local cas_after_file="$7"

  python3 - \
    "$label" \
    "$client_component" \
    "$client_time_file" \
    "$server_before_file" \
    "$server_after_file" \
    "$cas_before_file" \
    "$cas_after_file" \
    "$SEGMENT_PROFILE_DIR" <<'PY'
import csv
import json
import sys
from pathlib import Path

label, client_component, client_time_file, server_before_file, server_after_file, cas_before_file, cas_after_file, out_dir = sys.argv[1:9]
out_dir = Path(out_dir)
per_label_csv = out_dir / f"{label}.csv"
summary_csv = out_dir / "segment_profile_summary.csv"

def read_kv_file(path):
    values = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if "=" not in line:
                continue
            key, value = line.split("=", 1)
            values[key] = value
    return values

def read_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)

def process_delta(before_stats, after_stats):
    clk_tck = after_stats.get("clk_tck") or before_stats.get("clk_tck") or 100
    return {
        "pid_count_before": before_stats.get("pid_count", 0),
        "pid_count_after": after_stats.get("pid_count", 0),
        "cpu_sec": (
            (after_stats.get("utime_ticks", 0) - before_stats.get("utime_ticks", 0))
            + (after_stats.get("stime_ticks", 0) - before_stats.get("stime_ticks", 0))
        ) / clk_tck,
        "user_sec": (after_stats.get("utime_ticks", 0) - before_stats.get("utime_ticks", 0)) / clk_tck,
        "sys_sec": (after_stats.get("stime_ticks", 0) - before_stats.get("stime_ticks", 0)) / clk_tck,
        "vol_cs_delta": after_stats.get("vol_cs", 0) - before_stats.get("vol_cs", 0),
        "invol_cs_delta": after_stats.get("invol_cs", 0) - before_stats.get("invol_cs", 0),
        "rss_kb_after": after_stats.get("rss_kb", 0),
        "pids_after": " ".join(str(pid) for pid in after_stats.get("pids", [])),
    }

client_stats = read_kv_file(client_time_file)
server_delta = process_delta(read_json(server_before_file), read_json(server_after_file))
cas_delta = process_delta(read_json(cas_before_file), read_json(cas_after_file))

rows = [
    {
        "label": label,
        "component": client_component,
        "wall_sec": float(client_stats.get("wall_sec", "0")),
        "user_sec": float(client_stats.get("user_sec", "0")),
        "sys_sec": float(client_stats.get("sys_sec", "0")),
        "cpu_sec": float(client_stats.get("user_sec", "0")) + float(client_stats.get("sys_sec", "0")),
        "vol_cs_delta": int(client_stats.get("vol_cs", "0")),
        "invol_cs_delta": int(client_stats.get("invol_cs", "0")),
        "rss_kb_after": int(client_stats.get("max_rss_kb", "0")),
        "pid_count_before": 1,
        "pid_count_after": 1,
        "pids_after": client_component,
    },
    {
        "label": label,
        "component": "cub_server",
        "wall_sec": "",
        **server_delta,
    },
    {
        "label": label,
        "component": "cub_cas",
        "wall_sec": "",
        **cas_delta,
    },
]

fieldnames = [
    "label",
    "component",
    "wall_sec",
    "user_sec",
    "sys_sec",
    "cpu_sec",
    "vol_cs_delta",
    "invol_cs_delta",
    "rss_kb_after",
    "pid_count_before",
    "pid_count_after",
    "pids_after",
]

with open(per_label_csv, "w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(rows)

write_header = not summary_csv.exists()
with open(summary_csv, "a", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=fieldnames)
    if write_header:
        writer.writeheader()
    writer.writerows(rows)
PY

  log "saved segment profile to $SEGMENT_PROFILE_DIR/${label}.csv"
}

ensure_cas_timestamp_profile_dir() {
  if (( CAS_TIMESTAMP_PROFILE_ENABLE != 1 )); then
    return
  fi

  mkdir -p "$CAS_TIMESTAMP_PROFILE_DIR"
}

snapshot_cas_timestamp_offsets() {
  local out_file="$1"

  python3 - "$CAS_TIMESTAMP_PROFILE_DIR" "$out_file" <<'PY'
import json
import sys
from pathlib import Path

profile_dir = Path(sys.argv[1])
out_file = Path(sys.argv[2])
offsets = {}

if profile_dir.exists():
    for path in sorted(profile_dir.glob("cub_cas.*.csv")):
        try:
            with path.open(encoding="utf-8", errors="replace") as f:
                line_count = sum(1 for _ in f)
        except OSError:
            continue
        offsets[str(path)] = line_count

out_file.write_text(json.dumps(offsets, sort_keys=True), encoding="utf-8")
PY
}

snapshot_server_timestamp_offsets() {
  local out_file="$1"

  python3 - "$CAS_TIMESTAMP_PROFILE_DIR" "$out_file" <<'PY'
import json
import sys
from pathlib import Path

profile_dir = Path(sys.argv[1])
out_file = Path(sys.argv[2])
offsets = {}

if profile_dir.exists():
    for path in sorted(profile_dir.glob("cub_server.*.csv")):
        try:
            with path.open(encoding="utf-8", errors="replace") as f:
                line_count = sum(1 for _ in f)
        except OSError:
            continue
        offsets[str(path)] = line_count

out_file.write_text(json.dumps(offsets, sort_keys=True), encoding="utf-8")
PY
}

extract_cas_timestamp_delta() {
  local before_file="$1"
  local after_file="$2"
  local out_file="$3"

  python3 - "$before_file" "$after_file" "$out_file" <<'PY'
import csv
import json
import sys
from pathlib import Path

before = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
after = json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))
out_file = Path(sys.argv[3])
fieldnames = [
    "ts_epoch_us",
    "pid",
    "op",
    "phase",
    "srv_h_id",
    "query_seq",
    "stmt_label",
    "detail0",
    "detail1",
    "duration_us",
    "err_code",
]

with out_file.open("w", newline="", encoding="utf-8") as f_out:
    writer = csv.DictWriter(f_out, fieldnames=fieldnames)
    writer.writeheader()

    for path_str in sorted(after):
        path = Path(path_str)
        start_line = before.get(path_str, 0)
        try:
            with path.open(encoding="utf-8", errors="replace") as f_in:
                for idx, line in enumerate(f_in):
                    if idx == 0:
                        continue
                    if idx < start_line:
                        continue
                    line = line.strip()
                    if not line:
                        continue
                    parts = line.split(",")
                    if len(parts) != len(fieldnames):
                        continue
                    writer.writerow(dict(zip(fieldnames, parts)))
        except OSError:
            continue
PY
}

extract_server_timestamp_delta() {
  local before_file="$1"
  local after_file="$2"
  local out_file="$3"

  python3 - "$before_file" "$after_file" "$out_file" <<'PY'
import csv
import json
import sys
from pathlib import Path

before = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
after = json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))
out_file = Path(sys.argv[3])
fieldnames = [
    "ts_epoch_us",
    "pid",
    "op",
    "phase",
    "rid",
    "stmt_label",
    "detail0",
    "detail1",
    "duration_us",
    "err_code",
]

with out_file.open("w", newline="", encoding="utf-8") as f_out:
    writer = csv.DictWriter(f_out, fieldnames=fieldnames)
    writer.writeheader()

    for path_str in sorted(after):
        path = Path(path_str)
        start_line = before.get(path_str, 0)
        try:
            with path.open(encoding="utf-8", errors="replace") as f_in:
                for idx, line in enumerate(f_in):
                    if idx == 0:
                        continue
                    if idx < start_line:
                        continue
                    line = line.strip()
                    if not line:
                        continue
                    parts = line.split(",")
                    if len(parts) != len(fieldnames):
                        continue
                    writer.writerow(dict(zip(fieldnames, parts)))
        except OSError:
            continue
PY
}

summarize_cas_timestamp_delta() {
  local src_file="$1"
  local summary_file="$2"

  python3 - "$src_file" "$summary_file" <<'PY'
import csv
import math
import sys
from pathlib import Path

src = Path(sys.argv[1])
summary = Path(sys.argv[2])
rows = list(csv.DictReader(src.open()))
if not rows:
    raise SystemExit(0)

def percentile(sorted_values, pct):
    if not sorted_values:
        return 0.0
    if len(sorted_values) == 1:
        return sorted_values[0]
    pos = (len(sorted_values) - 1) * pct
    lower = math.floor(pos)
    upper = math.ceil(pos)
    if lower == upper:
        return sorted_values[lower]
    weight = pos - lower
    return sorted_values[lower] * (1.0 - weight) + sorted_values[upper] * weight

ops = {}
for row in rows:
    if row["phase"] != "end":
        continue
    op_key = (row["op"], row.get("stmt_label", "unknown"))
    ops.setdefault(op_key, []).append(float(row["duration_us"]) / 1000000.0)

with summary.open("w", newline="", encoding="utf-8") as f:
    writer = csv.writer(f)
    writer.writerow(["op", "stmt_label", "count", "avg_sec", "p50_sec", "p95_sec", "p99_sec", "max_sec"])
    for op, stmt_label in sorted(ops):
        values = sorted(ops[(op, stmt_label)])
        writer.writerow([
            op,
            stmt_label,
            len(values),
            f"{sum(values) / len(values):.9f}",
            f"{percentile(values, 0.50):.9f}",
            f"{percentile(values, 0.95):.9f}",
            f"{percentile(values, 0.99):.9f}",
            f"{values[-1]:.9f}",
        ])
PY

  log "saved cub_cas timestamp summary to $summary_file"
}

summarize_server_timestamp_delta() {
  local src_file="$1"
  local summary_file="$2"

  python3 - "$src_file" "$summary_file" <<'PY'
import csv
import math
import sys
from pathlib import Path

src = Path(sys.argv[1])
summary = Path(sys.argv[2])
rows = list(csv.DictReader(src.open()))
if not rows:
    raise SystemExit(0)

def percentile(sorted_values, pct):
    if not sorted_values:
        return 0.0
    if len(sorted_values) == 1:
        return sorted_values[0]
    pos = (len(sorted_values) - 1) * pct
    lower = math.floor(pos)
    upper = math.ceil(pos)
    if lower == upper:
        return sorted_values[lower]
    weight = pos - lower
    return sorted_values[lower] * (1.0 - weight) + sorted_values[upper] * weight

ops = {}
for row in rows:
    if row["phase"] != "end":
        continue
    op_key = (row["op"], row.get("stmt_label", "unknown"))
    ops.setdefault(op_key, []).append(float(row["duration_us"]) / 1000000.0)

with summary.open("w", newline="", encoding="utf-8") as f:
    writer = csv.writer(f)
    writer.writerow(["op", "stmt_label", "count", "avg_sec", "p50_sec", "p95_sec", "p99_sec", "max_sec"])
    for op, stmt_label in sorted(ops):
        values = sorted(ops[(op, stmt_label)])
        writer.writerow([
            op,
            stmt_label,
            len(values),
            f"{sum(values) / len(values):.9f}",
            f"{percentile(values, 0.50):.9f}",
            f"{percentile(values, 0.95):.9f}",
            f"{percentile(values, 0.99):.9f}",
            f"{values[-1]:.9f}",
        ])
PY

  log "saved cub_server timestamp summary to $summary_file"
}

write_cub_server_perf_stat_summary() {
  local build_stat_file="$1"
  local query_stat_file="$2"
  local build_segment_file="$3"
  local query_segment_file="$4"
  local out_csv="$5"
  local out_svg="$6"

  python3 - "$build_stat_file" "$query_stat_file" "$build_segment_file" "$query_segment_file" "$out_csv" "$out_svg" <<'PY'
import csv
import math
import sys
from pathlib import Path

build_stat_path = Path(sys.argv[1])
query_stat_path = Path(sys.argv[2])
build_segment_path = Path(sys.argv[3])
query_segment_path = Path(sys.argv[4])
out_csv = Path(sys.argv[5])
out_svg = Path(sys.argv[6])

def parse_perf_stat(path: Path):
    metrics = {}
    if not path.is_file():
        return metrics
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(",")
        if len(parts) < 7:
            continue
        value_str, unit, event, runtime, pct, metric_value, metric_unit = parts[:7]
        try:
            value = float(value_str)
        except ValueError:
            continue
        metrics[event] = {
            "value": value,
            "unit": unit,
            "runtime": float(runtime) if runtime else 0.0,
            "pct": pct,
            "metric_value": float(metric_value) if metric_value else None,
            "metric_unit": metric_unit,
        }
    return metrics

def parse_segment_wall(path: Path):
    if not path.is_file():
        return 0.0
    with path.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if row.get("component") == "cci":
                try:
                    return float(row.get("wall_sec") or 0.0)
                except ValueError:
                    return 0.0
    return 0.0

def fmt_num(value):
    if value is None:
        return "N/A"
    if abs(value) >= 1_000_000_000:
        return f"{value/1_000_000_000:.3f} B"
    if abs(value) >= 1_000_000:
        return f"{value/1_000_000:.3f} M"
    if abs(value) >= 1_000:
        return f"{value/1_000:.3f} K"
    if float(value).is_integer():
        return f"{int(value)}"
    return f"{value:.3f}"

def metric_value(metrics, name):
    item = metrics.get(name)
    return None if item is None else item["value"]

def derived(metrics, elapsed):
    task_clock_ns = metric_value(metrics, "task-clock")
    cycles = metric_value(metrics, "cycles")
    instructions = metric_value(metrics, "instructions")
    branches = metric_value(metrics, "branches")
    branch_misses = metric_value(metrics, "branch-misses")
    l1d_loads = metric_value(metrics, "L1-dcache-loads")
    l1d_misses = metric_value(metrics, "L1-dcache-load-misses")
    l1i_loads = metric_value(metrics, "L1-icache-loads")
    l1i_misses = metric_value(metrics, "L1-icache-load-misses")
    dtlb_loads = metric_value(metrics, "dTLB-loads")
    dtlb_misses = metric_value(metrics, "dTLB-load-misses")
    itlb_loads = metric_value(metrics, "iTLB-loads")
    itlb_misses = metric_value(metrics, "iTLB-load-misses")
    topdown_fe = metric_value(metrics, "topdown-fe-bound")
    slots = metric_value(metrics, "slots")
    context_switches = metric_value(metrics, "context-switches")
    cpu_migrations = metric_value(metrics, "cpu-migrations")
    page_faults = metric_value(metrics, "page-faults")

    task_clock_sec = task_clock_ns / 1_000_000_000 if task_clock_ns is not None else None
    cpus_utilized = (task_clock_sec / elapsed) if task_clock_sec is not None and elapsed > 0 else None
    ipc = (instructions / cycles) if instructions is not None and cycles not in (None, 0) else None
    fe_bound_pct = (topdown_fe / slots * 100.0) if topdown_fe is not None and slots not in (None, 0) else None
    branch_miss_rate = (branch_misses / branches * 100.0) if branch_misses is not None and branches not in (None, 0) else None
    l1d_miss_rate = (l1d_misses / l1d_loads * 100.0) if l1d_misses is not None and l1d_loads not in (None, 0) else None
    l1i_miss_rate = (l1i_misses / l1i_loads * 100.0) if l1i_misses is not None and l1i_loads not in (None, 0) else None
    dtlb_miss_rate = (dtlb_misses / dtlb_loads * 100.0) if dtlb_misses is not None and dtlb_loads not in (None, 0) else None
    itlb_miss_rate = (itlb_misses / itlb_loads * 100.0) if itlb_misses is not None and itlb_loads not in (None, 0) else None

    return {
        "elapsed_s": elapsed,
        "task_clock_s": task_clock_sec,
        "cpus_utilized": cpus_utilized,
        "cycles": cycles,
        "instructions": instructions,
        "ipc": ipc,
        "frontend_stalled_pct": fe_bound_pct,
        "branches": branches,
        "branch_misses": branch_misses,
        "branch_miss_rate_pct": branch_miss_rate,
        "L1_dcache_loads": l1d_loads,
        "L1_dcache_load_misses": l1d_misses,
        "L1_dcache_miss_rate_pct": l1d_miss_rate,
        "L1_icache_loads": l1i_loads,
        "L1_icache_load_misses": l1i_misses,
        "L1_icache_miss_rate_pct": l1i_miss_rate,
        "dTLB_loads": dtlb_loads,
        "dTLB_load_misses": dtlb_misses,
        "dTLB_miss_rate_pct": dtlb_miss_rate,
        "iTLB_loads": itlb_loads,
        "iTLB_load_misses": itlb_misses,
        "iTLB_miss_rate_pct": itlb_miss_rate,
        "context_switches": context_switches,
        "cpu_migrations": cpu_migrations,
        "page_faults": page_faults,
    }

build = derived(parse_perf_stat(build_stat_path), parse_segment_wall(build_segment_path))
query = derived(parse_perf_stat(query_stat_path), parse_segment_wall(query_segment_path))

rows = []
metrics_order = [
    ("elapsed_s", "elapsed"),
    ("task_clock_s", "task-clock"),
    ("cpus_utilized", "CPUs utilized"),
    ("cycles", "cycles"),
    ("instructions", "instructions"),
    ("ipc", "IPC"),
    ("frontend_stalled_pct", "frontend stalled %"),
    ("branches", "branches"),
    ("branch_misses", "branch-misses"),
    ("branch_miss_rate_pct", "branch-miss rate %"),
    ("L1_dcache_loads", "L1-dcache-loads"),
    ("L1_dcache_load_misses", "L1-dcache-load-misses"),
    ("L1_dcache_miss_rate_pct", "L1-dcache miss rate %"),
    ("L1_icache_loads", "L1-icache-loads"),
    ("L1_icache_load_misses", "L1-icache-load-misses"),
    ("L1_icache_miss_rate_pct", "L1-icache miss rate %"),
    ("dTLB_loads", "dTLB-loads"),
    ("dTLB_load_misses", "dTLB-load-misses"),
    ("dTLB_miss_rate_pct", "dTLB miss rate %"),
    ("iTLB_loads", "iTLB-loads"),
    ("iTLB_load_misses", "iTLB-load-misses"),
    ("iTLB_miss_rate_pct", "iTLB miss rate %"),
    ("context_switches", "context-switches"),
    ("cpu_migrations", "cpu-migrations"),
    ("page_faults", "page-faults"),
]

with out_csv.open("w", newline="", encoding="utf-8") as f:
    writer = csv.writer(f)
    writer.writerow(["metric", "build_index", "query"])
    for key, label in metrics_order:
        writer.writerow([label, build.get(key), query.get(key)])

focus = [
    ("elapsed", build["elapsed_s"], query["elapsed_s"], "#2f6fed"),
    ("task-clock", build["task_clock_s"], query["task_clock_s"], "#2c9a5f"),
    ("IPC", build["ipc"], query["ipc"], "#b55d00"),
    ("frontend stalled %", build["frontend_stalled_pct"], query["frontend_stalled_pct"], "#c43d4b"),
    ("branch-miss rate %", build["branch_miss_rate_pct"], query["branch_miss_rate_pct"], "#7b61ff"),
    ("L1-d miss rate %", build["L1_dcache_miss_rate_pct"], query["L1_dcache_miss_rate_pct"], "#0097a7"),
    ("dTLB miss rate %", build["dTLB_miss_rate_pct"], query["dTLB_miss_rate_pct"], "#8d6e63"),
]

available = [max(v1 or 0.0, v2 or 0.0) for _, v1, v2, _ in focus]
max_value = max(available) if available else 1.0
if max_value <= 0:
    max_value = 1.0

width, height = 1180, 620
left, top = 190, 70
plot_w = width - left - 70
group_h = 58
bar_h = 16
gap = 18
parts = [
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
    '<rect width="100%" height="100%" fill="#fcfcfb"/>',
    '<text x="36" y="34" font-size="24" font-weight="700" fill="#111">cub_server perf stat: build index vs query</text>',
    '<text x="36" y="54" font-size="13" fill="#555">Phase-separated perf stat summary for cub_server</text>',
    '<rect x="820" y="24" width="14" height="14" fill="#2f6fed"/>',
    '<text x="842" y="36" font-size="13" fill="#222">build index</text>',
    '<rect x="940" y="24" width="14" height="14" fill="#6aa9ff"/>',
    '<text x="962" y="36" font-size="13" fill="#222">query</text>',
    f'<line x1="{left}" y1="{height-50}" x2="{width-40}" y2="{height-50}" stroke="#333" stroke-width="1.2"/>',
]

for i, (label, build_val, query_val, color) in enumerate(focus):
    y = top + i * group_h
    b = build_val or 0.0
    q = query_val or 0.0
    bw = plot_w * (b / max_value)
    qw = plot_w * (q / max_value)
    parts.append(f'<text x="{left-14}" y="{y+18}" text-anchor="end" font-size="14" fill="#222">{label}</text>')
    parts.append(f'<rect x="{left}" y="{y}" width="{bw:.2f}" height="{bar_h}" rx="6" fill="{color}"/>')
    parts.append(f'<rect x="{left}" y="{y+bar_h+6}" width="{qw:.2f}" height="{bar_h}" rx="6" fill="#6aa9ff"/>')
    parts.append(f'<text x="{left + bw + 8:.2f}" y="{y+13}" font-size="12" fill="#222">{fmt_num(b)}</text>')
    parts.append(f'<text x="{left + qw + 8:.2f}" y="{y+bar_h+19}" font-size="12" fill="#222">{fmt_num(q)}</text>')

parts.append('</svg>')
out_svg.write_text("\n".join(parts), encoding="utf-8")
PY
}

summarize_cci_query_profile() {
  local query_profile_file="$1"
  local summary_file="$2"

  python3 - "$query_profile_file" "$summary_file" <<'PY'
import csv
import math
import sys
from pathlib import Path

src = Path(sys.argv[1])
dst = Path(sys.argv[2])

rows = list(csv.DictReader(src.open()))
if not rows:
    raise SystemExit(0)

metrics = [
    "vector_lookup_sec",
    "bind_sec",
    "execute_sec",
    "first_row_wait_sec",
    "fetch_sec",
    "close_result_sec",
    "total_sec",
]

def percentile(sorted_values, pct):
    if not sorted_values:
        return 0.0
    if len(sorted_values) == 1:
        return sorted_values[0]
    pos = (len(sorted_values) - 1) * pct
    lower = math.floor(pos)
    upper = math.ceil(pos)
    if lower == upper:
        return sorted_values[lower]
    weight = pos - lower
    return sorted_values[lower] * (1.0 - weight) + sorted_values[upper] * weight

with dst.open("w", newline="", encoding="utf-8") as f:
    writer = csv.writer(f)
    writer.writerow(["metric", "avg_sec", "p50_sec", "p95_sec", "p99_sec", "max_sec"])
    for metric in metrics:
        values = sorted(float(row[metric]) for row in rows)
        avg = sum(values) / len(values)
        writer.writerow([
            metric,
            f"{avg:.9f}",
            f"{percentile(values, 0.50):.9f}",
            f"{percentile(values, 0.95):.9f}",
            f"{percentile(values, 0.99):.9f}",
            f"{values[-1]:.9f}",
        ])
PY

  log "saved query timing summary to $summary_file"
}

ensure_cci_runner_built() {
  if [[ -z "${CUBRID:-}" ]]; then
    CUBRID="$PWD/install.out"
    export CUBRID
  fi

  if [[ ! -x "$CCI_RUNNER_BIN" || "$CCI_RUNNER_BIN" -ot "$CCI_RUNNER_SRC" ]]; then
    log "building CCI ANN runner: $CCI_RUNNER_BIN"
    gcc \
      -O2 \
      -std=c11 \
      -Wall \
      -Wextra \
      -I"$CUBRID/cci/include" \
      -I"$CUBRID/include" \
      "$CCI_RUNNER_SRC" \
      -L"$CUBRID/cci/lib" \
      -Wl,-rpath,"$CUBRID/cci/lib" \
      -lcascci \
      -o "$CCI_RUNNER_BIN"
  fi
}

set_broker_conf_param() {
  local broker_name="$1"
  local key="$2"
  local value="$3"
  local conf_file="$4"
  local tmp_file=""

  tmp_file="$(mktemp "${conf_file}.XXXXXX")"
  awk -v broker_name="$broker_name" -v key="$key" -v value="$value" '
    function flush_pending() {
      if (in_target && !replaced) {
        print key " = " value
      }
    }
    BEGIN {
      in_target = 0
      replaced = 0
      target_upper = toupper(broker_name)
    }
    /^\[[^]]+\]/ {
      flush_pending()
      section = $0
      gsub(/^\[%?/, "", section)
      gsub(/\]$/, "", section)
      in_target = (toupper(section) == target_upper)
      replaced = 0
      print
      next
    }
    {
      if (in_target) {
        pattern = "^[[:space:]]*" key "[[:space:]]*="
        if ($0 ~ pattern) {
          print key " = " value
          replaced = 1
          next
        }
      }
      print
    }
    END {
      flush_pending()
    }
  ' "$conf_file" > "$tmp_file"
  mv "$tmp_file" "$conf_file"
}

configure_broker_for_cci_profile() {
  local broker_conf=""

  if [[ -z "${CUBRID:-}" ]]; then
    return
  fi

  broker_conf="$CUBRID/conf/cubrid_broker.conf"
  if [[ ! -f "$broker_conf" ]]; then
    return
  fi

  if (( BROKER_SINGLE_CAS_ENABLE == 1 )); then
    log "configuring broker $BROKER_NAME to use a single CAS"
    set_broker_conf_param "$BROKER_NAME" "MIN_NUM_APPL_SERVER" "1" "$broker_conf"
    set_broker_conf_param "$BROKER_NAME" "MAX_NUM_APPL_SERVER" "1" "$broker_conf"
  fi
}

get_broker_cas_pids() {
  cubrid broker status 2>/dev/null | awk -v broker_name="$BROKER_NAME" '
    BEGIN {
      in_target = 0
    }
    /^%/ {
      section = $2
      gsub(/^%/, "", section)
      in_target = (tolower(section) == tolower(broker_name))
      next
    }
    in_target && $1 ~ /^[0-9]+$/ && $2 ~ /^[0-9]+$/ {
      print $2
    }
  '
}

get_single_broker_cas_pid() {
  local pids=""
  local pid_count=0

  pids="$(get_broker_cas_pids | paste -sd, -)"
  if [[ -z "$pids" ]]; then
    return
  fi

  IFS=',' read -r -a _cas_pid_arr <<< "$pids"
  pid_count="${#_cas_pid_arr[@]}"
  if (( pid_count != 1 )); then
    printf 'expected exactly 1 CAS for broker %s, found %d (%s)\n' \
      "$BROKER_NAME" \
      "$pid_count" \
      "$pids" >&2
    exit 1
  fi

  printf '%s\n' "$pids"
}

ensure_broker_running() {
  log "restarting broker $BROKER_NAME on port $BROKER_PORT for CCI path"
  configure_broker_for_cci_profile
  ensure_cas_timestamp_profile_dir
  run_cubrid broker stop >/dev/null 2>&1 || true
  if (( CAS_TIMESTAMP_PROFILE_ENABLE == 1 )); then
    CUBRID_CAS_PROFILE_DIR="$CAS_TIMESTAMP_PROFILE_DIR" \
      run_cubrid broker start >/dev/null 2>&1
  else
    run_cubrid broker start >/dev/null 2>&1
  fi
}

prepare_base_dataset() {
  local prepare_script="$SCRIPT_DIR/test_ann.sh"

  if [[ ! -x "$prepare_script" ]]; then
    printf 'prepare script not found: %s\n' "$prepare_script" >&2
    exit 1
  fi

  ANN_PREPARE_ONLY=1 \
  ANN_SINGLE_CORE_EXPERIMENT=0 \
  ANN_TASKSET_ENABLE="$ANN_TASKSET_ENABLE" \
  ANN_TASKSET_CPU="$ANN_TASKSET_CPU" \
  TRAIN_ROW_LIMIT="$TRAIN_ROW_LIMIT" \
  HNSW_M="$HNSW_M" \
  HNSW_EF_CONSTRUCTION="$HNSW_EF_CONSTRUCTION" \
  HNSW_EF_SEARCH_VALUES="$HNSW_EF_SEARCH_VALUES" \
  SEGMENT_PROFILE_ENABLE=0 \
  "$prepare_script"
}

load_query_ids_from_cache() {
  if [[ ! -s "$QUERY_ID_FILE" ]]; then
    printf 'missing query id cache: %s\n' "$QUERY_ID_FILE" >&2
    exit 1
  fi

  mapfile -t QUERY_IDS < "$QUERY_ID_FILE"
  EXCLUDED_QUERY_COUNT="$(grep -vc '^#' "$EXCLUDED_QUERY_FILE" || true)"
}

run_cci_runner_with_profile() {
  local out_file="$1"
  local label="$2"
  shift 2
  local safe_label=""
  local client_time_file=""
  local server_before_file=""
  local server_after_file=""
  local cas_before_file=""
  local cas_after_file=""
  local query_profile_file=""
  local query_profile_summary_file=""
  local cas_ts_before_offsets=""
  local cas_ts_after_offsets=""
  local cas_ts_delta_file=""
  local cas_ts_summary_file=""
  local server_ts_before_offsets=""
  local server_ts_after_offsets=""
  local server_ts_delta_file=""
  local server_ts_summary_file=""
  local server_pid=""
  local cas_pids=""
  local perf_pid=""
  local perf_data_file=""
  local profile=""
  local output_label=""
  local safe_output_label=""
  local hnsw_debug_collect_enabled=0
  local -a perf_job_pids=()
  local -a flamegraph_data_files=()
  local -a flamegraph_svg_files=()

  ensure_segment_profile_dir
  ensure_cas_timestamp_profile_dir
  safe_label="$(sanitize_perf_label "$label")"
  client_time_file="$SEGMENT_PROFILE_DIR/${safe_label}.cci.time"
  server_before_file="$SEGMENT_PROFILE_DIR/${safe_label}.cub_server.before.json"
  server_after_file="$SEGMENT_PROFILE_DIR/${safe_label}.cub_server.after.json"
  cas_before_file="$SEGMENT_PROFILE_DIR/${safe_label}.cub_cas.before.json"
  cas_after_file="$SEGMENT_PROFILE_DIR/${safe_label}.cub_cas.after.json"
  if [[ "$safe_label" == query_* ]]; then
    query_profile_file="$SEGMENT_PROFILE_DIR/${safe_label}.query_stage_breakdown.csv"
    query_profile_summary_file="$SEGMENT_PROFILE_DIR/${safe_label}.query_stage_breakdown.summary.csv"
  fi
  if (( CAS_TIMESTAMP_PROFILE_ENABLE == 1 )); then
    cas_ts_before_offsets="$SEGMENT_PROFILE_DIR/${safe_label}.cub_cas_ts.before.json"
    cas_ts_after_offsets="$SEGMENT_PROFILE_DIR/${safe_label}.cub_cas_ts.after.json"
    cas_ts_delta_file="$SEGMENT_PROFILE_DIR/${safe_label}.cub_cas_ts.delta.csv"
    cas_ts_summary_file="$SEGMENT_PROFILE_DIR/${safe_label}.cub_cas_ts.summary.csv"
    server_ts_before_offsets="$SEGMENT_PROFILE_DIR/${safe_label}.cub_server_ts.before.json"
    server_ts_after_offsets="$SEGMENT_PROFILE_DIR/${safe_label}.cub_server_ts.after.json"
    server_ts_delta_file="$SEGMENT_PROFILE_DIR/${safe_label}.cub_server_ts.delta.csv"
    server_ts_summary_file="$SEGMENT_PROFILE_DIR/${safe_label}.cub_server_ts.summary.csv"
  fi

  if is_hnsw_debug_enabled_in_conf; then
    hnsw_debug_collect_enabled=1
  elif (( PERF_ENABLE == 1 || CAS_TIMESTAMP_PROFILE_ENABLE == 1 )); then
    log "hnsw_debug is disabled in cubrid.conf; skipping cub_server/cub_cas profiling for $label"
  fi

  if (( CAS_TIMESTAMP_PROFILE_ENABLE == 1 && hnsw_debug_collect_enabled == 1 )); then
    snapshot_cas_timestamp_offsets "$cas_ts_before_offsets"
    snapshot_server_timestamp_offsets "$server_ts_before_offsets"
  fi

  snapshot_process_group_stats "cub_server" "$server_before_file"
  snapshot_process_group_stats "cub_cas" "$cas_before_file"

  if (( PERF_ENABLE == 1 && hnsw_debug_collect_enabled == 1 )); then
    mkdir -p "$PERF_OUTPUT_DIR"
    if perf_enabled_for_target cub_server || perf_record_enabled_for_target cub_server; then
      server_pid="$(get_cub_server_pid || true)"
      if [[ -n "$server_pid" ]]; then
        while IFS= read -r profile; do
          if is_stat_profile "$profile" && perf_enabled_for_target cub_server; then
            output_label="$(append_perf_profile_suffix "$label" "$profile")"
            safe_output_label="$(sanitize_perf_label "$output_label")"
            perf_pid="$(
              start_perf_stat_attach "$server_pid" "$PERF_OUTPUT_DIR/${safe_output_label}.cub_server.stat.csv" || true
            )"
            if [[ -n "$perf_pid" ]]; then
              perf_job_pids+=("$perf_pid")
            fi
          elif is_record_profile "$profile" && perf_record_enabled_for_target cub_server; then
            output_label="$(append_perf_profile_suffix "$label" "$profile")"
            safe_output_label="$(sanitize_perf_label "$output_label")"
            perf_data_file="$PERF_OUTPUT_DIR/${safe_output_label}.cub_server.data"
            perf_pid="$(
              start_perf_record_attach "$server_pid" "$perf_data_file" "$profile" || true
            )"
            if [[ -n "$perf_pid" ]]; then
              perf_job_pids+=("$perf_pid")
              flamegraph_data_files+=("$perf_data_file")
              flamegraph_svg_files+=("$PERF_OUTPUT_DIR/${safe_output_label}.cub_server.flamegraph.svg")
            fi
          fi
        done < <(expand_perf_profiles)
      else
        log "perf requested for cub_server, but no PID was found for $DB_NAME"
      fi
    fi

    if perf_enabled_for_target cub_cas || perf_record_enabled_for_target cub_cas; then
      cas_pids="$(get_single_broker_cas_pid || true)"
      if [[ -n "$cas_pids" ]]; then
        while IFS= read -r profile; do
          if is_stat_profile "$profile" && perf_enabled_for_target cub_cas; then
            output_label="$(append_perf_profile_suffix "$label" "$profile")"
            safe_output_label="$(sanitize_perf_label "$output_label")"
            perf_pid="$(
              start_perf_stat_attach "$cas_pids" "$PERF_OUTPUT_DIR/${safe_output_label}.cub_cas.stat.csv" || true
            )"
            if [[ -n "$perf_pid" ]]; then
              perf_job_pids+=("$perf_pid")
            fi
          elif is_record_profile "$profile" && perf_record_enabled_for_target cub_cas; then
            output_label="$(append_perf_profile_suffix "$label" "$profile")"
            safe_output_label="$(sanitize_perf_label "$output_label")"
            perf_data_file="$PERF_OUTPUT_DIR/${safe_output_label}.cub_cas.data"
            perf_pid="$(
              start_perf_record_attach "$cas_pids" "$perf_data_file" "$profile" || true
            )"
            if [[ -n "$perf_pid" ]]; then
              perf_job_pids+=("$perf_pid")
              flamegraph_data_files+=("$perf_data_file")
              flamegraph_svg_files+=("$PERF_OUTPUT_DIR/${safe_output_label}.cub_cas.flamegraph.svg")
            fi
          fi
        done < <(expand_perf_profiles)
      else
        log "perf requested for cub_cas, but a single broker CAS PID was not found"
      fi
    fi
  fi

  run_cci_runner_timed_to_file "$client_time_file" "$out_file" "$query_profile_file" "$@"

  for perf_pid in "${perf_job_pids[@]}"; do
    stop_perf_background_job "$perf_pid"
  done

  for ((i = 0; i < ${#flamegraph_data_files[@]}; i++)); do
    generate_flamegraph \
      "${flamegraph_data_files[i]}" \
      "${flamegraph_svg_files[i]}"
    write_perf_folded \
      "${flamegraph_data_files[i]}" \
      "${flamegraph_data_files[i]%.data}.folded"
    write_perf_report \
      "${flamegraph_data_files[i]}" \
      "${flamegraph_data_files[i]%.data}.report.txt"
    if [[ "${flamegraph_data_files[i]}" == *.cub_cas.data ]]; then
      write_perf_folded_active_only \
        "${flamegraph_data_files[i]}" \
        "${flamegraph_data_files[i]%.data}.active.folded"
      generate_flamegraph_from_folded \
        "${flamegraph_data_files[i]%.data}.active.folded" \
        "${flamegraph_data_files[i]%.data}.active.flamegraph.svg"
    fi
  done

  snapshot_process_group_stats "cub_server" "$server_after_file"
  snapshot_process_group_stats "cub_cas" "$cas_after_file"
  write_segment_profile_summary \
    "$safe_label" \
    "cci" \
    "$client_time_file" \
    "$server_before_file" \
    "$server_after_file" \
    "$cas_before_file" \
    "$cas_after_file"
  if (( CAS_TIMESTAMP_PROFILE_ENABLE == 1 && hnsw_debug_collect_enabled == 1 )); then
    snapshot_cas_timestamp_offsets "$cas_ts_after_offsets"
    extract_cas_timestamp_delta "$cas_ts_before_offsets" "$cas_ts_after_offsets" "$cas_ts_delta_file"
    if [[ -s "$cas_ts_delta_file" ]]; then
      summarize_cas_timestamp_delta "$cas_ts_delta_file" "$cas_ts_summary_file"
    fi
    snapshot_server_timestamp_offsets "$server_ts_after_offsets"
    extract_server_timestamp_delta "$server_ts_before_offsets" "$server_ts_after_offsets" "$server_ts_delta_file"
    if [[ -s "$server_ts_delta_file" ]]; then
      summarize_server_timestamp_delta "$server_ts_delta_file" "$server_ts_summary_file"
    fi
  fi
  if [[ -n "$query_profile_file" && -s "$query_profile_file" ]]; then
    summarize_cci_query_profile "$query_profile_file" "$query_profile_summary_file"
  fi
}

configure_cubrid_base_conf() {
  local conf_file="$1"

  remove_cubrid_conf_param "max_clients" "$conf_file"
  remove_cubrid_conf_param "thread_core_count" "$conf_file"
  remove_cubrid_conf_param "parallelism" "$conf_file"
  remove_cubrid_conf_param "max_parallel_workers" "$conf_file"
  remove_cubrid_conf_param "auto_restart_server" "$conf_file"
  remove_cubrid_conf_param "vacuum_disable" "$conf_file"
  remove_cubrid_conf_param "ha_mode" "$conf_file"
  remove_cubrid_conf_param "hnsw_debug" "$conf_file"
  set_cubrid_conf_param "stored_procedure" "no" "$conf_file"

  if (( ANN_HNSW_DEBUG_ENABLE == 1 )) && (( PERF_ENABLE == 1 || SEGMENT_PROFILE_ENABLE == 1 || CAS_TIMESTAMP_PROFILE_ENABLE == 1 )); then
    set_cubrid_conf_param "hnsw_debug" "1" "$conf_file"
  fi
}

configure_ann_experiment_conf() {
  local conf_file="$1"

  configure_cubrid_base_conf "$conf_file"

  if (( ANN_SINGLE_CORE_EXPERIMENT != 1 )); then
    return
  fi

  log "applying single-core ANN experiment overrides to $conf_file"
  set_cubrid_conf_param "max_clients" "$ANN_EXPERIMENT_MAX_CLIENTS" "$conf_file"
  set_cubrid_conf_param "thread_core_count" "$ANN_EXPERIMENT_THREAD_CORE_COUNT" "$conf_file"
  set_cubrid_conf_param "parallelism" "$ANN_EXPERIMENT_PARALLELISM" "$conf_file"
  set_cubrid_conf_param "max_parallel_workers" "$ANN_EXPERIMENT_MAX_PARALLEL_WORKERS" "$conf_file"
  set_cubrid_conf_param "auto_restart_server" "0" "$conf_file"
  set_cubrid_conf_param "vacuum_disable" "1" "$conf_file"
  set_cubrid_conf_param "ha_mode" "off" "$conf_file"
}

download_file_with_python() {
  local source_url="$1"
  local destination_path="$2"
  local destination_dir
  local tmp_file

  destination_dir="$(dirname "$destination_path")"
  mkdir -p "$destination_dir"
  tmp_file="$(mktemp "$destination_dir/.dataset_download.XXXXXX")"

  if ! python3 - "$source_url" "$tmp_file" <<'PY'
import sys
from urllib.request import build_opener, install_opener, urlretrieve

source_url = sys.argv[1]
destination_path = sys.argv[2]

opener = build_opener()
opener.addheaders = [("User-agent", "Mozilla/5.0")]
install_opener(opener)
urlretrieve(source_url, destination_path)
PY
  then
    rm -f "$tmp_file"
    return 1
  fi

  mv "$tmp_file" "$destination_path"
}

ensure_dataset_hdf5() {
  local dataset_url

  if [[ -f "$DATASET_HDF5" ]]; then
    return 0
  fi

  dataset_url="$(get_dataset_download_url)"
  log "dataset hdf5 not found: $DATASET_HDF5"
  log "downloading dataset from $dataset_url"

  if download_file_with_python "$dataset_url" "$DATASET_HDF5"; then
    log "downloaded dataset to $DATASET_HDF5"
    return 0
  fi

  printf 'failed to download dataset: %s -> %s\n' \
    "$dataset_url" "$DATASET_HDF5" >&2
  return 1
}

resolve_existing_file() {
  local candidate="$1"

  if [[ -n "$candidate" && -f "$candidate" ]]; then
    printf '%s\n' "$candidate"
  fi
}

has_word() {
  local needle="$1"
  shift || true
  local word

  for word in "$@"; do
    if [[ "$word" == "$needle" ]]; then
      return 0
    fi
  done

  return 1
}

perf_enabled_for_target() {
  local target="$1"

  if (( PERF_STAT_ENABLE != 1 )); then
    return 1
  fi

  has_word "$target" $PERF_TARGETS
}

perf_record_enabled_for_target() {
  local target="$1"

  if [[ -n "$PERF_RECORD_TARGETS" ]]; then
    has_word "$target" $PERF_RECORD_TARGETS
    return
  fi

  has_word "$target" $PERF_TARGETS
}

resolve_perf_stat_events() {
  local raw_events="$PERF_STAT_EVENTS"
  local event=""
  local test_log=""
  local -a supported=()
  local -a skipped=()
  local old_ifs="$IFS"

  if [[ -n "$PERF_STAT_EVENTS_RESOLVED" ]]; then
    printf '%s\n' "$PERF_STAT_EVENTS_RESOLVED"
    return
  fi

  IFS=','
  for event in $raw_events; do
    event="${event#"${event%%[![:space:]]*}"}"
    event="${event%"${event##*[![:space:]]}"}"
    if [[ -z "$event" ]]; then
      continue
    fi

    test_log="$(mktemp /tmp/perf_stat_event.XXXXXX)"
    if perf stat -x, -e "$event" -- true >/dev/null 2>"$test_log"; then
      supported+=("$event")
    else
      skipped+=("$event")
    fi
    rm -f "$test_log"
  done
  IFS="$old_ifs"

  if ((${#supported[@]} == 0)); then
    printf 'no supported perf stat events were resolved from: %s\n' "$raw_events" >&2
    exit 1
  fi

  PERF_STAT_EVENTS_RESOLVED="$(IFS=,; printf '%s' "${supported[*]}")"
  if ((${#skipped[@]} > 0)); then
    printf '[%s] %s\n' "$(date '+%F %T')" "skipping unsupported perf stat events: ${skipped[*]}" >&2
  fi
  printf '[%s] %s\n' "$(date '+%F %T')" "resolved perf stat events: $PERF_STAT_EVENTS_RESOLVED" >&2
  printf '%s\n' "$PERF_STAT_EVENTS_RESOLVED"
}

sanitize_perf_label() {
  printf '%s' "$1" | tr -c '[:alnum:]_.-' '_'
}

is_stat_profile() {
  [[ "$1" == "stat" || "$1" == "stat-default" ]]
}

is_record_profile() {
  case "$1" in
    hot|instructions|branch|cache|custom)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

expand_perf_profiles() {
  local raw_profiles
  local profile

  if [[ -n "$PERF_PROFILES" ]]; then
    raw_profiles="$PERF_PROFILES"
  elif [[ -n "$PERF_RECORD_PROFILE" ]]; then
    raw_profiles="$PERF_RECORD_PROFILE"
  elif (( PERF_STAT_ENABLE == 1 )); then
    raw_profiles="stat hot"
  else
    raw_profiles="hot"
  fi

  for profile in $raw_profiles; do
    case "$profile" in
      all)
        printf '%s\n' stat hot instructions branch cache
        ;;
      stat-default)
        printf '%s\n' stat
        ;;
      stat|hot|instructions|branch|cache|custom)
        printf '%s\n' "$profile"
        ;;
      *)
        printf 'unknown perf profile: %s\n' "$profile" >&2
        exit 1
        ;;
    esac
  done
}

get_perf_record_event() {
  local profile="$1"

  if [[ "$profile" == "custom" && -n "$PERF_RECORD_EVENT" ]]; then
    printf '%s\n' "$PERF_RECORD_EVENT"
    return
  fi

  case "$profile" in
    hot)
      printf 'cycles\n'
      ;;
    instructions)
      printf 'instructions\n'
      ;;
    branch)
      printf 'branch-misses\n'
      ;;
    cache)
      printf 'cache-misses\n'
      ;;
    custom)
      if [[ -n "$PERF_RECORD_EVENT" ]]; then
        printf '%s\n' "$PERF_RECORD_EVENT"
      else
        printf 'PERF_RECORD_EVENT must be set when PERF profile is custom\n' >&2
        exit 1
      fi
      ;;
    *)
      printf 'unknown PERF_RECORD_PROFILE: %s\n' "$profile" >&2
      exit 1
      ;;
  esac
}

append_perf_profile_suffix() {
  local label="$1"
  local profile="$2"

  printf '%s.%s\n' "$label" "$profile"
}

get_cub_server_pid() {
  ps -eo pid=,comm=,args= | awk -v db_name="$DB_NAME" '
    $2 == "cub_server" && index($0, db_name) {
      print $1
      exit
    }
  '
}

log_server_cpu_affinity() {
  local server_pid=""

  if (( ANN_TASKSET_ENABLE != 1 )); then
    return
  fi

  server_pid="$(get_cub_server_pid || true)"
  if [[ -z "$server_pid" ]]; then
    log "could not find cub_server pid to verify cpu affinity"
    return
  fi

  log "cub_server cpu affinity: $(taskset -pc "$server_pid" 2>/dev/null | tr '\n' ' ' | sed 's/  */ /g; s/ $//')"
}

ensure_db_access() {
  local server_pid=""

  if run_csql -u "$DB_USER" -q -N "$DB_NAME" -c "SELECT 1;" >/dev/null 2>&1; then
    return
  fi

  log "database access check failed for $DB_NAME; attempting to restore cub_master/server access"
  run_cubrid server start "$DB_NAME" >/dev/null 2>&1 || true

  if run_csql -u "$DB_USER" -q -N "$DB_NAME" -c "SELECT 1;" >/dev/null 2>&1; then
    return
  fi

  server_pid="$(get_cub_server_pid || true)"
  if [[ -n "$server_pid" ]]; then
    log "restarting orphaned cub_server pid=$server_pid for $DB_NAME"
    kill -TERM "$server_pid" >/dev/null 2>&1 || true
    sleep 2
  fi

  run_cubrid server start "$DB_NAME" >/dev/null 2>&1 || true

  if ! run_csql -u "$DB_USER" -q -N "$DB_NAME" -c "SELECT 1;" >/dev/null 2>&1; then
    printf 'failed to restore database access for %s\n' "$DB_NAME" >&2
    exit 1
  fi
}

start_perf_stat_attach() {
  local pid="$1"
  local output_file="$2"
  local log_file="${output_file}.log"
  local first_pid="${pid%%,*}"
  local resolved_events=""

  if ! kill -0 "$first_pid" 2>/dev/null; then
    return 1
  fi

  resolved_events="$(resolve_perf_stat_events)"
  perf stat \
    -x, \
    -e "$resolved_events" \
    -p "$pid" \
    -o "$output_file" >"$log_file" 2>&1 &
  echo $!
}

start_perf_record_attach() {
  local pid="$1"
  local output_file="$2"
  local profile="$3"
  local log_file="${output_file}.log"
  local record_event
  local -a perf_cmd
  local first_pid="${pid%%,*}"

  if ! kill -0 "$first_pid" 2>/dev/null; then
    return 1
  fi

  record_event="$(get_perf_record_event "$profile")"
  perf_cmd=(
    perf record
    -g
    --call-graph "$PERF_CALL_GRAPH"
    -e "$record_event"
    -p "$pid"
    -o "$output_file"
  )

  if [[ -n "$PERF_RECORD_PERIOD" ]]; then
    perf_cmd+=(-c "$PERF_RECORD_PERIOD")
  else
    perf_cmd+=(-F "$PERF_RECORD_FREQ")
  fi

  "${perf_cmd[@]}" >"$log_file" 2>&1 &
  echo $!
}

prepare_perf_output_dir() {
  local archive_dir
  local ts

  if [[ ! -d "$PERF_OUTPUT_DIR" ]]; then
    return
  fi

  if [[ -z "$(find "$PERF_OUTPUT_DIR" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]]; then
    return
  fi

  ts="$(date '+%Y%m%d_%H%M%S')"
  archive_dir="${PERF_OUTPUT_DIR}.old.${ts}"
  mv "$PERF_OUTPUT_DIR" "$archive_dir"
  log "archived previous perf output to $archive_dir"
}

stop_perf_background_job() {
  local perf_pid="$1"
  local waited=0

  if [[ -z "$perf_pid" ]]; then
    return
  fi

  if kill -0 "$perf_pid" 2>/dev/null; then
    kill -INT "$perf_pid" 2>/dev/null || true
  fi

  while kill -0 "$perf_pid" 2>/dev/null && (( waited < 50 )); do
    sleep 0.1
    waited=$((waited + 1))
  done

  if kill -0 "$perf_pid" 2>/dev/null; then
    kill -TERM "$perf_pid" 2>/dev/null || true
  fi

  wait "$perf_pid" 2>/dev/null || true
}

flamegraph_tools_ready() {
  [[ -f "$FLAMEGRAPH_DIR/flamegraph.pl" && -f "$FLAMEGRAPH_DIR/stackcollapse-perf.pl" ]]
}

download_flamegraph_with_git() {
  if ! command -v git >/dev/null 2>&1; then
    return 1
  fi

  rm -rf "$FLAMEGRAPH_DIR"
  git clone --depth 1 "$FLAMEGRAPH_REPO_URL" "$FLAMEGRAPH_DIR"
}

download_flamegraph_with_tarball() {
  local archive_file
  local tmp_dir
  local extracted_dir

  archive_file="$(mktemp /tmp/flamegraph.XXXXXX.tar.gz)"
  tmp_dir="$(mktemp -d /tmp/flamegraph.XXXXXX)"

  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$FLAMEGRAPH_TARBALL_URL" -o "$archive_file"
  elif command -v wget >/dev/null 2>&1; then
    wget -q -O "$archive_file" "$FLAMEGRAPH_TARBALL_URL"
  else
    rm -f "$archive_file"
    rmdir "$tmp_dir"
    return 1
  fi

  tar -xzf "$archive_file" -C "$tmp_dir"
  extracted_dir="$(find "$tmp_dir" -mindepth 1 -maxdepth 1 -type d | head -n 1)"

  if [[ -z "$extracted_dir" ]]; then
    rm -f "$archive_file"
    rm -rf "$tmp_dir"
    return 1
  fi

  rm -rf "$FLAMEGRAPH_DIR"
  mv "$extracted_dir" "$FLAMEGRAPH_DIR"
  rm -f "$archive_file"
  rm -rf "$tmp_dir"
}

ensure_flamegraph_tools() {
  if (( PERF_FLAMEGRAPH != 1 )); then
    return
  fi

  if flamegraph_tools_ready; then
    return
  fi

  mkdir -p "$(dirname "$FLAMEGRAPH_DIR")"
  log "flame graph tools not found under $FLAMEGRAPH_DIR; downloading"

  if download_flamegraph_with_git || download_flamegraph_with_tarball; then
    if flamegraph_tools_ready; then
      log "downloaded flame graph tools to $FLAMEGRAPH_DIR"
      return
    fi
  fi

  printf 'failed to provision FlameGraph tools in %s\n' "$FLAMEGRAPH_DIR" >&2
  exit 1
}

resolve_stackcollapse_perf() {
  local candidate=""

  candidate="$(resolve_existing_file "$STACKCOLLAPSE_PERF")"
  if [[ -n "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return
  fi

  candidate="$(resolve_existing_file "$FLAMEGRAPH_DIR/stackcollapse-perf.pl")"
  if [[ -n "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return
  fi

  candidate="$(resolve_existing_file "$SCRIPT_DIR/../FlameGraph/stackcollapse-perf.pl")"
  if [[ -n "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return
  fi

  candidate="$(command -v stackcollapse-perf.pl 2>/dev/null || true)"
  if [[ -n "$candidate" ]]; then
    printf '%s\n' "$candidate"
  fi
}

resolve_flamegraph_pl() {
  local candidate=""

  candidate="$(resolve_existing_file "$FLAMEGRAPH_PL")"
  if [[ -n "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return
  fi

  candidate="$(resolve_existing_file "$FLAMEGRAPH_DIR/flamegraph.pl")"
  if [[ -n "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return
  fi

  candidate="$(resolve_existing_file "$SCRIPT_DIR/../FlameGraph/flamegraph.pl")"
  if [[ -n "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return
  fi

  candidate="$(command -v flamegraph.pl 2>/dev/null || true)"
  if [[ -n "$candidate" ]]; then
    printf '%s\n' "$candidate"
  fi
}

generate_flamegraph() {
  local perf_data_file="$1"
  local svg_file="$2"
  local stackcollapse_perf
  local flamegraph_pl
  local tmp_folded_file
  local tmp_svg_file
  local perf_report_output

  if (( PERF_FLAMEGRAPH != 1 )); then
    log "skipping flame graph for $perf_data_file: PERF_FLAMEGRAPH=0"
    return
  fi

  if [[ ! -s "$perf_data_file" ]]; then
    log "skipping flame graph for $perf_data_file: perf data file is missing or empty"
    return
  fi

  log "generating flame graph from $perf_data_file -> $svg_file"

  ensure_flamegraph_tools

  stackcollapse_perf="$(resolve_stackcollapse_perf)"
  flamegraph_pl="$(resolve_flamegraph_pl)"

  if [[ -z "$stackcollapse_perf" || -z "$flamegraph_pl" ]]; then
    log "skipping flame graph for $perf_data_file: flamegraph tools not found"
    return
  fi

  perf_report_output="$(perf report --stdio -i "$perf_data_file" 2>&1 | sed -n '1,20p' || true)"
  if grep -q 'data has no samples' <<<"$perf_report_output"; then
    log "skipping flame graph for $perf_data_file: perf data has no samples"
    return
  fi

  tmp_folded_file="$(mktemp /tmp/nytimes_flamegraph_folded.XXXXXX)"
  tmp_svg_file="$(mktemp /tmp/nytimes_flamegraph.XXXXXX.svg)"
  if ! perf script -i "$perf_data_file" \
    | perl "$stackcollapse_perf" > "$tmp_folded_file"; then
    rm -f "$tmp_folded_file" "$tmp_svg_file"
    log "failed to collapse perf stacks for $perf_data_file"
    return
  fi

  if [[ ! -s "$tmp_folded_file" ]]; then
    rm -f "$tmp_folded_file" "$tmp_svg_file"
    log "skipping flame graph for $perf_data_file: collapsed stack output is empty"
    return
  fi

  if ! perl "$flamegraph_pl" --title "$(basename "$svg_file" .svg)" < "$tmp_folded_file" > "$tmp_svg_file"; then
    rm -f "$tmp_folded_file" "$tmp_svg_file"
    log "failed to generate flame graph from collapsed stacks for $perf_data_file"
    return
  fi

  rm -f "$tmp_folded_file"

  if [[ ! -s "$tmp_svg_file" ]]; then
    rm -f "$tmp_svg_file"
    log "failed to generate flame graph from $perf_data_file: svg output is empty"
    return
  fi

  if grep -q 'ERROR: No valid input provided to flamegraph.pl' "$tmp_svg_file"; then
    rm -f "$tmp_svg_file"
    log "skipping flame graph for $perf_data_file: flamegraph input was empty"
    return
  fi

  mv "$tmp_svg_file" "$svg_file"
  log "saved flame graph to $svg_file"
}

write_perf_report() {
  local perf_data_file="$1"
  local report_file="$2"

  if [[ ! -s "$perf_data_file" ]]; then
    return
  fi

  perf report --stdio -i "$perf_data_file" > "$report_file" 2>/dev/null || true
}

write_perf_folded() {
  local perf_data_file="$1"
  local folded_file="$2"
  local stackcollapse_perf

  if [[ ! -s "$perf_data_file" ]]; then
    return
  fi

  ensure_flamegraph_tools
  stackcollapse_perf="$(resolve_stackcollapse_perf)"
  if [[ -z "$stackcollapse_perf" ]]; then
    return
  fi

  perf script -i "$perf_data_file" | perl "$stackcollapse_perf" > "$folded_file" 2>/dev/null || true
}

write_perf_folded_active_only() {
  local perf_data_file="$1"
  local folded_file="$2"
  local stackcollapse_perf

  if [[ ! -s "$perf_data_file" ]]; then
    return
  fi

  ensure_flamegraph_tools
  stackcollapse_perf="$(resolve_stackcollapse_perf)"
  if [[ -z "$stackcollapse_perf" ]]; then
    return
  fi

  perf script -i "$perf_data_file" \
    | perl "$stackcollapse_perf" \
    | awk '
        /__poll/ { next }
        /__libc_recv/ { next }
        /css_receive_data_from_server_with_timeout/ { next }
        /css_receive_data/ { next }
        /css_net_recv/ { next }
        /poll_schedule_timeout/ { next }
        /schedule_hrtimeout/ { next }
        { print }
      ' > "$folded_file" 2>/dev/null || true
}

generate_flamegraph_from_folded() {
  local folded_file="$1"
  local svg_file="$2"
  local flamegraph_pl
  local tmp_svg_file

  if (( PERF_FLAMEGRAPH != 1 )); then
    return
  fi

  if [[ ! -s "$folded_file" ]]; then
    return
  fi

  ensure_flamegraph_tools
  flamegraph_pl="$(resolve_flamegraph_pl)"
  if [[ -z "$flamegraph_pl" ]]; then
    return
  fi

  tmp_svg_file="$(mktemp /tmp/nytimes_flamegraph.filtered.XXXXXX.svg)"
  if ! perl "$flamegraph_pl" --title "$(basename "$svg_file" .svg)" < "$folded_file" > "$tmp_svg_file"; then
    rm -f "$tmp_svg_file"
    return
  fi

  if [[ ! -s "$tmp_svg_file" ]]; then
    rm -f "$tmp_svg_file"
    return
  fi

  if grep -q 'ERROR: No valid input provided to flamegraph.pl' "$tmp_svg_file"; then
    rm -f "$tmp_svg_file"
    return
  fi

  mv "$tmp_svg_file" "$svg_file"
  log "saved flame graph to $svg_file"
}

run_csql_input_with_perf() {
  local sql_file="$1"
  local out_file="$2"
  local label="$3"
  local profile
  local output_label
  local safe_output_label
  local csql_pid=""
  local csql_status=0
  local server_pid=""
  local perf_pid=""
  local perf_data_file=""
  local flamegraph_file=""
  local -a perf_job_pids=()
  local -a flamegraph_data_files=()
  local -a flamegraph_svg_files=()
  local safe_label=""
  local csql_time_file=""
  local server_before_file=""
  local server_after_file=""
  local cas_before_file=""
  local cas_after_file=""

  safe_label="$(sanitize_perf_label "$label")"

  if (( SEGMENT_PROFILE_ENABLE == 1 )); then
    ensure_segment_profile_dir
    csql_time_file="$SEGMENT_PROFILE_DIR/${safe_label}.csql.time"
    server_before_file="$SEGMENT_PROFILE_DIR/${safe_label}.cub_server.before.json"
    server_after_file="$SEGMENT_PROFILE_DIR/${safe_label}.cub_server.after.json"
    cas_before_file="$SEGMENT_PROFILE_DIR/${safe_label}.cub_cas.before.json"
    cas_after_file="$SEGMENT_PROFILE_DIR/${safe_label}.cub_cas.after.json"
    snapshot_process_group_stats "cub_server" "$server_before_file"
    snapshot_process_group_stats "cub_cas" "$cas_before_file"
  fi

  if (( PERF_ENABLE != 1 )); then
    ensure_db_access

    if (( SEGMENT_PROFILE_ENABLE == 1 )); then
      run_csql_timed_to_file \
        "$csql_time_file" \
        "$out_file" \
        -u "$DB_USER" -q -N --delimiter='|' "$DB_NAME" -i "$sql_file"
      snapshot_process_group_stats "cub_server" "$server_after_file"
      snapshot_process_group_stats "cub_cas" "$cas_after_file"
      write_segment_profile_summary \
        "$safe_label" \
        "$csql_time_file" \
        "$server_before_file" \
        "$server_after_file" \
        "$cas_before_file" \
        "$cas_after_file"
      return
    fi

    run_csql -u "$DB_USER" -q -N --delimiter='|' "$DB_NAME" -i "$sql_file" > "$out_file"
    return
  fi

  mkdir -p "$PERF_OUTPUT_DIR"
  ensure_db_access

  run_csql -u "$DB_USER" -q -N --delimiter='|' "$DB_NAME" -i "$sql_file" > "$out_file" &
  csql_pid=$!

  while IFS= read -r profile; do
    if is_stat_profile "$profile" && perf_enabled_for_target csql; then
      output_label="$(append_perf_profile_suffix "$label" "$profile")"
      safe_output_label="$(sanitize_perf_label "$output_label")"
      perf_pid="$(
        start_perf_stat_attach "$csql_pid" "$PERF_OUTPUT_DIR/${safe_output_label}.csql.stat.csv" || true
      )"
      if [[ -n "$perf_pid" ]]; then
        perf_job_pids+=("$perf_pid")
      fi
    elif is_record_profile "$profile" && perf_record_enabled_for_target csql; then
      output_label="$(append_perf_profile_suffix "$label" "$profile")"
      safe_output_label="$(sanitize_perf_label "$output_label")"
      perf_data_file="$PERF_OUTPUT_DIR/${safe_output_label}.csql.data"
      perf_pid="$(
        start_perf_record_attach "$csql_pid" "$perf_data_file" "$profile" || true
      )"
      if [[ -n "$perf_pid" ]]; then
        perf_job_pids+=("$perf_pid")
        flamegraph_data_files+=("$perf_data_file")
        flamegraph_svg_files+=("$PERF_OUTPUT_DIR/${safe_output_label}.csql.flamegraph.svg")
      fi
    fi
  done < <(expand_perf_profiles)

  if perf_enabled_for_target cub_server || perf_record_enabled_for_target cub_server; then
    server_pid="$(get_cub_server_pid || true)"

    if [[ -n "$server_pid" ]]; then
      while IFS= read -r profile; do
        if is_stat_profile "$profile" && perf_enabled_for_target cub_server; then
          output_label="$(append_perf_profile_suffix "$label" "$profile")"
          safe_output_label="$(sanitize_perf_label "$output_label")"
          perf_pid="$(
            start_perf_stat_attach "$server_pid" "$PERF_OUTPUT_DIR/${safe_output_label}.cub_server.stat.csv" || true
          )"
          if [[ -n "$perf_pid" ]]; then
            perf_job_pids+=("$perf_pid")
          fi
        elif is_record_profile "$profile" && perf_record_enabled_for_target cub_server; then
          output_label="$(append_perf_profile_suffix "$label" "$profile")"
          safe_output_label="$(sanitize_perf_label "$output_label")"
          perf_data_file="$PERF_OUTPUT_DIR/${safe_output_label}.cub_server.data"
          perf_pid="$(
            start_perf_record_attach "$server_pid" "$perf_data_file" "$profile" || true
          )"
          if [[ -n "$perf_pid" ]]; then
            perf_job_pids+=("$perf_pid")
            flamegraph_data_files+=("$perf_data_file")
            flamegraph_svg_files+=("$PERF_OUTPUT_DIR/${safe_output_label}.cub_server.flamegraph.svg")
          fi
        fi
      done < <(expand_perf_profiles)
    else
      log "perf requested for cub_server, but no PID was found for $DB_NAME"
    fi
  fi

  set +e
  wait "$csql_pid"
  csql_status=$?
  set -e

  for perf_pid in "${perf_job_pids[@]}"; do
    stop_perf_background_job "$perf_pid"
  done

  for ((i = 0; i < ${#flamegraph_data_files[@]}; i++)); do
    generate_flamegraph \
      "${flamegraph_data_files[i]}" \
      "${flamegraph_svg_files[i]}"
  done

  if (( csql_status != 0 )); then
    return "$csql_status"
  fi

  if (( SEGMENT_PROFILE_ENABLE == 1 )); then
    log "segment profiling summary is only collected when PERF_ENABLE=0"
  fi
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

write_load_schema_file() {
  local load_schema_file="$1"

  awk '
    BEGIN {
      removed = 0
    }
    /^[[:space:]]*CREATE[[:space:]]+VECTOR[[:space:]]+INDEX[[:space:]]/ {
      removed = 1
      next
    }
    {
      print
    }
    END {
      if (removed) {
        printf "removed CREATE VECTOR INDEX statements from load schema\n" > "/dev/stderr"
      }
    }
  ' "$SCHEMA_FILE" > "$load_schema_file"
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

write_load_object_file() {
  local load_object_file="$1"

  if [[ -z "$TRAIN_ROW_LIMIT" ]]; then
    printf '%s\n' "$OBJECT_FILE"
    return
  fi

  printf '[%s] limiting nytimes_256_angular_train rows to %s for load\n' \
    "$(date '+%F %T')" \
    "$TRAIN_ROW_LIMIT" >&2
  perl -ne '
    BEGIN {
      $limit = $ENV{"TRAIN_ROW_LIMIT"};
      $train_count = 0;
      $class = "";
    }

    if (/^%class\s+(\S+)/) {
      $class = $1;
      print;
      next;
    }

    if ($class eq q{nytimes_256_angular_train} && /^[0-9]/) {
      if ($train_count >= $limit) {
        next;
      }
      $train_count++;
      print;
      next;
    }

    print;
  ' "$OBJECT_FILE" > "$load_object_file"

  printf '%s\n' "$load_object_file"
}

sql_capture() {
  local query="$1"
  ensure_db_access
  run_csql -u "$DB_USER" -q -N --delimiter='|' "$DB_NAME" -c "$query"
}

sql_exec() {
  local query="$1"
  ensure_db_access
  run_csql -u "$DB_USER" "$DB_NAME" -c "$query"
}

get_query_vector_literal() {
  local query_id="$1"

  sql_capture "
    SELECT CAST(vec AS STRING)
      FROM nytimes_256_angular_test
     WHERE id = ${query_id};
  " | awk -F'|' 'NF > 0 {print $1; exit}'
}

validate_vector_index_scan() {
  local query_id="$1"
  local vector_literal=""
  local trace_output=""

  vector_literal="$(get_query_vector_literal "$query_id")"
  if [[ -z "$vector_literal" ]]; then
    printf 'failed to fetch vector literal for query id %s\n' "$query_id" >&2
    exit 1
  fi

  trace_output="$(
    printf ';trace on text\nSELECT /*+ recompile no_parallel_heap_scan */ id FROM nytimes_256_angular_train ORDER BY vec <c> CAST(%s AS vector) LIMIT 3;\n' \
      "$vector_literal" \
      | run_csql -u "$DB_USER" "$DB_NAME"
  )"

  if ! grep -q 'VECTOR INDEX SCAN' <<<"$trace_output"; then
    printf 'vector index scan validation failed for query id %s\n' "$query_id" >&2
    printf '%s\n' "$trace_output" >&2
    exit 1
  fi

  log "validated vector index usage for query id $query_id"
}

validate_required_classes() {
  local missing_classes=""
  local required_classes=(
    nytimes_256_angular_train
    nytimes_256_angular_test
    nytimes_256_angular_answer
  )
  local class_name
  local found

  for class_name in "${required_classes[@]}"; do
    found="$(
      sql_capture "
        SELECT class_name
          FROM db_class
         WHERE class_name = '${class_name}';
      " | awk -F'|' 'NF > 0 {print $1; exit}'
    )"

    if [[ "$found" != "$class_name" ]]; then
      missing_classes+=" $class_name"
    fi
  done

  if [[ -n "$missing_classes" ]]; then
    printf 'required dataset classes are missing in %s:%s\n' \
      "$DB_NAME" \
      "$missing_classes" >&2
    printf 'expected classes: nytimes_256_angular_train, nytimes_256_angular_test, nytimes_256_angular_answer\n' >&2
    return 1
  fi
}

invalidate_dataset_cache() {
  log "invalidating cached query/results artifacts for $DATASET_NAME"
  rm -f \
    "$QUERY_ID_FILE" \
    "$EXCLUDED_QUERY_FILE" \
    "$QUERY_ID_CACHE_MARKER" \
    "$GT_CACHE_FILE" \
    "$GT_CACHE_MARKER" \
    "$RESULT_CSV" \
    "$RESULT_SVG"
}

prepare_ground_truth_cache() {
  local total_queries

  total_queries="${#QUERY_IDS[@]}"

  if [[ -s "$GT_CACHE_FILE" && -f "$GT_CACHE_MARKER" && "$GT_CACHE_MARKER" -nt "$QUERY_ID_FILE" ]]; then
    log "reusing cached ground-truth neighbors from $GT_CACHE_FILE"
    return
  fi

  log "fetching ground-truth neighbors for ${total_queries} queries with one set-based query"
  sql_capture "
    SELECT 2, id, neighbor_id
      FROM (
        SELECT id,
               neighbor_id,
               ROW_NUMBER() OVER (PARTITION BY id ORDER BY neighbor_distance) AS rn
          FROM nytimes_256_angular_answer
         WHERE neighbor_distance IS NOT NULL
      ) gt
     WHERE rn <= ${TOPK}
     ORDER BY id, rn;
  " > "$GT_CACHE_FILE"

  if [[ ! -s "$GT_CACHE_FILE" ]]; then
    printf 'failed to fetch ground-truth neighbors from nytimes_256_angular_answer\n' >&2
    exit 1
  fi

  touch "$GT_CACHE_MARKER"
  log "cached ground-truth neighbors to $GT_CACHE_FILE"
}

setup_demo_db() {
  local cubrid_conf

  if [[ -z "${CUBRID:-}" ]]; then
    printf 'CUBRID environment variable is not set.\n' >&2
    exit 1
  fi

  cubrid_conf="$CUBRID/conf/cubrid.conf"
  configure_cubrid_base_conf "$cubrid_conf"

  run_cubrid server stop "$DB_NAME" || true
  run_cubrid deletedb "$DB_NAME" || true

  (
    cd "$CUBRID/demo"
    run_make_cubrid_demo
  )

  configure_cubrid_base_conf "$cubrid_conf"
  run_cubrid_server_start "$DB_NAME"
  log_server_cpu_affinity
}

restart_db_for_ann_experiment() {
  local cubrid_conf
  local need_restart=0

  if (( ANN_SINGLE_CORE_EXPERIMENT == 1 )); then
    need_restart=1
  fi

  if (( ANN_HNSW_DEBUG_ENABLE == 1 )) && (( PERF_ENABLE == 1 || SEGMENT_PROFILE_ENABLE == 1 || CAS_TIMESTAMP_PROFILE_ENABLE == 1 )); then
    need_restart=1
  fi

  if (( need_restart != 1 )); then
    return
  fi

  cubrid_conf="$CUBRID/conf/cubrid.conf"
  if (( ANN_SINGLE_CORE_EXPERIMENT == 1 )); then
    configure_ann_experiment_conf "$cubrid_conf"
    log "restarting $DB_NAME with single-core ANN experiment configuration"
  else
    configure_cubrid_base_conf "$cubrid_conf"
    log "restarting $DB_NAME with profiling configuration"
  fi

  run_cubrid server stop "$DB_NAME"
  run_cubrid_server_start "$DB_NAME"
  log_server_cpu_affinity
}

load_dataset() {
  local load_schema_file
  local load_object_file
  local object_input_file

  load_schema_and_object_files() {
    sanitize_schema_file
    sanitize_object_file
    load_schema_file="$(mktemp /tmp/nytimes_load_schema.XXXXXX)"
    load_object_file="$(mktemp /tmp/nytimes_load_object.XXXXXX)"
    write_load_schema_file "$load_schema_file"
    object_input_file="$(write_load_object_file "$load_object_file")"
    log "loading dataset from $SCHEMA_FILE / $OBJECT_FILE"
    run_cubrid loaddb -s "$load_schema_file" -d "$object_input_file" -C -u "$DB_USER" "$DB_NAME" -v --no-statistics
    rm -f "$load_schema_file"
    if [[ "$object_input_file" == "$load_object_file" ]]; then
      rm -f "$load_object_file"
    fi
    return
  }

  if [[ -f "$SCHEMA_FILE" && -f "$OBJECT_FILE" ]]; then
    load_schema_and_object_files
    return
  fi

  if ensure_dataset_hdf5; then
    log "converting hdf5 dataset to loaddb files from $DATASET_HDF5"
    run_cubrid loaddb -h "$DATASET_HDF5" -C -u "$DB_USER" "$DB_NAME" --no-statistics

    if [[ -f "$SCHEMA_FILE" && -f "$OBJECT_FILE" ]]; then
      load_schema_and_object_files
      return
    fi

    printf 'dataset conversion did not produce expected files: %s, %s\n' \
      "$SCHEMA_FILE" "$OBJECT_FILE" >&2
    exit 1
  fi

  printf 'dataset not found or download failed. checked: %s, %s, %s\n' \
    "$DATASET_HDF5" "$SCHEMA_FILE" "$OBJECT_FILE" >&2
  exit 1
}

build_index() {
  local out_file

  log "creating vector index (M=$HNSW_M, ef_construction=$HNSW_EF_CONSTRUCTION)"

  out_file="$(mktemp /tmp/nytimes_build_index_out.XXXXXX)"
  run_cci_runner_with_profile \
    "$out_file" \
    "build_index_m${HNSW_M}_efc${HNSW_EF_CONSTRUCTION}" \
    build-index \
    "cci:cubrid:localhost:${BROKER_PORT}:${DB_NAME}:::" \
    "$DB_USER" \
    "$DB_PASS" \
    "$HNSW_M" \
    "$HNSW_EF_CONSTRUCTION"
  rm -f "$out_file"
}

prepare_query_ids() {
  local tmp_test_ids_file
  local tmp_valid_ids_file

  if [[ -s "$QUERY_ID_FILE" && -s "$EXCLUDED_QUERY_FILE" && -f "$QUERY_ID_CACHE_MARKER" ]]; then
    EXCLUDED_QUERY_COUNT="$(grep -vc '^#' "$EXCLUDED_QUERY_FILE" || true)"
    mapfile -t QUERY_IDS < <(grep -E '^-?[0-9]+$' "$QUERY_ID_FILE")

    if (( ${#QUERY_IDS[@]} > 0 )); then
      log "reusing cached query ids: ${#QUERY_IDS[@]} valid queries, ${EXCLUDED_QUERY_COUNT} excluded"
      return
    fi
  fi

  log "building query id cache from nytimes_256_angular_test/answer"

  # Queries whose vector is effectively invalid/null must be excluded.
  # They do not participate in vector indexing, and they should not be used
  # as ANN queries either. In this dataset, those cases appear as answer rows
  # with NULL neighbor_distance, so we keep only ids with at least one valid
  # ground-truth distance and record the excluded ids separately.
  tmp_test_ids_file="$(mktemp /tmp/nytimes_test_ids.XXXXXX)"
  tmp_valid_ids_file="$(mktemp /tmp/nytimes_valid_ids.XXXXXX)"

  log "fetching all test query ids"
  sql_capture "
    SELECT id
      FROM nytimes_256_angular_test
     ORDER BY id;
  " \
    | awk -F'|' '/^-?[0-9]+([|].*)?$/ {print $1}' > "$tmp_test_ids_file"

  if [[ ! -s "$tmp_test_ids_file" ]]; then
    rm -f "$tmp_test_ids_file" "$tmp_valid_ids_file"
    printf 'failed to fetch query ids from nytimes_256_angular_test\n' >&2
    exit 1
  fi

  log "fetching valid query ids from nytimes_256_angular_answer"
  sql_capture "
    SELECT id
      FROM nytimes_256_angular_answer
     WHERE neighbor_distance IS NOT NULL
     GROUP BY id
     ORDER BY id;
  " \
    | awk -F'|' '/^-?[0-9]+([|].*)?$/ {print $1}' > "$tmp_valid_ids_file"

  if [[ ! -s "$tmp_valid_ids_file" ]]; then
    rm -f "$tmp_test_ids_file" "$tmp_valid_ids_file"
    printf 'failed to fetch valid query ids from nytimes_256_angular_answer\n' >&2
    exit 1
  fi

  log "writing cached valid/excluded query id lists"

  {
    printf '# Excluded nytimes queries\n'
    printf '# reason: null/invalid vector queries are excluded from indexing and should also be excluded from ANN recall evaluation.\n'
    printf '# criterion: no non-NULL ground-truth neighbor_distance exists in nytimes_256_angular_answer.\n'
    awk 'NR == FNR {valid[$1] = 1; next} !($1 in valid) {print $1}' \
      "$tmp_valid_ids_file" \
      "$tmp_test_ids_file"
  } > "$EXCLUDED_QUERY_FILE"

  cp "$tmp_valid_ids_file" "$QUERY_ID_FILE"
  rm -f "$tmp_test_ids_file" "$tmp_valid_ids_file"

  if [[ ! -s "$QUERY_ID_FILE" ]]; then
    printf 'failed to fetch valid query ids from nytimes_256_angular_test\n' >&2
    exit 1
  fi

  touch "$QUERY_ID_CACHE_MARKER"

  EXCLUDED_QUERY_COUNT="$(grep -vc '^#' "$EXCLUDED_QUERY_FILE" || true)"
  log "excluded queries: ${EXCLUDED_QUERY_COUNT} (saved to $EXCLUDED_QUERY_FILE)"
  log "cached valid query ids to $QUERY_ID_FILE"

  mapfile -t QUERY_IDS < "$QUERY_ID_FILE"

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
  local ann_out_file
  local total_queries
  local query_id_input_file="$QUERY_ID_FILE"
  local tmp_query_id_file=""
  local query_label=""
  local query_time_file=""

  if (( QUERY_SAMPLE_LIMIT > 0 )); then
    tmp_query_id_file="$(mktemp /tmp/nytimes_query_ids_sample.XXXXXX)"
    head -n "$QUERY_SAMPLE_LIMIT" "$QUERY_ID_FILE" > "$tmp_query_id_file"
    query_id_input_file="$tmp_query_id_file"
    total_queries="$(wc -l < "$query_id_input_file" | awk '{print $1}')"
  else
    total_queries="${#QUERY_IDS[@]}"
  fi

  if [[ -s "$query_id_input_file" ]]; then
    validate_vector_index_scan "$(head -n 1 "$query_id_input_file")"
  fi

  log "measuring recall@${TOPK} for ${total_queries} queries (hnsw_ef_search=$HNSW_EF_SEARCH)"
  start_ns="$(date +%s%N)"
  ann_out_file="$(mktemp /tmp/nytimes_measure_ann_out.XXXXXX)"
  query_label="query_ann_ef${HNSW_EF_SEARCH}_topk${TOPK}_q${total_queries}"
  query_time_file="$SEGMENT_PROFILE_DIR/$(sanitize_perf_label "$query_label").cci.time"

  log "running ANN queries for ${total_queries} test vectors"
  run_cci_runner_with_profile \
    "$ann_out_file" \
    "$query_label" \
    query-ann \
    "cci:cubrid:localhost:${BROKER_PORT}:${DB_NAME}:::" \
    "$DB_USER" \
    "$DB_PASS" \
    "$HNSW_EF_SEARCH" \
    "$TOPK" \
    "$query_id_input_file"

  log "aggregating ANN results against ground truth"
  while IFS='|' read -r line_type line_processed line_hits; do
    case "$line_type" in
      P)
        partial="$(awk -v hits="$line_hits" -v total="$((line_processed * TOPK))" 'BEGIN { printf "%.6f", hits / total }')"
        log "progress: ${line_processed}/${total_queries} queries, partial recall@${TOPK}=${partial}"
        ;;
      S)
        processed="$line_processed"
        total_hits="$line_hits"
        ;;
    esac
  done < <(
    awk -F'|' -v topk="$TOPK" -v progress_every="$PROGRESS_EVERY" '
      function flush_query(   hits, i, n, key) {
        if (curr_qid == "") {
          return
        }

        hits = 0
        n = split(ann_list, ann_arr, " ")
        for (i = 1; i <= n; i++) {
          key = curr_qid SUBSEP ann_arr[i]
          if (ann_arr[i] != "" && (key in gt_hits)) {
            hits++
          }
        }

        total_hits += hits
        processed++

        ann_list = ""
        curr_qid = ""
      }

      FILENAME == gt_file && $1 == 2 && $2 ~ /^-?[0-9]+$/ && $3 ~ /^-?[0-9]+$/ {
        qid = $2 + 0
        rid = $3 + 0
        gt_hits[qid SUBSEP rid] = 1
      }

      FILENAME == ann_file && $1 == 1 && $2 ~ /^-?[0-9]+$/ && $3 ~ /^-?[0-9]+$/ {
        qid = $2 + 0
        rid = $3 + 0

        if (curr_qid != "" && qid != curr_qid) {
          flush_query()
        }

        if (curr_qid == "") {
          curr_qid = qid
        }

        ann_list = ann_list " " rid
      }

      END {
        flush_query()
        printf "S|%d|%d\n", processed, total_hits
      }
    ' ann_file="$ann_out_file" gt_file="$GT_CACHE_FILE" "$GT_CACHE_FILE" "$ann_out_file"
  )

  rm -f "$ann_out_file" "$tmp_query_id_file"

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
  LAST_QUERY_LABEL="$query_label"

  if [[ -f "$query_time_file" ]]; then
    {
      printf 'queries=%d\n' "$processed"
      printf 'qps=%s\n' "$qps"
    } >> "$query_time_file"
  fi

  printf '\n'
  printf 'dataset=%s\n' "$DATASET_NAME"
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
  printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
    "$DATASET_NAME" \
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
  python3 - "$RESULT_CSV" "$RESULT_SVG" "$DATASET_NAME" <<'PY'
import csv
import math
import sys

csv_path, svg_path, dataset_name = sys.argv[1], sys.argv[2], sys.argv[3]
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
<text x="{left}" y="28" font-size="22" font-weight="700" fill="#111">{dataset_name} ef_search sweep</text>
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

prepare_final_results_dir() {
  local archive_dir=""
  local ts=""

  if (( FINAL_RESULTS_ENABLE != 1 )); then
    return
  fi

  if [[ -d "$FINAL_RESULTS_DIR" ]] && [[ -n "$(find "$FINAL_RESULTS_DIR" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]]; then
    ts="$(date '+%Y%m%d_%H%M%S')"
    archive_dir="${FINAL_RESULTS_DIR}.old.${ts}"
    mv "$FINAL_RESULTS_DIR" "$archive_dir"
    log "archived previous final results to $archive_dir"
  fi

  mkdir -p "$FINAL_RESULTS_DIR"
}

build_final_results_bundle() {
  local query_label="$1"
  local safe_label=""
  local query_time_file=""
  local query_summary_file=""
  local cas_summary_file=""
  local server_ts_summary_file=""
  local server_report_file=""
  local cas_report_file=""
  local server_flamegraph_file=""
  local cas_flamegraph_file=""
  local cas_active_flamegraph_file=""
  local build_server_stat_file=""
  local query_server_stat_file=""
  local build_segment_file=""
  local query_segment_file=""
  local cub_server_perf_summary_csv=""
  local cub_server_perf_summary_svg=""
  local env_manifest_file=""
  local git_manifest_file=""
  local command_file=""

  if (( FINAL_RESULTS_ENABLE != 1 )); then
    return
  fi

  if (( PERF_ENABLE != 1 || SEGMENT_PROFILE_ENABLE != 1 || CAS_TIMESTAMP_PROFILE_ENABLE != 1 )); then
    log "skipping final results bundle: PERF_ENABLE=1, SEGMENT_PROFILE_ENABLE=1, CAS_TIMESTAMP_PROFILE_ENABLE=1 are required"
    return
  fi

  safe_label="$(sanitize_perf_label "$query_label")"
  query_time_file="$SEGMENT_PROFILE_DIR/${safe_label}.cci.time"
  query_summary_file="$SEGMENT_PROFILE_DIR/${safe_label}.query_stage_breakdown.summary.csv"
  cas_summary_file="$SEGMENT_PROFILE_DIR/${safe_label}.cub_cas_ts.summary.csv"
  server_ts_summary_file="$SEGMENT_PROFILE_DIR/${safe_label}.cub_server_ts.summary.csv"
  server_report_file="$PERF_OUTPUT_DIR/${safe_label}.hot.cub_server.report.txt"
  cas_report_file="$PERF_OUTPUT_DIR/${safe_label}.hot.cub_cas.report.txt"
  server_flamegraph_file="$PERF_OUTPUT_DIR/${safe_label}.hot.cub_server.flamegraph.svg"
  cas_flamegraph_file="$PERF_OUTPUT_DIR/${safe_label}.hot.cub_cas.flamegraph.svg"
  cas_active_flamegraph_file="$PERF_OUTPUT_DIR/${safe_label}.hot.cub_cas.active.flamegraph.svg"
  build_server_stat_file="$PERF_OUTPUT_DIR/build_index_m24_efc200.stat.cub_server.stat.csv"
  query_server_stat_file="$PERF_OUTPUT_DIR/${safe_label}.stat.cub_server.stat.csv"
  build_segment_file="$SEGMENT_PROFILE_DIR/build_index_m24_efc200.csv"
  query_segment_file="$SEGMENT_PROFILE_DIR/${safe_label}.csv"
  cub_server_perf_summary_csv="$FINAL_RESULTS_DIR/cub_server_perf_stat_summary.csv"
  cub_server_perf_summary_svg="$FINAL_RESULTS_DIR/cub_server_perf_stat_summary.svg"

  prepare_final_results_dir
  write_cub_server_perf_stat_summary \
    "$build_server_stat_file" \
    "$query_server_stat_file" \
    "$build_segment_file" \
    "$query_segment_file" \
    "$cub_server_perf_summary_csv" \
    "$cub_server_perf_summary_svg"

  env_manifest_file="$(mktemp)"
  git_manifest_file="$(mktemp)"
  command_file="$(mktemp)"
  write_env_manifest_tsv "$env_manifest_file"
  write_git_manifest_tsv "$git_manifest_file"
  build_reproduction_command >"$command_file"

  python3 - \
    "$FINAL_RESULTS_DIR" \
    "$DATASET_NAME" \
    "$TOPK" \
    "$HNSW_M" \
    "$HNSW_EF_CONSTRUCTION" \
    "$HNSW_EF_SEARCH" \
    "$query_time_file" \
    "$query_summary_file" \
    "$cas_summary_file" \
    "$server_ts_summary_file" \
    "$server_report_file" \
    "$cas_report_file" \
    "$server_flamegraph_file" \
    "$cas_flamegraph_file" \
    "$cas_active_flamegraph_file" \
    "$env_manifest_file" \
    "$git_manifest_file" \
    "$command_file" <<'PY'
import csv
import re
import shutil
import sys
from pathlib import Path

(
    final_dir_arg,
    dataset_name,
    topk_arg,
    hnsw_m_arg,
    efc_arg,
    ef_search_arg,
    query_time_arg,
    query_summary_arg,
    cas_summary_arg,
    server_ts_summary_arg,
    server_report_arg,
    cas_report_arg,
    server_flamegraph_arg,
    cas_flamegraph_arg,
    cas_active_flamegraph_arg,
    env_manifest_arg,
    git_manifest_arg,
    command_arg,
) = sys.argv[1:]

final_dir = Path(final_dir_arg)
topk = int(topk_arg)
hnsw_m = int(hnsw_m_arg)
efc = int(efc_arg)
ef_search = int(ef_search_arg)
query_time_path = Path(query_time_arg)
query_summary_path = Path(query_summary_arg)
cas_summary_path = Path(cas_summary_arg)
server_ts_summary_path = Path(server_ts_summary_arg)
server_report_path = Path(server_report_arg)
cas_report_path = Path(cas_report_arg)
server_flamegraph_path = Path(server_flamegraph_arg)
cas_flamegraph_path = Path(cas_flamegraph_arg)
cas_active_flamegraph_path = Path(cas_active_flamegraph_arg)
env_manifest_path = Path(env_manifest_arg)
git_manifest_path = Path(git_manifest_arg)
command_path = Path(command_arg)

required = [
    query_time_path,
    query_summary_path,
    cas_summary_path,
    server_ts_summary_path,
    server_report_path,
    cas_report_path,
]
for path in required:
    if not path.is_file():
        raise SystemExit(f"missing required final artifact: {path}")

def write_placeholder_flamegraph(path: Path, title: str, detail: str):
    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="980" height="220" viewBox="0 0 980 220">
<rect width="100%" height="100%" fill="#fcfcfb"/>
<text x="40" y="46" font-size="28" font-weight="700" fill="#111">{title}</text>
<rect x="40" y="78" width="900" height="92" rx="14" fill="#f3efe7" stroke="#d8c8aa"/>
<text x="64" y="118" font-size="18" fill="#573600">Flamegraph was not generated because perf samples were empty for this run.</text>
<text x="64" y="148" font-size="15" fill="#6b7280">{detail}</text>
</svg>"""
    path.write_text(svg, encoding="utf-8")

def read_kv(path: Path):
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values

def read_csv_by_key(path: Path, key: str):
    rows = {}
    with path.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            rows[row[key]] = row
    return rows

def read_csv_by_two_keys(path: Path, key1: str, key2: str):
    rows = {}
    with path.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            rows[(row[key1], row[key2])] = row
    return rows

def read_tsv(path: Path):
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f, delimiter="\t"))

def find_pct(report_text: str, symbol: str):
    child_pattern = re.compile(r"^\s*[0-9]+\.[0-9]+%.*?--([0-9]+\.[0-9]+)%--" + re.escape(symbol) + r"(?:\(|$)", re.M)
    match = child_pattern.search(report_text)
    if match:
        return float(match.group(1))
    line_pattern = re.compile(r"^\s*([0-9]+\.[0-9]+)%.*?" + re.escape(symbol) + r"(?:\(|$)", re.M)
    match = line_pattern.search(report_text)
    if match:
        return float(match.group(1))
    return 0.0

time_values = read_kv(query_time_path)
query_metrics = read_csv_by_key(query_summary_path, "metric")
cas_metrics = read_csv_by_two_keys(cas_summary_path, "op", "stmt_label")
server_ts_metrics = read_csv_by_two_keys(server_ts_summary_path, "op", "stmt_label")
server_report = server_report_path.read_text(encoding="utf-8", errors="replace")
cas_report = cas_report_path.read_text(encoding="utf-8", errors="replace")
env_manifest_rows = read_tsv(env_manifest_path)
git_manifest_rows = read_tsv(git_manifest_path)
reproduction_command = command_path.read_text(encoding="utf-8").strip()

queries = int(float(time_values.get("queries", "0") or 0))
wall_sec = float(time_values.get("wall_sec", "0") or 0.0)
qps = float(time_values.get("qps", "0") or 0.0)
client_cpu_sec = float(time_values.get("user_sec", "0") or 0.0) + float(time_values.get("sys_sec", "0") or 0.0)

vector_lookup_avg = float(query_metrics["vector_lookup_sec"]["avg_sec"])
vector_lookup_p95 = float(query_metrics["vector_lookup_sec"]["p95_sec"])
vector_lookup_p99 = float(query_metrics["vector_lookup_sec"]["p99_sec"])
bind_avg = float(query_metrics["bind_sec"]["avg_sec"])
execute_avg = float(query_metrics["execute_sec"]["avg_sec"])
execute_p95 = float(query_metrics["execute_sec"]["p95_sec"])
execute_p99 = float(query_metrics["execute_sec"]["p99_sec"])
fetch_avg = float(query_metrics["fetch_sec"]["avg_sec"])
close_avg = float(query_metrics["close_result_sec"]["avg_sec"])
total_avg = float(query_metrics["total_sec"]["avg_sec"])
client_overhead_avg = total_avg - vector_lookup_avg - execute_avg

cas_ann_request_total = cas_metrics.get(("request_total", "ann_query"), {})
cas_ann_execute_total = cas_metrics.get(("execute_total", "ann_query"), {})
cas_ann_execute_ux = cas_metrics.get(("execute_ux", "ann_query"), {})
cas_vector_execute_total = cas_metrics.get(("execute_total", "vector_lookup"), {})
cas_ann_request_read = cas_metrics.get(("request_read_body", "ann_query"), {})
cas_ann_request_write = cas_metrics.get(("request_write", "ann_query"), {})
cas_ann_server_send = cas_metrics.get(("server_send", "ann_query"), {})
cas_ann_server_receive = cas_metrics.get(("server_receive", "ann_query"), {})
server_ann_execute_total = server_ts_metrics.get(("execute_total", "ann_query"), {})
server_ann_reply_send = server_ts_metrics.get(("reply_send", "ann_query"), {})
server_vector_execute_total = server_ts_metrics.get(("execute_total", "vector_lookup"), {})

cas_ann_request_total_avg = float(cas_ann_request_total.get("avg_sec", "0") or 0.0)
cas_ann_request_total_p95 = float(cas_ann_request_total.get("p95_sec", "0") or 0.0)
cas_ann_request_total_p99 = float(cas_ann_request_total.get("p99_sec", "0") or 0.0)
cas_ann_execute_total_avg = float(cas_ann_execute_total.get("avg_sec", "0") or 0.0)
cas_ann_execute_total_p95 = float(cas_ann_execute_total.get("p95_sec", "0") or 0.0)
cas_ann_execute_total_p99 = float(cas_ann_execute_total.get("p99_sec", "0") or 0.0)
cas_ann_execute_ux_avg = float(cas_ann_execute_ux.get("avg_sec", "0") or 0.0)
cas_ann_execute_ux_p95 = float(cas_ann_execute_ux.get("p95_sec", "0") or 0.0)
cas_ann_execute_ux_p99 = float(cas_ann_execute_ux.get("p99_sec", "0") or 0.0)
cas_vector_execute_total_avg = float(cas_vector_execute_total.get("avg_sec", "0") or 0.0)
cas_ann_request_read_avg = float(cas_ann_request_read.get("avg_sec", "0") or 0.0)
cas_ann_request_write_avg = float(cas_ann_request_write.get("avg_sec", "0") or 0.0)
cas_ann_server_send_avg = float(cas_ann_server_send.get("avg_sec", "0") or 0.0)
cas_ann_server_receive_avg = float(cas_ann_server_receive.get("avg_sec", "0") or 0.0)
cas_ann_request_total_count = int(cas_ann_request_total.get("count", "0") or 0)
cas_ann_server_receive_count = int(cas_ann_server_receive.get("count", "0") or 0)
server_ann_execute_total_avg = float(server_ann_execute_total.get("avg_sec", "0") or 0.0)
server_ann_reply_send_avg = float(server_ann_reply_send.get("avg_sec", "0") or 0.0)
server_vector_execute_total_avg = float(server_vector_execute_total.get("avg_sec", "0") or 0.0)

cci_cas_outer_avg = max(0.0, execute_avg - cas_ann_request_total_avg)
cas_overhead_before_server_avg = max(0.0, cas_ann_execute_total_avg - cas_ann_server_send_avg - cas_ann_server_receive_avg)
cas_request_other_avg = max(0.0, cas_ann_request_total_avg - cas_ann_request_read_avg - cas_ann_execute_total_avg - cas_ann_request_write_avg)
cas_ann_server_receive_per_query_avg = 0.0
if cas_ann_request_total_count > 0:
    cas_ann_server_receive_per_query_avg = cas_ann_server_receive_avg * cas_ann_server_receive_count / cas_ann_request_total_count
server_transport_gap_avg = max(0.0, cas_ann_server_receive_per_query_avg - server_ann_execute_total_avg - server_ann_reply_send_avg)

server_metrics = [
    ("sqmgr_execute_query -> xqmgr_execute_query", find_pct(server_report, "sqmgr_execute_query")),
    ("qexec_execute_query -> qexec_execute_mainblock", find_pct(server_report, "qexec_execute_query")),
    ("hnsw_search_element", find_pct(server_report, "hnsw_search_element")),
    ("seek_on_layer_", find_pct(server_report, "seek_on_layer_")),
    ("cosine_distance", find_pct(server_report, "cubhnsw::cubvec_cosine_distance")),
    ("get_vector_by_slot_id", find_pct(server_report, "get_vector_by_slot_id")),
]
cas_readout = [
    ("process_request", find_pct(cas_report, "process_request")),
    ("fn_execute_internal", find_pct(cas_report, "fn_execute_internal")),
    ("ux_execute", find_pct(cas_report, "ux_execute")),
    ("db_execute_and_keep_statement", find_pct(cas_report, "db_execute_and_keep_statement")),
    ("css_receive_data_from_server_with_timeout", find_pct(cas_report, "css_receive_data_from_server_with_timeout")),
    ("__poll", find_pct(cas_report, "__poll")),
    ("__libc_recv", find_pct(cas_report, "__libc_recv")),
]

def write_csv(path: Path, header, rows):
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(header)
        writer.writerows(rows)

profile_summary_csv = final_dir / "profile_summary.csv"
write_csv(
    profile_summary_csv,
    ("section", "metric", "value"),
    [
        ("overall", "queries", f"{queries}"),
        ("overall", "wall_sec", f"{wall_sec:.6f}"),
        ("overall", "qps", f"{qps:.3f}"),
        ("cci", "vector_lookup_sec.avg_sec", f"{vector_lookup_avg:.9f}"),
        ("cci", "vector_lookup_sec.p95_sec", f"{vector_lookup_p95:.9f}"),
        ("cci", "vector_lookup_sec.p99_sec", f"{vector_lookup_p99:.9f}"),
        ("cci", "bind_sec.avg_sec", f"{bind_avg:.9f}"),
        ("cci", "execute_sec.avg_sec", f"{execute_avg:.9f}"),
        ("cci", "execute_sec.p95_sec", f"{execute_p95:.9f}"),
        ("cci", "execute_sec.p99_sec", f"{execute_p99:.9f}"),
        ("cci", "fetch_sec.avg_sec", f"{fetch_avg:.9f}"),
        ("cci", "close_result_sec.avg_sec", f"{close_avg:.9f}"),
        ("cci", "total_sec.avg_sec", f"{total_avg:.9f}"),
        ("cas", "ann_query.request_total.avg_sec", f"{cas_ann_request_total_avg:.9f}"),
        ("cas", "ann_query.request_total.p95_sec", f"{cas_ann_request_total_p95:.9f}"),
        ("cas", "ann_query.request_total.p99_sec", f"{cas_ann_request_total_p99:.9f}"),
        ("cas", "ann_query.execute_total.avg_sec", f"{cas_ann_execute_total_avg:.9f}"),
        ("cas", "ann_query.execute_total.p95_sec", f"{cas_ann_execute_total_p95:.9f}"),
        ("cas", "ann_query.execute_total.p99_sec", f"{cas_ann_execute_total_p99:.9f}"),
        ("cas", "ann_query.execute_ux.avg_sec", f"{cas_ann_execute_ux_avg:.9f}"),
        ("cas", "ann_query.execute_ux.p95_sec", f"{cas_ann_execute_ux_p95:.9f}"),
        ("cas", "ann_query.execute_ux.p99_sec", f"{cas_ann_execute_ux_p99:.9f}"),
        ("cas", "ann_query.request_read_body.avg_sec", f"{cas_ann_request_read_avg:.9f}"),
        ("cas", "ann_query.request_write.avg_sec", f"{cas_ann_request_write_avg:.9f}"),
        ("cas", "ann_query.server_send.avg_sec", f"{cas_ann_server_send_avg:.9f}"),
        ("cas", "ann_query.server_receive.avg_sec", f"{cas_ann_server_receive_avg:.9f}"),
        ("cas", "ann_query.server_receive_per_query_avg_sec", f"{cas_ann_server_receive_per_query_avg:.9f}"),
        ("cas", "vector_lookup.execute_total.avg_sec", f"{cas_vector_execute_total_avg:.9f}"),
        ("server", "ann_query.execute_total.avg_sec", f"{server_ann_execute_total_avg:.9f}"),
        ("server", "ann_query.reply_send.avg_sec", f"{server_ann_reply_send_avg:.9f}"),
        ("server", "vector_lookup.execute_total.avg_sec", f"{server_vector_execute_total_avg:.9f}"),
        ("derived", "cci_minus_cas_request_total.avg_sec", f"{cci_cas_outer_avg:.9f}"),
        ("derived", "cas_request_other.avg_sec", f"{cas_request_other_avg:.9f}"),
        ("derived", "cas_before_server_overhead.avg_sec", f"{cas_overhead_before_server_avg:.9f}"),
        ("derived", "cas_server_transport_gap.avg_sec", f"{server_transport_gap_avg:.9f}"),
    ],
)

oneline_csv = final_dir / "cci_cas_server_oneline_summary.csv"
write_csv(
    oneline_csv,
    ("group", "metric", "value"),
    [
        ("overall", "queries", f"{queries}"),
        ("overall", "wall_sec", f"{wall_sec:.6f}"),
        ("overall", "qps", f"{qps:.3f}"),
        ("overall", "client_cpu_sec", f"{client_cpu_sec:.6f}"),
        ("cci", "vector_lookup_avg_sec", f"{vector_lookup_avg:.9f}"),
        ("cci", "execute_avg_sec", f"{execute_avg:.9f}"),
        ("cci", "client_overhead_avg_sec", f"{client_overhead_avg:.9f}"),
        ("cci", "total_avg_sec", f"{total_avg:.9f}"),
        ("cas", "ann_request_total_avg_sec", f"{cas_ann_request_total_avg:.9f}"),
        ("cas", "ann_execute_total_avg_sec", f"{cas_ann_execute_total_avg:.9f}"),
        ("cas", "ann_execute_ux_avg_sec", f"{cas_ann_execute_ux_avg:.9f}"),
        ("cas", "ann_request_read_avg_sec", f"{cas_ann_request_read_avg:.9f}"),
        ("cas", "ann_request_write_avg_sec", f"{cas_ann_request_write_avg:.9f}"),
        ("cas", "ann_server_send_avg_sec", f"{cas_ann_server_send_avg:.9f}"),
        ("cas", "ann_server_receive_avg_sec", f"{cas_ann_server_receive_avg:.9f}"),
        ("cas", "ann_server_receive_per_query_avg_sec", f"{cas_ann_server_receive_per_query_avg:.9f}"),
        ("derived", "cci_minus_cas_request_total_avg_sec", f"{cci_cas_outer_avg:.9f}"),
        ("derived", "cas_request_other_avg_sec", f"{cas_request_other_avg:.9f}"),
        ("derived", "cas_before_server_overhead_avg_sec", f"{cas_overhead_before_server_avg:.9f}"),
        ("derived", "cas_server_transport_gap_avg_sec", f"{server_transport_gap_avg:.9f}"),
        ("cas", "vector_lookup_execute_total_avg_sec", f"{cas_vector_execute_total_avg:.9f}"),
        ("cub_server_ts", "ann_execute_total_avg_sec", f"{server_ann_execute_total_avg:.9f}"),
        ("cub_server_ts", "ann_reply_send_avg_sec", f"{server_ann_reply_send_avg:.9f}"),
        ("cub_server_ts", "vector_lookup_execute_total_avg_sec", f"{server_vector_execute_total_avg:.9f}"),
        ("cub_server_perf", "hnsw_search_element", f"{server_metrics[2][1]:.2f}"),
        ("cub_server_perf", "seek_on_layer_", f"{server_metrics[3][1]:.2f}"),
        ("cub_server_perf", "cosine_distance", f"{server_metrics[4][1]:.2f}"),
        ("cub_server_perf", "get_vector_by_slot_id", f"{server_metrics[5][1]:.2f}"),
    ],
)

readout_csv = final_dir / "cas_vs_server_flamegraph_readout.csv"
readout_rows = [("cub_server", metric, f"{value:.2f}") for metric, value in server_metrics]
readout_rows.extend(("cub_cas", metric, f"{value:.2f}") for metric, value in cas_readout)
readout_rows.extend([
    ("cub_cas", "ann_query.request_total.avg_sec", f"{cas_ann_request_total_avg:.9f}"),
    ("cub_cas", "ann_query.execute_total.avg_sec", f"{cas_ann_execute_total_avg:.9f}"),
    ("cub_cas", "ann_query.execute_ux.avg_sec", f"{cas_ann_execute_ux_avg:.9f}"),
    ("cub_cas", "ann_query.request_read_body.avg_sec", f"{cas_ann_request_read_avg:.9f}"),
    ("cub_cas", "ann_query.request_write.avg_sec", f"{cas_ann_request_write_avg:.9f}"),
    ("cub_cas", "ann_query.server_send.avg_sec", f"{cas_ann_server_send_avg:.9f}"),
    ("cub_cas", "ann_query.server_receive.avg_sec", f"{cas_ann_server_receive_avg:.9f}"),
    ("cub_cas", "ann_query.server_receive_per_query_avg_sec", f"{cas_ann_server_receive_per_query_avg:.9f}"),
    ("cub_cas", "vector_lookup.execute_total.avg_sec", f"{cas_vector_execute_total_avg:.9f}"),
    ("cub_server_ts", "ann_query.execute_total.avg_sec", f"{server_ann_execute_total_avg:.9f}"),
    ("cub_server_ts", "ann_query.reply_send.avg_sec", f"{server_ann_reply_send_avg:.9f}"),
    ("derived", "cci_minus_cas_request_total.avg_sec", f"{cci_cas_outer_avg:.9f}"),
    ("derived", "cas_request_other.avg_sec", f"{cas_request_other_avg:.9f}"),
    ("derived", "cas_before_server_overhead.avg_sec", f"{cas_overhead_before_server_avg:.9f}"),
    ("derived", "cas_server_transport_gap.avg_sec", f"{server_transport_gap_avg:.9f}"),
])
write_csv(readout_csv, ("component", "metric", "value"), readout_rows)
perf_stat_summary_csv = final_dir / "cub_server_perf_stat_summary.csv"
perf_stat_rows = read_csv_by_key(perf_stat_summary_csv, "metric") if perf_stat_summary_csv.is_file() else {}

def draw_oneline_svg(path: Path):
    rows = [
        ("CCI total", total_avg, "#156f4a"),
        ("CCI execute", execute_avg, "#2f9e74"),
        ("CCI-CAS outer", cci_cas_outer_avg, "#88c8ad"),
        ("CAS ann request total", cas_ann_request_total_avg, "#b36a00"),
        ("CAS request read", cas_ann_request_read_avg, "#cf8a1a"),
        ("CAS-server send", cas_ann_server_send_avg, "#e6b85c"),
        ("CAS-server receive/q", cas_ann_server_receive_per_query_avg, "#f5d48e"),
        ("Server execute", server_ann_execute_total_avg, "#8b5cf6"),
        ("Server reply send", server_ann_reply_send_avg, "#c4b5fd"),
        ("CAS-CCI write", cas_ann_request_write_avg, "#f2e7c6"),
    ]
    max_v = max(v for _, v, _ in rows) or 1.0
    width, height = 920, 500
    left, top, bottom = 220, 60, 80
    plot_w = width - left - 70
    bar_h = 40
    gap = 24
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#fcfcfb"/>',
        '<text x="40" y="34" font-size="24" font-weight="700" fill="#111">CCI / CAS / cub_server one-line summary</text>',
        '<text x="40" y="54" font-size="13" fill="#555">Average query-stage times and CAS/server transport-visible costs</text>',
    ]
    for idx, (label, value, color) in enumerate(rows):
        y = top + idx * (bar_h + gap)
        w = plot_w * (value / max_v)
        parts.append(f'<text x="{left - 14}" y="{y + 25}" text-anchor="end" font-size="14" fill="#222">{label}</text>')
        parts.append(f'<rect x="{left}" y="{y}" width="{w:.2f}" height="{bar_h}" rx="8" fill="{color}"/>')
        parts.append(f'<text x="{left + w + 10:.2f}" y="{y + 25}" font-size="14" fill="#222">{value:.6f}s</text>')
    perf_y = top + len(rows) * (bar_h + gap) + 24
    parts.append(f'<text x="40" y="{perf_y}" font-size="18" font-weight="700" fill="#111">cub_server perf hotspots</text>')
    for idx, (metric, value) in enumerate(server_metrics[2:]):
        y = perf_y + 26 + idx * 26
        parts.append(f'<text x="60" y="{y}" font-size="14" fill="#222">{metric}</text>')
        parts.append(f'<text x="420" y="{y}" font-size="14" fill="#8a4b00">{value:.2f}%</text>')
    parts.append('</svg>')
    path.write_text("\n".join(parts), encoding="utf-8")

def draw_readout_svg(path: Path):
    width, height = 980, 520
    left = 80
    top = 80
    col_gap = 430
    scale = 2.6
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#fcfcfb"/>',
        '<text x="40" y="34" font-size="24" font-weight="700" fill="#111">cub_cas vs cub_server flamegraph readout</text>',
        '<text x="40" y="54" font-size="13" fill="#555">Server is compute-heavy; CAS is wait/relay-heavy</text>',
        f'<text x="{left}" y="{top}" font-size="18" font-weight="700" fill="#111">cub_server</text>',
        f'<text x="{left + col_gap}" y="{top}" font-size="18" font-weight="700" fill="#111">cub_cas</text>',
    ]
    for idx, (metric, value) in enumerate(server_metrics):
        y = top + 24 + idx * 52
        parts.append(f'<text x="{left}" y="{y}" font-size="13" fill="#222">{metric}</text>')
        parts.append(f'<rect x="{left}" y="{y + 10}" width="{value * scale:.2f}" height="18" rx="6" fill="#1d7f5f"/>')
        parts.append(f'<text x="{left + value * scale + 10:.2f}" y="{y + 24}" font-size="13" fill="#222">{value:.2f}%</text>')
    for idx, (metric, value) in enumerate(cas_readout):
        y = top + 24 + idx * 52
        parts.append(f'<text x="{left + col_gap}" y="{y}" font-size="13" fill="#222">{metric}</text>')
        parts.append(f'<rect x="{left + col_gap}" y="{y + 10}" width="{value * scale:.2f}" height="18" rx="6" fill="#b36a00"/>')
        parts.append(f'<text x="{left + col_gap + value * scale + 10:.2f}" y="{y + 24}" font-size="13" fill="#222">{value:.2f}%</text>')
    parts.append(f'<text x="{left + col_gap}" y="{height - 78}" font-size="14" fill="#222">CAS ann request_total {cas_ann_request_total_avg:.6f}s, execute_total {cas_ann_execute_total_avg:.6f}s, execute_ux {cas_ann_execute_ux_avg:.6f}s</text>')
    parts.append(f'<text x="{left + col_gap}" y="{height - 54}" font-size="14" fill="#222">request_read {cas_ann_request_read_avg:.6f}s, server_send {cas_ann_server_send_avg:.6f}s, server_receive/q {cas_ann_server_receive_per_query_avg:.6f}s, request_write {cas_ann_request_write_avg:.6f}s</text>')
    parts.append(f'<text x="{left}" y="{height - 30}" font-size="14" fill="#222">server execute {server_ann_execute_total_avg:.6f}s, server reply_send {server_ann_reply_send_avg:.6f}s, receive-execute gap {server_transport_gap_avg:.6f}s</text>')
    parts.append('</svg>')
    path.write_text("\n".join(parts), encoding="utf-8")

draw_oneline_svg(final_dir / "cci_cas_server_oneline_summary.svg")
draw_readout_svg(final_dir / "cas_vs_server_flamegraph_readout.svg")

server_bundle_flamegraph = final_dir / "cub_server.full.flamegraph.svg"
cas_bundle_flamegraph = final_dir / "cub_cas.full.flamegraph.svg"
if server_flamegraph_path.is_file():
    shutil.copy2(server_flamegraph_path, server_bundle_flamegraph)
else:
    write_placeholder_flamegraph(server_bundle_flamegraph, "cub_server flamegraph unavailable", f"Expected file: {server_flamegraph_path.name}")
if cas_flamegraph_path.is_file():
    shutil.copy2(cas_flamegraph_path, cas_bundle_flamegraph)
else:
    write_placeholder_flamegraph(cas_bundle_flamegraph, "cub_cas flamegraph unavailable", f"Expected file: {cas_flamegraph_path.name}")
cas_active_bundle_flamegraph = final_dir / "cub_cas.active.flamegraph.svg"
if cas_active_flamegraph_path.is_file():
    shutil.copy2(cas_active_flamegraph_path, cas_active_bundle_flamegraph)
else:
    write_placeholder_flamegraph(cas_active_bundle_flamegraph, "cub_cas active flamegraph unavailable", f"Expected file: {cas_active_flamegraph_path.name}")

shutil.copy2(server_ts_summary_path, final_dir / server_ts_summary_path.name)

readme = f"""# Final Results Bundle

이 폴더는 `test_ann_cci.sh` 실행으로 자동 생성된 최종 결과 묶음이다.

## 재현 정보

- 실행 위치 가정: `cubrid` workspace root
- 실행 명령:

```bash
{reproduction_command}
```

### 저장소 버전

| Component | Path | Branch | Commit | Dirty |
|---|---|---|---|---|
"""

for row in git_manifest_rows:
    readme += f'| `{row["component"]}` | `{row["path"]}` | `{row["branch"]}` | `{row["commit"]}` | `{row["dirty"]}` |\n'

readme += f"""

## 테스트 설명

- 데이터셋: `{dataset_name}`
- query 개수: `{queries:,}`
- ANN 파라미터: `m={hnsw_m}`, `ef_construction={efc}`, `ef_search={ef_search}`, `topk={topk}`
- 실행 경로: `CCI -> cub_cas -> cub_server`
- ANN query 형태: `ORDER BY vec <c> CAST(? AS vector)`
- 우항 벡터 값: `nytimes_256_angular_test`에서 `CAST(vec AS STRING)`으로 읽어와 `run_query_ann` 파라미터로 바인딩
- 인덱스 검증: 실행 전 trace에서 `VECTOR INDEX SCAN` 확인 후 측정
- CAS/cub_server 수집 조건: `hnsw_debug=1`일 때만 CAS timestamp와 CAS/cub_server perf를 수집

## 환경변수

| Name | Default | This run | Description |
|---|---|---|---|
"""

for row in env_manifest_rows:
    default = row["default"] if row["default"] != "" else "(empty)"
    value = row["value"] if row["value"] != "" else "(empty)"
    readme += f'| `{row["name"]}` | `{default}` | `{value}` | {row["description"]} |\n'

readme += f"""

## Profiling Analysis (ANN query, `ef_search={ef_search}`)

### End-to-End Summary

| Metric | Value | Assessment |
|---|---:|---|
| Query count | `{queries:,}` | Full query set |
| Query wall time | `{wall_sec:.2f}s` | End-to-end CCI measurement |
| QPS | `{qps:.3f}` | Final throughput |
| Recall@10 | see result CSV | Quality check |
| CCI total / query | `{total_avg:.6f}s` | End-to-end per query |
| CCI execute / query | `{execute_avg:.6f}s` | Dominant client-visible phase |
| CAS ann request_total / query | `{cas_ann_request_total_avg:.6f}s` | CAS request lifecycle |
| cub_server execute_total / query | `{server_ann_execute_total_avg:.6f}s` | Main server execution cost |

Finding: `CCI execute` and `CAS request_total` are almost identical, and `cub_server execute_total` explains most of that time. The dominant cost is server-side ANN execution, not client/protocol overhead.

### Phase Breakdown

| Layer | Phase | Avg (s) | Interpretation |
|---|---|---:|---|
| CCI | `vector_lookup` | `{vector_lookup_avg:.6f}` | Read query vector string from test table |
| CCI | `execute` | `{execute_avg:.6f}` | ANN execute wait seen by client |
| CCI | `fetch + close` | `{(fetch_avg + close_avg):.6f}` | Very small |
| CAS | `request_read_body` | `{cas_ann_request_read_avg:.6f}` | `CCI -> CAS` request read |
| CAS | `server_send` | `{cas_ann_server_send_avg:.6f}` | `CAS -> cub_server` send |
| CAS | `server_receive` per call | `{cas_ann_server_receive_avg:.6f}` | Low-level receive call average |
| CAS | `server_receive` per query | `{cas_ann_server_receive_per_query_avg:.6f}` | Query-normalized receive wait |
| CAS | `request_write` | `{cas_ann_request_write_avg:.6f}` | `CAS -> CCI` reply write |
| cub_server | `execute_total` | `{server_ann_execute_total_avg:.6f}` | Actual ANN execution |
| cub_server | `reply_send` | `{server_ann_reply_send_avg:.6f}` | Server reply send cost |
| Derived | `CCI execute - CAS request_total` | `{cci_cas_outer_avg:.6f}` | Client/protocol outer overhead |
| Derived | `CAS receive/query - server execute/reply` | `{server_transport_gap_avg:.6f}` | Residual transport/wait gap |

### Hot Path

Top server hotspots from perf/flamegraph:

| Rank | Function | Share |
|---|---|---:|
| 1 | `hnsw_search_element` | `{server_metrics[2][1]:.2f}%` |
| 2 | `seek_on_layer_` | `{server_metrics[3][1]:.2f}%` |
| 3 | `cosine_distance` | `{server_metrics[4][1]:.2f}%` |
| 4 | `get_vector_by_slot_id` | `{server_metrics[5][1]:.2f}%` |

Hot path: `sqmgr_execute_query` → `xqmgr_execute_query` → `hnsw_search_element` → `seek_on_layer_` → `cosine_distance` / `get_vector_by_slot_id`

These functions dominate server CPU time. The flamegraph shows a compute-heavy HNSW search path rather than a broker-side bottleneck.

### CAS vs Server Interpretation

CAS-side readout:

| Metric | Value |
|---|---:|
| `ann_query request_total` | `{cas_ann_request_total_avg:.6f}s` |
| `ann_query execute_total` | `{cas_ann_execute_total_avg:.6f}s` |
| `ann_query execute_ux` | `{cas_ann_execute_ux_avg:.6f}s` |
| `ann_query request_other` | `{cas_request_other_avg:.6f}s` |

Server-side readout:

| Metric | Value |
|---|---:|
| `ann_query execute_total` | `{server_ann_execute_total_avg:.6f}s` |
| `ann_query reply_send` | `{server_ann_reply_send_avg:.6f}s` |

Interpretation:

- `server_receive` looked large at first because CAS measures low-level receive waits, not a pure network transfer.
- After normalizing to per-query and adding server timestamps, most of `CAS server_receive` is explained by `cub_server execute_total`.
- The remaining gap is only `{server_transport_gap_avg:.6f}s`, which is much smaller than server execution time.
- `CCI execute - CAS request_total` is only `{cci_cas_outer_avg:.6f}s`, so client-side overhead outside CAS is also small.

Conclusion: the apparent CAS “receive time” is mostly server execution wait. The main bottleneck remains `cub_server` HNSW execution.

### Optimization Targets

| Priority | Target | Evidence |
|---|---|---|
| P0 | `hnsw_search_element` / `seek_on_layer_` | Largest shares in server flamegraph |
| P0 | `cosine_distance` | Large compute fraction inside search loop |
| P1 | `get_vector_by_slot_id` | Meaningful storage access share in hot path |
| P2 | CAS request wrappers | Small but measurable (`request_other`, transport gap) |

The profiling result does not support “CAS/network is the main bottleneck.” If optimization effort is limited, server-side HNSW execution should be the first target.

### cub_server Perf Stat (build vs query)

| Metric | build index | query |
|---|---:|---:|
| elapsed | `{perf_stat_rows.get("elapsed", {}).get("build_index", "N/A")}` | `{perf_stat_rows.get("elapsed", {}).get("query", "N/A")}` |
| task-clock | `{perf_stat_rows.get("task-clock", {}).get("build_index", "N/A")}` | `{perf_stat_rows.get("task-clock", {}).get("query", "N/A")}` |
| CPUs utilized | `{perf_stat_rows.get("CPUs utilized", {}).get("build_index", "N/A")}` | `{perf_stat_rows.get("CPUs utilized", {}).get("query", "N/A")}` |
| cycles | `{perf_stat_rows.get("cycles", {}).get("build_index", "N/A")}` | `{perf_stat_rows.get("cycles", {}).get("query", "N/A")}` |
| instructions | `{perf_stat_rows.get("instructions", {}).get("build_index", "N/A")}` | `{perf_stat_rows.get("instructions", {}).get("query", "N/A")}` |
| IPC | `{perf_stat_rows.get("IPC", {}).get("build_index", "N/A")}` | `{perf_stat_rows.get("IPC", {}).get("query", "N/A")}` |
| frontend stalled % | `{perf_stat_rows.get("frontend stalled %", {}).get("build_index", "N/A")}` | `{perf_stat_rows.get("frontend stalled %", {}).get("query", "N/A")}` |
| branch-miss rate % | `{perf_stat_rows.get("branch-miss rate %", {}).get("build_index", "N/A")}` | `{perf_stat_rows.get("branch-miss rate %", {}).get("query", "N/A")}` |
| L1-dcache miss rate % | `{perf_stat_rows.get("L1-dcache miss rate %", {}).get("build_index", "N/A")}` | `{perf_stat_rows.get("L1-dcache miss rate %", {}).get("query", "N/A")}` |
| dTLB miss rate % | `{perf_stat_rows.get("dTLB miss rate %", {}).get("build_index", "N/A")}` | `{perf_stat_rows.get("dTLB miss rate %", {}).get("query", "N/A")}` |
| context-switches | `{perf_stat_rows.get("context-switches", {}).get("build_index", "N/A")}` | `{perf_stat_rows.get("context-switches", {}).get("query", "N/A")}` |
| cpu-migrations | `{perf_stat_rows.get("cpu-migrations", {}).get("build_index", "N/A")}` | `{perf_stat_rows.get("cpu-migrations", {}).get("query", "N/A")}` |
| page-faults | `{perf_stat_rows.get("page-faults", {}).get("build_index", "N/A")}` | `{perf_stat_rows.get("page-faults", {}).get("query", "N/A")}` |

This table is `cub_server`-only and is phase-separated into index build and ANN query execution. Unsupported PMU events on the current host are left as `N/A`.

## 포함된 파일

- `cub_server.full.flamegraph.svg`
- `cub_cas.full.flamegraph.svg`
- `cub_cas.active.flamegraph.svg`
- `cub_server_perf_stat_summary.csv`
- `cub_server_perf_stat_summary.svg`
- `cci_cas_server_oneline_summary.svg`
- `cci_cas_server_oneline_summary.csv`
- `cas_vs_server_flamegraph_readout.svg`
- `cas_vs_server_flamegraph_readout.csv`
- `profile_summary.csv`
- `query_ann_ef400_topk10_q9991.cub_server_ts.summary.csv`
"""
(final_dir / "README.md").write_text(readme, encoding="utf-8")
PY

  rm -f "$env_manifest_file" "$git_manifest_file" "$command_file"

  log "saved final results bundle to $FINAL_RESULTS_DIR"
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

  if [[ -n "$LAST_QUERY_LABEL" ]]; then
    build_final_results_bundle "$LAST_QUERY_LABEL"
  fi
}

main() {
  require_cmd cubrid
  require_cmd csql
  require_cmd gcc
  require_cmd taskset
  require_cmd awk
  require_cmd perl
  require_cmd rg
  require_cmd python3
  require_cmd /usr/bin/time

  if (( PERF_ENABLE == 1 )); then
    require_cmd perf
    prepare_perf_output_dir
  fi

  run_stage "prepare base dataset" prepare_base_dataset
  load_query_ids_from_cache
  run_stage "build cci runner" ensure_cci_runner_built
  run_stage "restart broker" ensure_broker_running
  run_stage "restart db for ann experiment" restart_db_for_ann_experiment
  run_stage "build index" build_index
  run_stage "ef_search sweep" run_ef_search_experiments
}

main "$@"
