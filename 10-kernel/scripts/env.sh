#!/usr/bin/env bash

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  echo "Source this script: source scripts/env.sh" >&2
  exit 2
fi

RSM_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RSM_CANN_VERSION="${CANN_VERSION:-8.5.0}"
RSM_ASCEND_ROOT="${ASCEND_INSTALL_ROOT:-${RSM_ROOT}/.local/Ascend}"
RSM_DEVICE_ID="${ASCEND_DEVICE_ID:-0}"
RSM_TOOLS_PREFIX="${RSM_TOOLCHAIN_ROOT:-${RSM_ROOT}/.venv}"
export ASCEND_INSTALL_ROOT="${RSM_ASCEND_ROOT}"
export RSM_TOOLCHAIN_ROOT="${RSM_TOOLS_PREFIX}"

_rsm_strip_other_ascend_paths() {
  local value="${1:-}"
  local entry
  local kept=""
  local old_ifs="${IFS}"
  IFS=':'
  for entry in ${value}; do
    [[ -z "${entry}" ]] && continue
    case "${entry}" in
      /usr/local/Ascend/*|"${HOME}/Ascend"/*) continue ;;
    esac
    kept="${kept:+${kept}:}${entry}"
  done
  IFS="${old_ifs}"
  printf '%s' "${kept}"
}

export PATH="$(_rsm_strip_other_ascend_paths "${PATH:-}")"
export LD_LIBRARY_PATH="$(_rsm_strip_other_ascend_paths "${LD_LIBRARY_PATH:-}")"
export PYTHONPATH="$(_rsm_strip_other_ascend_paths "${PYTHONPATH:-}")"
export CMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH:-}"

if [[ -d "${RSM_TOOLS_PREFIX}/bin" ]]; then
  export PATH="${RSM_TOOLS_PREFIX}/bin:${PATH}"
  export CMAKE_PREFIX_PATH="${RSM_TOOLS_PREFIX}${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
  if [[ -x "${RSM_TOOLS_PREFIX}/bin/aarch64-conda-linux-gnu-cc" ]]; then
    export CC="${RSM_TOOLS_PREFIX}/bin/aarch64-conda-linux-gnu-cc"
  fi
  if [[ -x "${RSM_TOOLS_PREFIX}/bin/aarch64-conda-linux-gnu-c++" ]]; then
    export CXX="${RSM_TOOLS_PREFIX}/bin/aarch64-conda-linux-gnu-c++"
  fi

  # BiSheng does not discover C++ headers from the project-local GCC toolchain.
  # Export the same search roots reported by the GCC driver so Ascend C builds
  # remain independent of system development packages.
  _rsm_gcc_version="$(find "${RSM_TOOLS_PREFIX}/lib/gcc/aarch64-conda-linux-gnu" \
    -mindepth 1 -maxdepth 1 -type d -printf '%f\n' 2>/dev/null | sort -V | tail -n 1)"
  if [[ -n "${_rsm_gcc_version}" ]]; then
    _rsm_gcc_root="${RSM_TOOLS_PREFIX}/lib/gcc/aarch64-conda-linux-gnu/${_rsm_gcc_version}"
    _rsm_cxx_root="${_rsm_gcc_root}/include/c++"
    _rsm_toolchain_includes="${_rsm_cxx_root}:${_rsm_cxx_root}/aarch64-conda-linux-gnu:${_rsm_cxx_root}/backward:${RSM_TOOLS_PREFIX}/aarch64-conda-linux-gnu/sysroot/usr/include"
    export CPLUS_INCLUDE_PATH="${_rsm_toolchain_includes}${CPLUS_INCLUDE_PATH:+:${CPLUS_INCLUDE_PATH}}"
  fi
fi

RSM_SET_ENV=""
for _rsm_candidate in \
  "${RSM_ASCEND_ROOT}/cann-${RSM_CANN_VERSION}/set_env.sh" \
  "${RSM_ASCEND_ROOT}/ascend-toolkit/${RSM_CANN_VERSION}/set_env.sh" \
  "${RSM_ASCEND_ROOT}/ascend-toolkit/latest/set_env.sh"; do
  if [[ -f "${_rsm_candidate}" ]]; then
    RSM_SET_ENV="$(readlink -f "${_rsm_candidate}")"
    break
  fi
done
if [[ -z "${RSM_SET_ENV}" ]]; then
  RSM_SET_ENV="$(find "${RSM_ASCEND_ROOT}" -type f -name set_env.sh -print 2>/dev/null | sort -V | tail -n 1)"
fi
unset _rsm_candidate
if [[ -z "${RSM_SET_ENV}" ]]; then
  echo "CANN ${RSM_CANN_VERSION} is not installed below ${RSM_ASCEND_ROOT}." >&2
  echo "Check ASCEND_INSTALL_ROOT or use the provided contest container." >&2
  return 20
fi

# shellcheck disable=SC1090
source "${RSM_SET_ENV}"

if ! command -v npu-smi >/dev/null 2>&1 || ! npu-smi info >/dev/null 2>&1; then
  echo "npu-smi is unavailable or the device is inaccessible." >&2
  return 21
fi

export ASCEND_DEVICE_ID="${RSM_DEVICE_ID}"
export ASCEND_RT_VISIBLE_DEVICES="${RSM_DEVICE_ID}"
export RANK_ID="${RSM_DEVICE_ID}"

if [[ "${RSM_ENV_QUIET:-0}" != "1" ]]; then
  echo "Ragged Softmax Moments environment"
  echo "  root: ${RSM_ROOT}"
  echo "  CANN: ${RSM_CANN_VERSION} (${RSM_SET_ENV})"
  echo "  device: ${RSM_DEVICE_ID}"
  echo "  compiler: $(command -v ccec_compiler || command -v bisheng || echo unavailable)"
fi

unset -f _rsm_strip_other_ascend_paths
unset _rsm_gcc_version _rsm_gcc_root _rsm_cxx_root _rsm_toolchain_includes
