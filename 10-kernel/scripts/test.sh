#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

ROBUSTNESS=0
CASE_ARGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --robustness) ROBUSTNESS=1; shift ;;
    --case-id) CASE_ARGS+=("--case-id" "$2"); shift 2 ;;
    *) echo "Usage: $0 [--case-id CASE_ID] ... [--robustness]" >&2; exit 2 ;;
  esac
done

python3 tools/correctness/run_correctness.py \
  --manifest tools/data/public_cases.json \
  --variant solution \
  "${CASE_ARGS[@]+"${CASE_ARGS[@]}"}" \
  --output-json artifacts/public_correctness_results.json
if [[ "${ROBUSTNESS}" == "1" ]]; then
  python3 tools/correctness/runtime_robustness.py \
    --variant solution \
    --output-json artifacts/runtime_robustness_results.json
fi
echo "Public correctness tests passed."
