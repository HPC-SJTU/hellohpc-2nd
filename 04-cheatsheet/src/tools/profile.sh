#!/usr/bin/env bash
# tools/profile.sh — profile every public size with the task's configured threads.
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_common.sh"

cheatsheet_lock_tool exclusive || exit 70
cheatsheet_require_kernel

log="$CHEATSHEET_BUILD/build.log"
mkdir -p "$CHEATSHEET_BUILD"
if ! cheatsheet_ensure_current_build "" "$CHEATSHEET_BUILD/harness" "$log"; then
    echo "# error: build failed (see $log)" >&2
    exit 5
fi

sizes="$(cheatsheet_public_sizes)" || { echo "# error: cannot resolve sizes" >&2; exit 6; }

first=1
for size in $sizes; do
  out="$(cheatsheet_run_pinned "$size" profile)"
  rc=$?
  if [ "$rc" -ne 0 ]; then
    echo "# error: profile crashed at size $size (rc=$rc)" >&2
    exit 7
  fi

  [ "$first" -eq 0 ] && echo "#"
  first=0
  echo "# size: $size"
  echo "$out" | awk -F': ' '
    /^profile_status:/   {print}
    /^ipc:/              {print}
    /^l1d_mpki:/         {print}
    /^llc_mpki:/         {print}
    /^branch_miss_rate:/ {print}'
done
exit 0
