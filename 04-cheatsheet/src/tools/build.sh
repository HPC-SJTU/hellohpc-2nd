#!/usr/bin/env bash
# tools/build.sh — compile the under-test harness.
#
# Trusted binary = harness_main.o + kernel_reference.o + parent-side profiler.
# kernel.cpp is compiled separately as the restricted worker's shared object.
# Prints:  build: ok|failed   warnings: N
# Exit non-zero IFF compile failed (Runner flags Compile Error on non-zero).
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_common.sh"

cheatsheet_lock_tool shared || exit 70
cheatsheet_require_kernel

mkdir -p "$CHEATSHEET_BUILD"
log="$CHEATSHEET_BUILD/build.log"

cheatsheet_compile_harness "" "$CHEATSHEET_BUILD/harness" "$log"
rc=$?

# count g++ warning lines (grep -c already prints 0 on no-match; || true swallows its exit 1)
warns=$(grep -c 'warning:' "$log" 2>/dev/null || true)
warns=${warns:-0}

if [ "$rc" -eq 0 ]; then
  echo "build: ok"
  echo "warnings: $warns"
  exit 0
else
  echo "build: failed"
  echo "warnings: $warns"
  # surface a short compiler-error tail to stderr for the Runner's state summary
  grep -E 'error:|error ' "$log" 2>/dev/null | head -5 >&2
  exit 1
fi
