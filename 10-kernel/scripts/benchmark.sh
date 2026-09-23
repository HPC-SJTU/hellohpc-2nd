#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT_DIR}/scripts/env.sh"
exec python "${ROOT_DIR}/tools/benchmark/player_benchmark.py" "$@"
