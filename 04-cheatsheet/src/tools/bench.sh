#!/usr/bin/env bash
# tools/bench.sh — pinned benchmark over public sizes.
#
# taskset -c $CHEATSHEET_CORES  OMP_NUM_THREADS=$CHEATSHEET_OMP_THREADS OMP_PROC_BIND=$CHEATSHEET_OMP_BIND
# OMP_PLACES=$CHEATSHEET_OMP_PLACES  harness bench <size>  over public sizes (or CHEATSHEET_SIZE).
# Prints per size: median_time_ms / gflops / variance / size (one block per size).
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_common.sh"

cheatsheet_lock_tool exclusive || exit 70
cheatsheet_require_kernel

# Ensure the artifacts represent the current kernel.cpp, not an earlier Agent
# edit that happened to leave an executable in build/.
log="$CHEATSHEET_BUILD/build.log"
mkdir -p "$CHEATSHEET_BUILD"
if ! cheatsheet_ensure_current_build "" "$CHEATSHEET_BUILD/harness" "$log"; then
    echo "# error: build failed (see $log)" >&2
    exit 5
fi

# Benchmarking an unchecked fingerprint used to return success with
# `best: not_eligible`.  That made an invalid tool order look like a completed
# optimization round and also spent time measuring candidates that could not
# be checkpointed.  Refuse it before timing instead.
if ! cheatsheet_candidate_is_checked; then
  echo "# error: current candidate has not passed a matching check" >&2
  echo "candidate_status: unchecked"
  echo "required_action: run bash tools/test_candidate.sh"
  exit 9
fi

sizes="$(cheatsheet_public_sizes)" || { echo "# error: cannot resolve sizes" >&2; exit 6; }

first=1
times=""
size_count=0
max_variance=0
for sz in $sizes; do
  out="$(cheatsheet_run_pinned "$sz" bench 2>&1)"
  rc=$?
  if [ "$rc" -ne 0 ]; then
    echo "# error: bench crashed at size $sz (rc=$rc)" >&2
    printf '%s\n' "$out" | head -c 3000 | sed 's/^/# diagnostic: /' >&2
    exit 7
  fi
  measured="$(echo "$out" | awk -F': ' '/^median_time_ms:/{print $2}')"
  variance="$(echo "$out" | awk -F': ' '/^variance:/{print $2}' | tr -d '%')"
  if awk -v value="$variance" -v threshold="$CHEATSHEET_VARIANCE_THRESHOLD_PCT" \
      'BEGIN{exit !(value+0>threshold+0)}'; then
    retry="$(cheatsheet_run_pinned "$sz" bench 2>&1)"
    retry_rc=$?
    if [ "$retry_rc" -ne 0 ]; then
      echo "# error: variance retry crashed at size $sz (rc=$retry_rc)" >&2
      exit 7
    fi
    retry_measured="$(echo "$retry" | awk -F': ' '/^median_time_ms:/{print $2}')"
    retry_variance="$(echo "$retry" | awk -F': ' '/^variance:/{print $2}' | tr -d '%')"
    if awk -v first="$variance" -v second="$retry_variance" \
        'BEGIN{exit !(second+0<first+0)}'; then
      out="$retry"
      measured="$retry_measured"
      variance="$retry_variance"
    fi
    echo "# variance_retry: size=$sz selected=${variance}% threshold=${CHEATSHEET_VARIANCE_THRESHOLD_PCT}%"
  fi

  # pass through the four keys the harness prints, in order
  [ "$first" -eq 0 ] && echo "#"     # separator comment between blocks
  first=0
  if ! awk -v value="$measured" 'BEGIN{exit !(value+0>0)}'; then
    echo "# error: invalid benchmark time at size $sz" >&2
    exit 7
  fi
  times="${times:+$times }$measured"
  max_variance="$(awk -v previous="$max_variance" -v current="$variance" \
      'BEGIN{print (current+0>previous+0 ? current : previous)}')"
  size_count=$((size_count + 1))
  echo "$out" | awk -F': ' '
    /^median_time_ms:/ {print}
    /^gflops:/         {print}
    /^variance:/       {print}
    /^size:/           {print}'
done
public_geomean_ms="$(awk -v values="$times" 'BEGIN{
  count=split(values, item, " "); total=0
  for(i=1;i<=count;i++) total+=log(item[i])
  printf "%.9f", exp(total/count)
}')"
echo "#"
echo "public_geomean_ms: $public_geomean_ms"
echo "public_variance_pct: $max_variance"

if awk -v value="$max_variance" -v threshold="$CHEATSHEET_VARIANCE_THRESHOLD_PCT" 'BEGIN{exit !(value+0>threshold+0)}'; then
  echo "timing_reliability: noisy"
  echo "# candidate passed correctness, but timing differences are inconclusive; do not claim a speedup or repeatedly retest unchanged files. Preserve the checkpoint; other small experiments may continue while budget remains. Formal scoring remeasures independently."
else
  echo "timing_reliability: within_variance_threshold"
fi
exit 0
