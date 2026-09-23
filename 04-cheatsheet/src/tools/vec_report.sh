#!/usr/bin/env bash
# tools/vec_report.sh — g++ vectorization report summary.
#
# Recompile ONLY $CHEATSHEET_WORK/kernel.cpp to /dev/null with vec-info flags, capture
# stderr, and summarize into:
#   vectorization: none|partial|full
#   hot_loop_vectorized: yes|no
#   top_missed: <file:line (reason)>|none
# Heuristic based on counts of "optimized:" vs "missed:" lines from kernel.cpp.
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_common.sh"

cheatsheet_lock_tool shared || exit 70
cheatsheet_require_kernel

log="$CHEATSHEET_BUILD/vec.log"
mkdir -p "$CHEATSHEET_BUILD"

# Match the submitted-source compile scope and use a fresh empty output mount.
# The surrounding build directory may contain trusted harness/oracle artifacts.
stage="$(mktemp -d "$CHEATSHEET_BUILD/.cheatsheet-vec-report.XXXXXX")" || exit 70
# shellcheck disable=SC2086
cheatsheet_isolated_compile submitted "$CHEATSHEET_COMPILE_SECONDS" "$stage" \
  "$CHEATSHEET_CXX" -I"$CHEATSHEET_HARNESS_SRC" $CHEATSHEET_CXXFLAGS $CHEATSHEET_VEC_FLAGS \
  "${CHEATSHEET_KERNEL_EXTRA_FLAGS[@]}" \
  -c "$CHEATSHEET_KERNEL" -o "$stage/kernel.o" 2> "$log"
rc=$?
rm -f -- "$stage/kernel.o"
rmdir -- "$stage" 2>/dev/null || true
if [ "$rc" -ne 0 ]; then
  # kernel.cpp doesn't compile standalone (e.g. syntax error). Report and exit nonzero.
  echo "# error: kernel.cpp failed to compile for vec report (see $log)" >&2
  echo "vectorization: none"
  echo "hot_loop_vectorized: no"
  echo "top_missed: none"
  exit 8
fi

kbase="$(basename "$CHEATSHEET_KERNEL")"

# Count optimized / missed lines that pertain to the agent's kernel file only.
opt=$(grep -E "$kbase:[0-9]+.*optimized: loop vectorized" "$log" | wc -l | tr -d ' ')
# g++ phrasing: "note: loop vectorized" appears with -fopt-info-vec-optimized.
if [ "$opt" -eq 0 ]; then
  opt=$(grep -E "$kbase:[0-9]+" "$log" | grep -c 'loop vectorized')
fi
missed=$(grep -E "$kbase:[0-9]+" "$log" | grep -c 'missed:')

# vectorization verdict
if [ "$opt" -gt 0 ] && [ "$missed" -eq 0 ]; then
  vecclass="full"
elif [ "$opt" -gt 0 ]; then
  vecclass="partial"
else
  vecclass="none"
fi

# hot loop heuristic: any loop in kernel.cpp vectorized => yes
if [ "$opt" -gt 0 ]; then
  hot="yes"
else
  hot="no"
fi

# top missed: first missed line for kernel.cpp, with reason in parentheses.
missed_line="$(grep -E "$kbase:[0-9]+.*missed:" "$log" | head -1)"
if [ -n "$missed_line" ]; then
  # form: path/kernel.cpp:34:12: missed: <reason>
  loc="$(echo "$missed_line" | grep -oE "$kbase:[0-9]+" | head -1)"
  reason="$(echo "$missed_line" | sed -E 's/.*missed: *//' | sed 's/[[:space:]]*$//')"
  top_missed="$loc ($reason)"
else
  top_missed="none"
fi

echo "vectorization: $vecclass"
echo "hot_loop_vectorized: $hot"
echo "top_missed: $top_missed"
exit 0
