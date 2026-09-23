# tools/_common.sh — shared environment, build recipe and spec parsing.
#
# Contract: tools are env-driven (CHEATSHEET_*). This file supplies sane defaults so
# local dev can run `bash tools/build.sh` etc. with just CHEATSHEET_TASK set (and a
# kernel.cpp in $PWD). Every tool prints ONLY `key: value` lines (a leading
# `#` comment line is allowed). Hard errors exit non-zero; a WA keeps exit 0
# and prints `correctness: failed` (the Runner distinguishes it from a crash).

# --- strictness (do NOT set -e: tools must control their own exit codes) ---
set -u

# OpenCode needs the provider credential in its own process, but generated C++
# and compiler/measurement subprocesses must not inherit it. Every trusted
# agent-facing tool sources this file before invoking untrusted code.
unset SJTU_API_KEY
# --- resolve CHEATSHEET_SRC (src/ root) from this file's location ------------------
# _common.sh lives in $CHEATSHEET_SRC/tools/ ; default CHEATSHEET_SRC = its parent's parent.
__cheatsheet_common_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
: "${CHEATSHEET_SRC:=$(cd "${__cheatsheet_common_dir}/.." && pwd)}"

# --- required: task name ----------------------------------------------------
if [ -z "${CHEATSHEET_TASK:-}" ]; then
  echo "# error: CHEATSHEET_TASK is not set (fft|bitmatrix)" >&2
  echo "error: CHEATSHEET_TASK unset" >&2
  exit 3
fi

# --- workspace (holds kernel.cpp the agent edits) ---------------------------
: "${CHEATSHEET_WORK:=$PWD}"

# --- harness source dir (harness_main.cpp + kernel_reference.cpp + kernel.h) -
# In the sandbox the Runner points this at a read-only copy; locally it is the
# task dir under src/tasks/.
: "${CHEATSHEET_HARNESS_SRC:=$CHEATSHEET_SRC/tasks/$CHEATSHEET_TASK}"

# --- build output dir -------------------------------------------------------
: "${CHEATSHEET_BUILD:=$CHEATSHEET_WORK/build}"

# --- compile flags (mirror config.yaml compile.*) ---------------------------
CHEATSHEET_PYTHON="${CHEATSHEET_PYTHON:-python3}"
CHEATSHEET_CXX="${CHEATSHEET_CXX:-/usr/bin/g++}"
: "${CHEATSHEET_CXXFLAGS:=-O3 -march=native -fopenmp -std=c++17 -funroll-loops}"
# Runtime-free UB trap build (config.yaml san_flags).  The submitted shared
# object must have no sanitizer constructor/init-array and no runtime DSO.
CHEATSHEET_SAN_FLAGS="${CHEATSHEET_SAN_FLAGS:--fsanitize=undefined -fsanitize-undefined-trap-on-error -g -O1 -fopenmp -std=c++17}"
# vectorization report flags (config.yaml vec_flags) — used by vec_report.sh
CHEATSHEET_VEC_FLAGS="${CHEATSHEET_VEC_FLAGS:--fopt-info-vec-optimized -fopt-info-vec-missed}"
CHEATSHEET_COMPILE_OPTIONS_FILE="$CHEATSHEET_WORK/compile_options.txt"
CHEATSHEET_KERNEL_EXTRA_FLAGS=()
__cheatsheet_option_lines="$(
  "$CHEATSHEET_PYTHON" "$CHEATSHEET_SRC/runner/compile_options.py" "$CHEATSHEET_COMPILE_OPTIONS_FILE"
)" || {
  echo "# error: invalid compile_options.txt" >&2
  exit 8
}
if [ -n "$__cheatsheet_option_lines" ]; then
  mapfile -t CHEATSHEET_KERNEL_EXTRA_FLAGS <<< "$__cheatsheet_option_lines"
fi
unset __cheatsheet_option_lines

