#!/usr/bin/env python3

import argparse
import json
import os
import sys


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--value", required=True, type=float, help="measured wall time (ms)")
    parser.add_argument("--full", required=True, type=float, help="full-score time (ms)")
    parser.add_argument("--zero", required=True, type=float, help="zero-score time (ms)")
    parser.add_argument("--gamma", default=1.0, type=float, help="curve exponent, >= 1 steepens near full")
    return parser.parse_args()


def curve(value: float, full: float, zero: float, gamma: float) -> float:
    ratio = full * (zero - value) / (value * (zero - full))
    ratio = min(max(ratio, 0.0), 1.0)
    return ratio**gamma


def main() -> int:
    arguments = parse_arguments()
    if not 0 < arguments.full < arguments.zero:
        print("score_curve: require 0 < full < zero", file=sys.stderr)
        return 2
    if not arguments.value > 0:
        print("score_curve: require value > 0", file=sys.stderr)
        return 2
    if not arguments.gamma > 0:
        print("score_curve: require gamma > 0", file=sys.stderr)
        return 2
    score = curve(arguments.value, arguments.full, arguments.zero, arguments.gamma)
    curve_value = 2.0 / (score + 1.0)
    output_path = os.environ.get("HELLOHPC_OUTPUT")
    if output_path:
        payload = {"metrics": {"curve-value": {"value": curve_value, "unit": "score"}}}
        try:
            with open(output_path, "w", encoding="utf-8") as handle:
                json.dump(payload, handle)
        except OSError as error:
            print(f"score_curve: cannot write output: {error}", file=sys.stderr)
            return 2
    print(
        f"score_curve: wall={arguments.value:.3f} ms full={arguments.full:.0f} "
        f"zero={arguments.zero:.0f} gamma={arguments.gamma:g} score={score:.4f} "
        f"curve-value={curve_value:.6f}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
