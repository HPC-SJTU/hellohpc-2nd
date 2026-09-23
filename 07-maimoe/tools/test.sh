#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
REPORT_DIR="$ROOT/tools/report"

DATA=Data
CONFIG=solution/KstroParam.toml
SAMPLES=(1 2 3 4)
REPEAT=3
WARMUP=1
TIMEOUT=300
REFRESH=0
NPROC=$(nproc)
if (( NPROC > 8 )); then
    NPROC=8
fi
CPUS=$(seq -s, 0 $(( NPROC - 1 )))

while [[ $# -gt 0 ]]; do
    case $1 in
        --sample) shift; IFS=, read -ra SAMPLES <<< "$1" ;;
        --data) shift; DATA=$1 ;;
        --config) shift; CONFIG=$1 ;;
        --refresh) REFRESH=1 ;;
        --repeat) shift; REPEAT=$1 ;;
        --warmup) shift; WARMUP=$1 ;;
        --cpus) shift; CPUS=$1 ;;
        --timeout) shift; TIMEOUT=$1 ;;
        *) echo "test: unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

if [[ ! -f "$ROOT/$CONFIG" ]]; then
    echo "test: config file not found: $CONFIG" >&2
    exit 2
fi

for s in "${SAMPLES[@]}"; do
    if [[ ! -f "$ROOT/$DATA/Sample${s}/input/manifest.mmf" ]]; then
        echo "test: missing $DATA/Sample${s} (评测数据由附件提供，请放置后重试，或用 --data 指定目录)" >&2
        exit 2
    fi
done

if ! command -v rsync >/dev/null 2>&1; then
    echo "test: rsync is required" >&2
    exit 2
fi

mkdir -p "$REPORT_DIR"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/maimoe-test.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

echo "test: staging workspace $WORK"
echo "test:   [1/4] 复制源码"
rsync -a \
    --exclude .git --exclude 'build*' --exclude out --exclude 'out-*' \
    --exclude .auto-eval --exclude .hellohpc --exclude '*.tmp' \
    --exclude tools/report --exclude '/Data/' \
    "$ROOT"/ "$WORK"/
echo "test:   [2/4] 复制数据: ${SAMPLES[*]/#/Data/Sample}"
CACHE="${TMPDIR:-/tmp}/maimoe-data-cache-$(basename "$DATA")"
mkdir -p "$WORK/$DATA"
for s in "${SAMPLES[@]}"; do
    if [[ "$REFRESH" == 1 || ! -f "$CACHE/Sample$s/input/manifest.mmf" ]]; then
        rm -rf "$CACHE/Sample$s"
        mkdir -p "$CACHE"
        tar -C "$ROOT/$DATA" -cf - "Sample$s" | tar -C "$CACHE" -xf -
    fi
    cp -al "$CACHE/Sample$s" "$WORK/$DATA/Sample$s"
done
echo "test:   [3/4] 复制配置"
cp "$ROOT/$CONFIG" "$WORK/solution/KstroParam.toml"
if [[ -x "$ROOT/build/maimoe" && -x "$ROOT/build/maimoe_kernel" && -x "$ROOT/build/maimoe_check" ]]; then
    echo "test:   [4/4] 复制二进制"
    mkdir -p "$WORK/build"
    cp "$ROOT"/build/maimoe{,_kernel,_check} "$WORK"/build/
else
    echo "test:   [4/4] 构建二进制"
    (cd "$WORK" && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release &&
        cmake --build build --target maimoe maimoe_kernel maimoe_check -j)
fi
EXE="$WORK/build/maimoe"
CHECKER="$WORK/build/maimoe_check"

STAMP=$(date +%Y%m%d-%H%M%S)
MD="$REPORT_DIR/test-$STAMP.md"
JSON="$REPORT_DIR/test-$STAMP.json"
{
    echo "# MaiMoe 快速评测"
    echo ""
    echo "- 时间: $(date '+%Y-%m-%d %H:%M:%S %z')"
    echo "- samples: ${SAMPLES[*]}; repeat=$REPEAT warmup=$WARMUP cpus=$CPUS"
    echo ""
} > "$MD"

ALL_JSON=()
for s in "${SAMPLES[@]}"; do
    echo "== Sample$s ==" | tee -a "$MD"
    RESULT="$WORK/result-$s.json"
    ALL_JSON+=("$RESULT")
    (cd "$WORK" && python3 -u src/judge/run_benchmark.py \
        --executable "$EXE" \
        --checker "$CHECKER" \
        --dataset "$WORK/$DATA/Sample${s}/input" \
        --expected "$WORK/$DATA/Sample${s}/expected.bin" \
        --output-root "$WORK/.maimoe-output/sample$s" \
        --result-file "$RESULT" \
        --cpus "$CPUS" \
        "--warmup=$WARMUP" "--repeat=$REPEAT" \
        "--timeout-seconds=$TIMEOUT") 2>&1 | tee -a "$MD"
    echo "" >> "$MD"
done

python3 - "$JSON" "${ALL_JSON[@]}" <<'PY'
import json, sys
output, *inputs = sys.argv[1:]
document = {}
for path in inputs:
    name = path.rsplit("/", 1)[-1].replace("result-", "").replace(".json", "")
    try:
        document[name] = json.load(open(path, encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        document[name] = {"error": str(error)}
with open(output, "w", encoding="utf-8") as stream:
    json.dump(document, stream, indent=2, sort_keys=True)
    stream.write("\n")
PY

echo "test: done. report: $MD  json: $JSON"
