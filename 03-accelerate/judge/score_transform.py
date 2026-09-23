#!/usr/bin/env python3
"""Map the official logarithmic score exactly onto builtin/score-ratio.

With fixed full-at=1 and zero-at=2, score-ratio(x) = 2/x - 1. Mapping a
desired normalized score p to x=2/(1+p) therefore preserves p exactly.
"""

from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path


def normalized_log_score(runtime_ms: float, full_time_ms: float, max_time_ms: float) -> float:
    values = (runtime_ms, full_time_ms, max_time_ms)
    if not all(math.isfinite(value) and value > 0.0 for value in values):
        raise ValueError("runtime and thresholds must be finite positive numbers")
    if full_time_ms >= max_time_ms:
        raise ValueError("full-time must be smaller than max-time")
    if runtime_ms <= full_time_ms:
        return 1.0
    if runtime_ms >= max_time_ms:
        return 0.0
    return (math.log(max_time_ms) - math.log(runtime_ms)) / (
        math.log(max_time_ms) - math.log(full_time_ms)
    )


def score_ratio_value(normalized_score: float) -> float:
    if not math.isfinite(normalized_score) or not 0.0 <= normalized_score <= 1.0:
        raise ValueError("normalized score must be finite and in [0, 1]")
    return 2.0 / (1.0 + normalized_score)


def _write_protocol_output(path: Path, score_value: float) -> None:
    payload = {"outputs": {"score_value": score_value}}
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp-{os.getpid()}")
    try:
        temporary.write_text(json.dumps(payload, separators=(",", ":")) + "\n", encoding="utf-8")
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--kind", choices=("sample", "correctness", "performance"), required=True)
    parser.add_argument("--runtime-ms", type=float, required=True)
    parser.add_argument("--full-time-ms", type=float, required=True)
    parser.add_argument("--max-time-ms", type=float, required=True)
    args = parser.parse_args()

    if args.kind == "performance":
        normalized = normalized_log_score(args.runtime_ms, args.full_time_ms, args.max_time_ms)
    else:
        if not math.isfinite(args.runtime_ms) or args.runtime_ms <= 0.0:
            raise ValueError("runtime must be finite and positive")
        normalized = 1.0

    output_path = os.environ.get("HELLOHPC_OUTPUT")
    if not output_path:
        raise RuntimeError("HELLOHPC_OUTPUT is not set")
    _write_protocol_output(Path(output_path), score_ratio_value(normalized))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
