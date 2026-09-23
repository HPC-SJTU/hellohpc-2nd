#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT_DIR}/scripts/env.sh"
INSTALL_DIR="${ROOT_DIR}/build/baseline-install"
export LD_LIBRARY_PATH="${INSTALL_DIR}/lib64:${ASCEND_HOME_PATH}/lib64:${LD_LIBRARY_PATH:-}"
exec "${INSTALL_DIR}/bin/rsm_baseline_runner" "$@"
