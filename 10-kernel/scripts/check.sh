#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ "$#" -eq 0 ]] || { echo "Usage: $0 [-- output-json path]" >&2; exit 2; }
cd "${ROOT_DIR}"
python3 tools/compliance/run_checks.py "$@"
