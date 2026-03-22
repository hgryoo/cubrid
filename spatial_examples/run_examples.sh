#!/usr/bin/env bash

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${SCRIPT_DIR}/results"

usage() {
  cat <<'EOF'
Usage:
  run_examples.sh <database> [csql options...]

Examples:
  ./run_examples.sh demodb
  ./run_examples.sh demodb -u dba -p password --no-auto-commit

Notes:
  - Each .sql file in spatial_examples is executed independently.
  - Per-example stdout/stderr logs are written under spatial_examples/results/.
  - A summary report is written to spatial_examples/results/summary.txt.
EOF
}

if [ $# -lt 1 ]; then
  usage
  exit 1
fi

if ! command -v csql >/dev/null 2>&1; then
  echo "csql not found in PATH" >&2
  exit 1
fi

DB_NAME="$1"
shift

mkdir -p "${OUT_DIR}"
SUMMARY_FILE="${OUT_DIR}/summary.txt"

{
  echo "Spatial example run"
  echo "database: ${DB_NAME}"
  echo "timestamp: $(date '+%Y-%m-%d %H:%M:%S %z')"
  echo
} > "${SUMMARY_FILE}"

overall_status=0

for sql_file in "${SCRIPT_DIR}"/*.sql; do
  base_name="$(basename "${sql_file}" .sql)"
  stdout_file="${OUT_DIR}/${base_name}.out"
  stderr_file="${OUT_DIR}/${base_name}.err"

  echo "[RUN] ${base_name}.sql"

  if csql -i "${sql_file}" "${DB_NAME}" "$@" >"${stdout_file}" 2>"${stderr_file}"; then
    echo "[OK ] ${base_name}.sql"
    {
      echo "[OK ] ${base_name}.sql"
      echo "  stdout: ${stdout_file}"
      if [ -s "${stderr_file}" ]; then
        echo "  stderr: ${stderr_file}"
      fi
      echo
    } >> "${SUMMARY_FILE}"
  else
    echo "[FAIL] ${base_name}.sql"
    {
      echo "[FAIL] ${base_name}.sql"
      echo "  stdout: ${stdout_file}"
      echo "  stderr: ${stderr_file}"
      echo
    } >> "${SUMMARY_FILE}"
    overall_status=1
  fi
done

echo "summary: ${SUMMARY_FILE}"
exit "${overall_status}"
