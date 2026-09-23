#!/usr/bin/env bash
# tools/check.sh — correctness over public sizes plus a sanitizer build.
#
# 1. Ensure the normal harness is built (build if missing).
# 2. Run `harness check <size>` over spec public sizes (or CHEATSHEET_SIZE).
# 3. Separately build with runtime-free GCC undefined-behaviour trap
#    instrumentation (config san_flags), then run one small size. A trap or
#    nonzero exit fails the diagnostic.
# Aggregate: correctness / max_abs_err / max_rel_err / failed_sizes.
# A wrong answer prints `correctness: failed` but exits 0 (Runner reads the field).
# Hard errors (missing kernel, build/link failure) exit non-zero.
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_common.sh"

cheatsheet_lock_tool exclusive || exit 70
cheatsheet_require_kernel
rm -f "$CHEATSHEET_BUILD/check.manifest"

# --- ensure the normal build matches the current kernel and trusted sources --
log="$CHEATSHEET_BUILD/build.log"
mkdir -p "$CHEATSHEET_BUILD"
if ! cheatsheet_ensure_current_build "" "$CHEATSHEET_BUILD/harness" "$log"; then
    echo "# error: build failed (see $log)" >&2
    echo "correctness: failed"
    echo "max_abs_err: n/a"
    echo "max_rel_err: n/a"
    echo "failed_sizes: build_error"
    exit 5
fi

sizes="$(cheatsheet_public_sizes)" || { echo "# error: cannot resolve sizes" >&2; exit 6; }

overall_pass=1
max_abs="0"
max_rel="0"
failed_list=""
observed_errors=0

# helper: numeric max of two floats (handles sci-notation), pure awk
fmax() { awk -v a="$1" -v b="$2" 'BEGIN{ printf (a+0>b+0)?a:b }'; }

for sz in $sizes; do
  out="$(cheatsheet_run_pinned "$sz" check 2>&1)"
  rc=$?
  printf '%s\n' "$out" > "$CHEATSHEET_BUILD/check-${sz}.log"
  if [ "$rc" -ne 0 ]; then
    echo "# check size=$sz status=execution_failed rc=$rc (124=timeout; other codes need the diagnostic below)"
    printf '%s\n' "$out" | head -c 3000 | sed 's/^/# diagnostic: /'
    echo
    # Runtime failure remains a hard fail for this size.
    overall_pass=0
    failed_list="${failed_list:+$failed_list,}$sz"
    continue
  fi
  c="$(echo "$out"  | awk -F': ' '/^correctness:/{print $2}')"
  ma="$(echo "$out" | awk -F': ' '/^max_abs_err:/{print $2}')"
  mr="$(echo "$out" | awk -F': ' '/^max_rel_err:/{print $2}')"
  [ -n "$ma" ] && observed_errors=1
  [ -n "$ma" ] && max_abs="$(fmax "$max_abs" "$ma")"
  [ -n "$mr" ] && max_rel="$(fmax "$max_rel" "$mr")"
  echo "# check size=$sz correctness=${c:-missing} max_abs_err=${ma:-n/a} max_rel_err=${mr:-n/a}"
  printf '%s\n' "$out" | awk '/^(mismatch_|input_integrity:)/ {print "# " $0}'
  if [ "$c" != "passed" ]; then
    overall_pass=0
    failed_list="${failed_list:+$failed_list,}$sz"
  fi
done

# --- UB-trap diagnostic build + run on the smallest public size --------------
san_size="$(echo "$sizes" | tr ' ' '\n' | grep -v '^$' | awk -F'x' '{
  p=1; for(i=1;i<=NF;i++) p*=$i; print p, $0
}' | sort -n | head -1 | awk '{print $2}')"
[ -z "$san_size" ] && san_size="$(echo "$sizes" | awk '{print $1}')"

san_bin="$CHEATSHEET_BUILD/harness_san"
san_log="$CHEATSHEET_BUILD/build_san.log"
san_run="$CHEATSHEET_BUILD/san_run.log"
san_retry="$CHEATSHEET_BUILD/san_retry.log"
rm -f "$san_retry"

