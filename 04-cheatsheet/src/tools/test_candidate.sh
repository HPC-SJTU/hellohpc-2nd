#!/usr/bin/env bash
# tools/test_candidate.sh — atomic measured candidate workflow.
#
# A single Agent call performs the only valid round transition:
# build -> exact public correctness + UB trap -> public benchmark/checkpoint.
# Each stage short-circuits on failure, so an unchecked candidate is never
# timed and a failed candidate cannot consume the next optimization round.
tool_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

run_stage() {
  local name="$1" output rc
  output="$(bash "$tool_dir/$name.sh" 2>&1)"
  rc=$?
  printf '%s\n' "$output"
  return "$rc"
}

echo "test_stage: build"
if ! run_stage build; then
  echo "candidate_status: compile_failed"
  exit 10
fi

echo "test_stage: check"
check_output="$(bash "$tool_dir/check.sh" 2>&1)"
check_rc=$?
printf '%s\n' "$check_output"
if [ "$check_rc" -ne 0 ]; then
  echo "candidate_status: check_error"
  exit 11
fi
if ! grep -q '^correctness: passed$' <<< "$check_output"; then
  echo "candidate_status: correctness_failed"
  exit 12
fi

echo "test_stage: benchmark"
bench_output="$(bash "$tool_dir/bench.sh" 2>&1)"
bench_rc=$?
printf '%s\n' "$bench_output"
if [ "$bench_rc" -ne 0 ]; then
  echo "candidate_status: benchmark_failed"
  exit 13
fi
echo "candidate_status: measured"
echo "# Preserve this source and compiler flags before further edits. Final files alone are scored; a later broken version is not automatically restored."
exit 0
