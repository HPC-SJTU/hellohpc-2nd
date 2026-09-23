#!/usr/bin/env python3
"""Compare device outputs with the formal CPU reference."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.reference.reference import (
    DEFAULT_EPSILON, PRECISION_CONTRACT, reference_with_conditioning, read_inputs,
)


FP32_UNIT_ROUNDOFF = 2.0 ** -24
MEAN_CONDITION_FACTOR = 8.0 * FP32_UNIT_ROUNDOFF

TOLERANCES = {
    "mean": {"atol": 3e-3, "rtol": 2.0 * FP32_UNIT_ROUNDOFF},
    "rstd": {"atol": 0.0, "rtol": 4e-3},
    "logsumexp": {"atol": 5e-3, "rtol": 2.0 * FP32_UNIT_ROUNDOFF},
}


def _read_output(path: Path, shape: tuple[int, ...]) -> np.ndarray:
    expected_count = int(np.prod(shape, dtype=np.int64))
    output = np.fromfile(path, dtype=np.float32)
    if output.size != expected_count:
        raise ValueError(f"{path}: expected {expected_count} float32 values, got {output.size}")
    return output.reshape(shape)


def compare_case(
    input_dir: Path,
    output_dir: Path,
    epsilon: float | None = None,
) -> dict[str, Any]:
    score, x, offsets, metadata = read_inputs(input_dir)
    effective_epsilon = float(metadata.get("epsilon", DEFAULT_EPSILON) if epsilon is None else epsilon)
    expected_values, mean_deviation = reference_with_conditioning(
        score, x, offsets, effective_epsilon, accumulation="float64"
    )
    s, d = offsets.size - 1, x.shape[1]
    shapes = {"mean": (s, d), "rstd": (s, d), "logsumexp": (s,)}
    result: dict[str, Any] = {
        "precision_contract": PRECISION_CONTRACT,
        "case_id": metadata.get("case_id", input_dir.name),
        "N": int(score.size),
        "D": int(d),
        "S": int(s),
        "epsilon": effective_epsilon,
        "passed": True,
        "outputs": {},
    }
    for (name, expected) in zip(("mean", "rstd", "logsumexp"), expected_values):
        actual = _read_output(output_dir / f"{name}.bin", shapes[name])
        finite = bool(np.isfinite(actual).all())
        # Do comparison arithmetic in FP64, including the acceptance boundary.
        actual64, expected64 = actual.astype(np.float64), expected.astype(np.float64)
        absolute = np.where(np.isfinite(actual64), np.abs(actual64 - expected64), np.inf)
        relative = absolute / np.maximum(np.abs(expected64), 1e-12)
        tolerance = TOLERANCES[name]
        atol = float(tolerance["atol"])
        rtol = float(tolerance["rtol"])
        # Margin against the actual acceptance rule, element-wise: values <= 1
        # pass, larger values report how far from passing they are.
        allowed = atol + rtol * np.abs(expected64)
        if name == "mean":
            allowed = allowed + MEAN_CONDITION_FACTOR * mean_deviation
        normalized = absolute / allowed
        # Finite positive rstd is part of the mathematical output domain.
        invalid = ~np.isfinite(actual) | ((actual <= 0) if name == "rstd" else False)
        failing = invalid | (normalized > 1.0)
        failure_count = int(np.count_nonzero(failing))
        worst_flat = int(np.argmax(absolute))
        worst_index = tuple(int(index) for index in np.unravel_index(worst_flat, absolute.shape))
        worst_normalized_flat = int(np.argmax(normalized))
        worst_normalized_index = tuple(
            int(index) for index in np.unravel_index(worst_normalized_flat, normalized.shape)
        )
        passed = finite and not failing.any()
        entry = {
            "passed": passed,
            "finite": finite,
            "atol": atol,
            "rtol": rtol,
            "max_abs_error": float(np.max(absolute)),
            "p99_abs_error": float(np.quantile(absolute, 0.99)) if finite else float("inf"),
            "max_rel_error": float(np.max(relative)),
            "max_normalized_error": float(np.max(normalized)),
            "worst_index": list(worst_index),
            "actual_at_worst": float(actual[worst_index]),
            "expected_at_worst": float(expected[worst_index]),
            "worst_normalized_index": list(worst_normalized_index),
            "actual_at_worst_normalized": float(actual[worst_normalized_index]),
            "expected_at_worst_normalized": float(expected[worst_normalized_index]),
            "failure_count": failure_count,
            "allowed_error_at_worst_normalized": float(allowed[worst_normalized_index]),
            "min_allowed_error": float(np.min(allowed)),
            "max_allowed_error": float(np.max(allowed)),
        }
        if name == "mean":
            entry["conditioning_factor"] = MEAN_CONDITION_FACTOR
            entry["weighted_abs_deviation_at_worst_normalized"] = float(
                mean_deviation[worst_normalized_index])
        if failure_count:
            first_flat = int(np.argmax(failing))
            first_index = tuple(
                int(index) for index in np.unravel_index(first_flat, normalized.shape)
            )
            entry["first_failure_index"] = list(first_index)
            entry["first_failure_normalized_error"] = float(normalized[first_index])
            entry["actual_at_first_failure"] = float(actual[first_index])
            entry["expected_at_first_failure"] = float(expected[first_index])
        result["outputs"][name] = entry
        result["passed"] = result["passed"] and passed
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--epsilon", type=float)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    result = compare_case(args.input_dir, args.output_dir, args.epsilon)
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    raise SystemExit(0 if result["passed"] else 1)


if __name__ == "__main__":
    main()