if cheatsheet_compile_harness "$CHEATSHEET_SAN_FLAGS" "$san_bin" "$san_log"; then
  # Run single-thread to keep the diagnostic deterministic.
  run_sanitizer_once() {
    CHEATSHEET_KERNEL_SO="${san_bin}.kernel.so" \
    CHEATSHEET_KERNEL_WORKER="${san_bin}.worker" \
    OMP_NUM_THREADS=1 \
      "$CHEATSHEET_PYTHON" "$CHEATSHEET_SRC/runner/isolation.py" exec \
        --timeout "$CHEATSHEET_CHECK_SECONDS" \
        --api-key-env SJTU_API_KEY \
        --ro "$san_bin" --ro "${san_bin}.worker" \
        --ro "${san_bin}.kernel.so" -- \
        "$san_bin" check "$san_size" > "$san_run" 2>&1
  }

  run_sanitizer_once
  san_rc=$?
  # A freshly materialized executable on the shared filesystem can
  # occasionally fail before emitting any diagnostic. Re-run the exact same
  # binary and input once so an infrastructure transient cannot invalidate a
  # long campaign. Deterministic UB still traps twice and remains fail-closed.
  if [ "$san_rc" -ne 0 ] && [ ! -s "$san_run" ]; then
    printf 'first_returncode: %s\n' "$san_rc" > "$san_retry"
    run_sanitizer_once
    san_rc=$?
    printf 'retry_returncode: %s\n' "$san_rc" >> "$san_retry"
    echo "# sanitizer empty-diagnostic retry performed (see $san_retry)" >&2
  fi
  san_c="$(awk -F': ' '/^correctness:/{print $2}' "$san_run")"
  # Trap-mode UBSan terminates on detected UB; retain text checks as defence in
  # depth if toolchain flags are overridden by an operator.
  if [ "$san_rc" -ne 0 ] \
     || grep -qE 'runtime error:|AddressSanitizer|LeakSanitizer|SUMMARY: ' "$san_run" \
     || [ "$san_c" = "failed" ]; then
    overall_pass=0
    case ",$failed_list," in
      *",san($san_size),"*) : ;;
      *) failed_list="${failed_list:+$failed_list,}san($san_size)";;
    esac
    if [ "$san_rc" -ne 0 ]; then
      echo "# sanitizer-build execution failed: rc=$san_rc; a nonzero return alone does not establish undefined behavior (see $san_run)" >&2
    elif grep -qE 'runtime error:|AddressSanitizer|LeakSanitizer|SUMMARY: ' "$san_run"; then
      echo "# sanitizer diagnostic detected (see $san_run)" >&2
    else
      echo "# sanitizer-build numerical correctness failed; this is a wrong answer, not evidence of a sanitizer trap (see $san_run)" >&2
    fi
    head -c 3000 "$san_run" | sed 's/^/# sanitizer detail: /' >&2
    echo >&2
  fi
else
  # Could not build sanitizer variant — surface but don't crash the tool;
  # treat as a correctness failure so it can't hide UB.
  overall_pass=0
  failed_list="${failed_list:+$failed_list,}san_build_error"
  echo "# error: sanitizer build failed (see $san_log)" >&2
  head -c 3000 "$san_log" | sed 's/^/# sanitizer compile: /' >&2
  echo >&2
fi

if [ "$overall_pass" -eq 1 ]; then
  cheatsheet_build_fingerprint "" > "$CHEATSHEET_BUILD/check.manifest" || exit 70
  echo "correctness: passed"
else
  echo "correctness: failed"
fi
if [ "$observed_errors" -eq 0 ]; then max_abs="n/a"; max_rel="n/a"; fi
echo "max_abs_err: $max_abs"
echo "max_rel_err: $max_rel"
echo "failed_sizes: ${failed_list:-none}"
exit 0
