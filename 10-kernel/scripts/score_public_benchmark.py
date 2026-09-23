#!/usr/bin/env python3
"""Recompute geometric G_ref_public from paired public-case latencies."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.benchmark.scoring import calculate_public_score


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("latencies", type=Path)
    args = parser.parse_args()
    cases = json.loads(args.latencies.read_text(encoding="utf-8"))["cases"]
    try:
        summary = calculate_public_score(cases)
    except (KeyError, TypeError, ValueError) as error:
        raise SystemExit(str(error)) from error
    for case in cases:
        print(f"{case['case_id']}: speedup={case['speedup']:.6f}")
    print(json.dumps(summary, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
