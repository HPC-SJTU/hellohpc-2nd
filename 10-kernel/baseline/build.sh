#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT_DIR}/scripts/env.sh"

BUILD_DIR="${ROOT_DIR}/build/baseline"
INSTALL_DIR="${ROOT_DIR}/build/baseline-install"
rm -rf "${BUILD_DIR}" "${INSTALL_DIR}"
mkdir -p "${BUILD_DIR}" "${INSTALL_DIR}" "${ROOT_DIR}/artifacts/logs"

cmake -S "${ROOT_DIR}/baseline" -B "${BUILD_DIR}" -G "Unix Makefiles" \
  -DSOC_VERSION=Ascend910B3 -DRUN_MODE=npu -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
  -DASCEND_CANN_PACKAGE_PATH="${ASCEND_HOME_PATH}"
cmake --build "${BUILD_DIR}" -j "${RSM_BUILD_JOBS:-8}"
cmake --install "${BUILD_DIR}"
