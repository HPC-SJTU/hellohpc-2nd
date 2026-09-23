#!/usr/bin/env python3
"""Quadratic score-curve transform step (used inside the hellohpc workflow).

Reads the run_benchmark result JSON and maps its wall-time to a normalized
quadratic score:

    r = clamp((base - t) / (base - full), 0, 1)
    s = r^2

and emits the inverse value v = 2 / (s + 1) as the custom metric
`curve-value`, so that hellohpc's builtin/score-ratio with fixed
full-at=1 / zero-at=2 reproduces s exactly:

    score_ratio(v) = 1 * (2 - v) / (v * (2 - 1)) = (2 - v) / v = s

The output JSON ({"metrics": {"curve-value": ...}}) is written to
$HELLOHPC_OUTPUT, the per-step custom-output file the harness collects.
Without the variable the script still prints the values (manual runs).

Usage (as configured in problem.yaml steps):
    python3 src/judge/score_curve.py --result .maimoe-benchmark-result.json \
        --full <full_time_ms> --base <base_time_ms>
"""

import argparse
import json
import os
import sys


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--result", required=True, help="run_benchmark result JSON")
    parser.add_argument("--full", required=True, type=float, help="full-score time (ms)")
    parser.add_argument("--base", required=True, type=float, help="zero-score time (ms)")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if not 0 < arguments.full < arguments.base:
        print("score_curve: require 0 < full < base", file=sys.stderr)
        return 2
    try:
        with open(arguments.result, encoding="utf-8") as handle:
            report = json.load(handle)
    except (OSError, json.JSONDecodeError) as error:
        print(f"score_curve: cannot read result file: {error}", file=sys.stderr)
        return 2
    wall = report["metrics"]["wall-time"]["value"]
    if not isinstance(wall, (int, float)) or wall <= 0:
        print("score_curve: invalid wall-time in result file", file=sys.stderr)
        return 2
    ratio = (arguments.base - wall) / (arguments.base - arguments.full)
    ratio = min(max(ratio, 0.0), 1.0)
    score = ratio * ratio
    curve_value = 2.0 / (score + 1.0)
    output_path = os.environ.get("HELLOHPC_OUTPUT")
    if output_path:
        payload = {
            "metrics": {
                "curve-value": {"value": curve_value, "unit": "score"},
            }
        }
        try:
            with open(output_path, "w", encoding="utf-8") as handle:
                json.dump(payload, handle)
        except OSError as error:
            print(f"score_curve: cannot write output: {error}", file=sys.stderr)
            return 2
    print(
        f"score_curve: wall={wall:.3f} ms full={arguments.full:.0f} "
        f"base={arguments.base:.0f} ratio={ratio:.4f} score={score:.4f} "
        f"curve-value={curve_value:.6f}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