# --- tool-local hard wall-clock limits (seconds) ---------------------------
# A bench includes three warmups plus eleven timed repetitions.  Keep the
# default large enough for the public starter, not just for an optimized kernel.
: "${CHEATSHEET_BENCH_SECONDS:=600}"
: "${CHEATSHEET_VARIANCE_THRESHOLD_PCT:=10.0}"
: "${CHEATSHEET_PROFILE_SECONDS:=120}"
: "${CHEATSHEET_COMPILE_SECONDS:=300}"
: "${CHEATSHEET_CHECK_SECONDS:=300}"
__cheatsheet_compiler_probe_done=0

# --- pinning / OpenMP (mirror config.yaml pinning.*) ------------------------
: "${CHEATSHEET_CORES:=0-31}"
: "${CHEATSHEET_OMP_THREADS:=32}"
: "${CHEATSHEET_OMP_BIND:=close}"
: "${CHEATSHEET_OMP_PLACES:=cores}"
# Ambient OpenMP policy must not override the formal fixed team size.
OMP_DYNAMIC=FALSE
export OMP_DYNAMIC

# --- optional single-size override ------------------------------------------
# CHEATSHEET_SIZE empty => iterate spec.yaml public sizes.
: "${CHEATSHEET_SIZE:=}"

# --- optional organizer-only NUMA tool lane -------------------------------
# Parallel official Agent workers share one advisory lock per NUMA node.
# Public debug runs leave CHEATSHEET_TOOL_LOCK unset and run exactly as before.
cheatsheet_lock_tool() {
  local mode="$1" lock="${CHEATSHEET_TOOL_LOCK:-}" attempt diagnostic
  [ -z "$lock" ] && return 0
  case "$mode" in shared|exclusive) ;; *) return 70 ;; esac
  case "$lock" in /*) ;; *) echo "# error: invalid tool lock path" >&2; return 70 ;; esac
  if [ ! -f "$lock" ] || [ -L "$lock" ] || ! command -v flock >/dev/null 2>&1; then
    echo "# error: trusted tool lock is unavailable" >&2
    return 70
  fi
  exec 9>>"$lock" || return 70
  # Network lock managers can transiently return ENOLCK. Retry acquisition,
  # never run unlocked. This occurs before compilation or any timed work.
  for attempt in {1..10}; do
    if diagnostic="$(LC_ALL=C flock --"$mode" 9 2>&1)"; then
      return 0
    fi
    if [[ "$diagnostic" != *"No locks available"* ]]; then
      printf '%s\n' "$diagnostic" >&2
      return 70
    fi
    echo "# transient lock-manager failure; acquisition retry $attempt/10" >&2
    sleep 0.2
  done
  printf '# error: trusted tool lock acquisition failed: %s\n' "$diagnostic" >&2
  return 70
}

# --- derived paths ----------------------------------------------------------
CHEATSHEET_KERNEL="$CHEATSHEET_WORK/kernel.cpp"
CHEATSHEET_SPEC="$CHEATSHEET_HARNESS_SRC/spec.yaml"

# ---------------------------------------------------------------------------
# Build freshness. Agent edits happen between tool calls, so the mere presence
# of harness/kernel.so is not evidence that they represent the current
# kernel.cpp.  Fingerprint the generated input, trusted build sources and exact
# compile settings.  A failed/partial rebuild never leaves a valid manifest.
# ---------------------------------------------------------------------------
cheatsheet_build_fingerprint() {
  local extra="$1" file
  {
    printf '%s\n' \
      "cxx=$CHEATSHEET_CXX" \
      "cxxflags=$CHEATSHEET_CXXFLAGS" \
      "kernel_extra_flags=${CHEATSHEET_KERNEL_EXTRA_FLAGS[*]}" \
      "extra=$extra"
    for file in \
      "$CHEATSHEET_KERNEL" \
      "$CHEATSHEET_HARNESS_SRC/harness_main.cpp" \
      "$CHEATSHEET_HARNESS_SRC/kernel_reference.cpp" \
      "$CHEATSHEET_HARNESS_SRC/kernel.h" \
      "$CHEATSHEET_HARNESS_SRC/common/isolated_runner.h" \
      "$CHEATSHEET_SRC/profiler/hwcounters.h" \
      "$CHEATSHEET_SRC/profiler/hwcounters.cpp" \
      "$CHEATSHEET_SRC/worker/kernel_worker_main.cpp" \
      "$CHEATSHEET_SRC/runner/compile_options.py" \
      "$__cheatsheet_common_dir/_common.sh"; do
      [ -f "$file" ] || return 1
      sha256sum "$file" || return 1
    done
    if [ -f "$CHEATSHEET_COMPILE_OPTIONS_FILE" ]; then
      sha256sum "$CHEATSHEET_COMPILE_OPTIONS_FILE" || return 1
    else
      printf '%s\n' "compile_options=empty"
    fi
  } | sha256sum | awk '{print $1}'
}

# Return success only when check.sh validated the exact current source, trusted
# inputs and compiler options.  Measurement tools use this before doing any
# expensive timing so an unchecked edit cannot look like a successful round.
cheatsheet_candidate_is_checked() {
  local fingerprint checked=""
  fingerprint="$(cheatsheet_build_fingerprint "")" || return 1
  [ -f "$CHEATSHEET_BUILD/check.manifest" ] && IFS= read -r checked < "$CHEATSHEET_BUILD/check.manifest"
  [ "$checked" = "$fingerprint" ]
}

cheatsheet_build_is_current() {
  local extra="$1" out="$2" expected actual manifest="${out}.manifest"
  [ -x "$out" ] && [ -x "${out}.worker" ] && \
    [ -f "${out}.kernel.so" ] && [ -f "$manifest" ] || return 1
  expected="$(cheatsheet_build_fingerprint "$extra")" || return 1
  IFS= read -r actual < "$manifest" || return 1
  [ "$actual" = "$expected" ]
}

cheatsheet_ensure_current_build() {
  local extra="$1" out="$2" log="$3"
  if cheatsheet_build_is_current "$extra" "$out"; then
    return 0
  fi
  cheatsheet_compile_harness "$extra" "$out" "$log"
}

# ---------------------------------------------------------------------------
# cheatsheet_require_kernel — hard-fail (exit 4) if the agent's kernel.cpp is missing.
# ---------------------------------------------------------------------------
cheatsheet_require_kernel() {
  if [ ! -f "$CHEATSHEET_KERNEL" ] || [ -L "$CHEATSHEET_KERNEL" ]; then
    echo "# error: kernel.cpp not found at $CHEATSHEET_KERNEL" >&2
    echo "error: kernel.cpp must be a regular non-symlink file" >&2
    exit 4
  fi
}

# ---------------------------------------------------------------------------
# cheatsheet_public_sizes — echo the size list to iterate, space-separated.
#   If CHEATSHEET_SIZE is set, echo just that. Otherwise parse sizes.public from YAML.
#   Both inline and block-list YAML are emitted by trusted bundle builders.
# ---------------------------------------------------------------------------
cheatsheet_public_sizes() {
  if [ -n "$CHEATSHEET_SIZE" ]; then
    echo "$CHEATSHEET_SIZE"
    return 0
  fi
  if [ ! -f "$CHEATSHEET_SPEC" ]; then
    echo "# error: spec.yaml not found at $CHEATSHEET_SPEC" >&2
    return 1
  fi
  "$CHEATSHEET_PYTHON" - "$CHEATSHEET_SPEC" <<'PY'
import re
import sys

try:
    import yaml

    spec = yaml.safe_load(open(sys.argv[1], encoding="utf-8"))
    sizes = spec.get("sizes") if isinstance(spec, dict) else None
    values = sizes.get("public") if isinstance(sizes, dict) else None
    if not isinstance(values, list) or not values:
        raise ValueError
    token_re = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.:+x-]{0,127}$")
    if any(not isinstance(value, str) or not token_re.fullmatch(value) for value in values):
        raise ValueError
except (OSError, UnicodeError, ValueError, yaml.YAMLError):
    print("# error: invalid public sizes in spec.yaml", file=sys.stderr)
    raise SystemExit(1)
sys.stdout.write(" ".join(values) + "\n")
PY
}

# ---------------------------------------------------------------------------
# cheatsheet_compile_harness <extra_flags> <out_binary> — compile the under-test
#   trusted parent: harness_main.cpp + kernel_reference.cpp (from
#   $CHEATSHEET_HARNESS_SRC) + parent-side $CHEATSHEET_SRC/profiler/hwcounters.cpp
#   Emits compiler stderr to the given log path via global CHEATSHEET_BUILD_LOG.
#   Returns g++ exit code.
# ---------------------------------------------------------------------------
cheatsheet_compile_harness() {
  local extra="$1" out="$2" log="$3"
  mkdir -p "$(dirname "$out")"
  local manifest="${out}.manifest" manifest_tmp="${out}.manifest.$$" digest rc
  local kernel_stage staged_kernel
  rm -f "$manifest" "$manifest_tmp" "${out}.kernel.so"
  # shellcheck disable=SC2086
  cheatsheet_isolated_compile trusted "$CHEATSHEET_COMPILE_SECONDS" "$CHEATSHEET_BUILD" \
    "$CHEATSHEET_CXX" -I"$CHEATSHEET_HARNESS_SRC" -I"$CHEATSHEET_SRC" $CHEATSHEET_CXXFLAGS $extra \
    "$CHEATSHEET_HARNESS_SRC/harness_main.cpp" \
    "$CHEATSHEET_HARNESS_SRC/kernel_reference.cpp" \
    "$CHEATSHEET_SRC/profiler/hwcounters.cpp" \
    -ldl -o "$out" 2> "$log" || return $?
  # The worker is a separate minimal executable and links no task oracle.
  # shellcheck disable=SC2086
  cheatsheet_isolated_compile trusted "$CHEATSHEET_COMPILE_SECONDS" "$CHEATSHEET_BUILD" \
    "$CHEATSHEET_CXX" -I"$CHEATSHEET_HARNESS_SRC" -I"$CHEATSHEET_SRC" $CHEATSHEET_CXXFLAGS $extra \
    "$CHEATSHEET_SRC/worker/kernel_worker_main.cpp" -ldl \
    -o "${out}.worker" 2>> "$log" || return $?

  # Give the submitted compiler a new, otherwise empty output mount.  The
  # surrounding build directory already contains the trusted harness and must
  # not be readable through #include or assembler .incbin.
  kernel_stage="$(mktemp -d "$CHEATSHEET_BUILD/.cheatsheet-submitted-kernel.XXXXXX")" || return 70
  staged_kernel="$kernel_stage/kernel.so"
  # shellcheck disable=SC2086
  cheatsheet_isolated_compile submitted "$CHEATSHEET_COMPILE_SECONDS" "$kernel_stage" \
    "$CHEATSHEET_CXX" -I"$CHEATSHEET_HARNESS_SRC" $CHEATSHEET_CXXFLAGS $extra \
    "${CHEATSHEET_KERNEL_EXTRA_FLAGS[@]}" \
    -fPIC -shared -nostartfiles "$CHEATSHEET_KERNEL" -o "$staged_kernel" 2>> "$log" || {
      rc=$?
      rm -f -- "$staged_kernel"
      rmdir -- "$kernel_stage" 2>/dev/null || true
      return "$rc"
    }
  if [ ! -f "$staged_kernel" ]; then
    rmdir -- "$kernel_stage" 2>/dev/null || true
    return 70
  fi
  mv -f -- "$staged_kernel" "${out}.kernel.so" || return 70
  rmdir -- "$kernel_stage" || return 70

  digest="$(cheatsheet_build_fingerprint "$extra")" || return 70
  printf '%s\n' "$digest" > "$manifest_tmp" || return 70
  mv -f "$manifest_tmp" "$manifest" || return 70
}

# ---------------------------------------------------------------------------
# cheatsheet_run_pinned <size> <mode> [extra harness args...] — run harness under
#   taskset + OMP env (bench). Echoes harness stdout.
# ---------------------------------------------------------------------------
cheatsheet_run_pinned() {
  local size="$1" mode="$2"; shift 2
  if [ ! -f "$CHEATSHEET_BUILD/harness.kernel.so" ] || \
     [ ! -x "$CHEATSHEET_BUILD/harness.worker" ]; then
    echo "# error: isolated kernel artifact is missing" >&2
    return 70
  fi
  local run_seconds="$CHEATSHEET_CHECK_SECONDS"
  [ "$mode" = "bench" ] && run_seconds="$CHEATSHEET_BENCH_SECONDS"
  [ "$mode" = "profile" ] && run_seconds="$CHEATSHEET_PROFILE_SECONDS"
  CHEATSHEET_KERNEL_SO="$CHEATSHEET_BUILD/harness.kernel.so" \
  CHEATSHEET_KERNEL_WORKER="$CHEATSHEET_BUILD/harness.worker" \
  OMP_NUM_THREADS="$CHEATSHEET_OMP_THREADS" \
  OMP_PROC_BIND="$CHEATSHEET_OMP_BIND" \
  OMP_PLACES="$CHEATSHEET_OMP_PLACES" \
  OMP_DYNAMIC=FALSE \
    cheatsheet_isolated_run "$run_seconds" \
      taskset -c "$CHEATSHEET_CORES" "$CHEATSHEET_BUILD/harness" "$mode" "$size" "$@"
}

# All formal generated-code compilation and execution must cross the same
# fail-closed Python/bwrap boundary.  Trusted harness builds can read trusted
# source.  A submitted build sees only kernel.cpp, the task ABI header, compiler
# runtime, and a newly-created empty output directory.
cheatsheet_isolated_compile() {
  local scope="$1" seconds="$2" writable="$3"; shift 3
  local readonly_args=()
  # This probe resolves the absolute compiler and compiles/runs OpenMP
  # within the same namespace/mount policy. It fails
  # closed before any submitted source is compiled.
  if [ "$__cheatsheet_compiler_probe_done" -eq 0 ]; then
    "$CHEATSHEET_PYTHON" "$CHEATSHEET_SRC/runner/isolation.py" probe \
      --compiler "$CHEATSHEET_CXX" || return $?
    __cheatsheet_compiler_probe_done=1
  fi

  case "$scope" in
    trusted)
      readonly_args=(--ro "$CHEATSHEET_SRC" --ro "$CHEATSHEET_HARNESS_SRC")
      ;;
    submitted)
      if [ ! -f "$CHEATSHEET_HARNESS_SRC/kernel.h" ] || \
         [ -L "$CHEATSHEET_HARNESS_SRC/kernel.h" ] || [ -L "$CHEATSHEET_KERNEL" ]; then
        echo "isolation: submitted compile inputs must be regular files" >&2
        return 125
      fi
      readonly_args=(--ro "$CHEATSHEET_KERNEL" --ro "$CHEATSHEET_HARNESS_SRC/kernel.h")
      ;;
    *)
      echo "isolation: unknown compile scope" >&2
      return 125
      ;;
  esac
  "$CHEATSHEET_PYTHON" "$CHEATSHEET_SRC/runner/isolation.py" exec \
    --timeout "$seconds" \
    --api-key-env SJTU_API_KEY \
    "${readonly_args[@]}" --rw "$writable" --cwd "$writable" -- "$@"
}

# Runtime has no writable host bind.  Only the three exact artifacts required
# by the trusted parent/worker boundary are exposed, all read-only.
cheatsheet_isolated_run() {
  local seconds="$1"; shift
  "$CHEATSHEET_PYTHON" "$CHEATSHEET_SRC/runner/isolation.py" exec \
    --timeout "$seconds" \
    --api-key-env SJTU_API_KEY \
    --ro "$CHEATSHEET_BUILD/harness" \
    --ro "$CHEATSHEET_BUILD/harness.worker" \
    --ro "$CHEATSHEET_BUILD/harness.kernel.so" -- "$@"
}
