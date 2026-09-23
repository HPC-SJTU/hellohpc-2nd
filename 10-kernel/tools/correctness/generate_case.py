#!/usr/bin/env python3
"""Deterministic case generator for all required distribution families."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.reference.reference import write_inputs


LENGTH_DISTRIBUTIONS = {
    "uniform_short",
    "uniform_medium",
    "uniform_long",
    "power_law",
    "one_heavy",
    "bimodal",
    "adversarial_alignment",
    "nearly_equal",
}
SCORE_DISTRIBUTIONS = {
    "equal",
    "normal",
    "nearly_equal",
    "large_dynamic_range",
    "one_max",
    "multiple_max",
    "monotonic_increasing",
    "monotonic_decreasing",
    "fp16_boundary",
    "large_positive_offset",
    "block_extremes",
    "low_weight_outlier",
}
X_DISTRIBUTIONS = {
    "large_sparse_perturbation",
    "normal",
    "uniform",
    "small_variance",
    "offset_small_variance",
    "large_constant",
    "extreme_mix",
    "low_weight_outlier",
    "large_magnitude",
    "per_dimension_scale",
    "constant_rows",
    "constant_columns",
}


def _raw_lengths(s: int, distribution: str, rng: np.random.Generator) -> np.ndarray:
    if distribution == "uniform_short":
        return rng.integers(1, 17, size=s)
    if distribution == "uniform_medium":
        return rng.integers(32, 257, size=s)
    if distribution == "uniform_long":
        return rng.integers(512, 4097, size=s)
    if distribution == "power_law":
        return np.minimum(rng.zipf(2.0, size=s) * 4, 16384)
    if distribution == "one_heavy":
        values = rng.integers(1, 17, size=s)
        values[int(rng.integers(0, s))] = 16384
        return values
    if distribution == "bimodal":
        long_mask = rng.random(s) < 0.2
        values = rng.integers(1, 17, size=s)
        values[long_mask] = rng.integers(512, 4097, size=int(long_mask.sum()))
        return values
    if distribution == "adversarial_alignment":
        return rng.choice(np.array([31, 33, 63, 65, 127, 129]), size=s)
    if distribution == "nearly_equal":
        center = int(rng.integers(32, 257))
        return np.maximum(1, center + rng.integers(-2, 3, size=s))
    raise ValueError(f"unknown length distribution: {distribution}")


def _fit_lengths(raw: np.ndarray, n: int | None) -> np.ndarray:
    raw = np.asarray(raw, dtype=np.int64)
    if n is None:
        return raw
    if n < raw.size:
        raise ValueError("N must be at least S")
    remaining = n - raw.size
    weights = np.maximum(raw - 1, 0).astype(np.float64)
    if weights.sum() == 0:
        weights[:] = 1
    scaled = weights * (remaining / weights.sum())
    extra = np.floor(scaled).astype(np.int64)
    residual = remaining - int(extra.sum())
    if residual:
        order = np.argsort(-(scaled - extra), kind="stable")
        extra[order[:residual]] += 1
    result = extra + 1
    if np.max(result) > 16384:
        raise ValueError("requested N produces a segment longer than 16384")
    return result


def generate_lengths(
    s: int, distribution: str, rng: np.random.Generator, n: int | None = None
) -> np.ndarray:
    if distribution not in LENGTH_DISTRIBUTIONS:
        raise ValueError(f"unsupported length distribution: {distribution}")
    return _fit_lengths(_raw_lengths(s, distribution, rng), n)


def generate_score(
    lengths: np.ndarray, distribution: str, rng: np.random.Generator
) -> np.ndarray:
    if distribution not in SCORE_DISTRIBUTIONS:
        raise ValueError(f"unsupported score distribution: {distribution}")
    n = int(lengths.sum())
    if distribution in {"equal", "low_weight_outlier"}:
        values = np.zeros(n)
    elif distribution == "normal":
        values = rng.normal(0, 1, size=n)
    elif distribution == "nearly_equal":
        values = rng.normal(0.25, 1.0e-3, size=n)
    elif distribution == "large_dynamic_range":
        values = rng.uniform(-40, 40, size=n)
    elif distribution == "fp16_boundary":
        values = rng.choice(np.array([-60.0, -20.0, 0.0, 20.0, 60.0]), size=n)
    elif distribution == "large_positive_offset":
        values = rng.normal(0, 1, size=n)
        begin = 0
        for segment, length in enumerate(lengths):
            end = begin + int(length)
            values[begin:end] += 300.0 + 37.0 * (segment % 8)
            begin = end
    else:
        values = rng.normal(0, 1, size=n)
        begin = 0
        for length in lengths:
            end = begin + int(length)
            if distribution == "one_max":
                values[begin + int(rng.integers(0, length))] += 4.0
            elif distribution == "multiple_max":
                count = min(3, int(length))
                indices = rng.choice(np.arange(begin, end), size=count, replace=False)
                values[indices] = 8.0 + rng.normal(0, 1e-3, size=count)
            elif distribution == "monotonic_increasing":
                values[begin:end] = np.linspace(-8, 8, int(length))
            elif distribution == "monotonic_decreasing":
                values[begin:end] = np.linspace(8, -8, int(length))
            elif distribution == "block_extremes":
                # Adjacent 256-row blocks alternate between a deep negative
                # floor and near-zero noise.  Merging partial results across
                # such blocks must survive rescale factors exp(-1000+) that
                # underflow to zero in FP32.
                for block_start in range(begin, end, 256):
                    block_end = min(block_start + 256, end)
                    if ((block_start - begin) // 256) % 2 == 0:
                        values[block_start:block_end] = -1000.0
            begin = end
    return np.asarray(values, dtype=np.float16)


def generate_x(n: int, d: int, distribution: str, rng: np.random.Generator) -> np.ndarray:
    if distribution not in X_DISTRIBUTIONS:
        raise ValueError(f"unsupported x distribution: {distribution}")
    if distribution in {"large_sparse_perturbation", "low_weight_outlier"}:
        # The correlated score/x pattern is applied after offsets are known.
        values = np.zeros((n, d))
    elif distribution == "normal":
        values = rng.normal(0, 1, size=(n, d))
    elif distribution == "uniform":
        values = rng.uniform(-3, 3, size=(n, d))
    elif distribution == "small_variance":
        # Center near zero so m2 - mean^2 remains well-conditioned across
        # legal FP32 reduction trees. Nonzero constant cases cover exact-zero
        # variance separately.
        values = rng.normal(0.0, 2e-3, size=(n, d))
    elif distribution == "large_constant":
        # Every row identical at the FP16 magnitude boundary over a long
        # reduction: the shifted, delta-form merge keeps the mean and the
        # zero variance exact (the original numerical regression).
        values = np.full((n, d), 65504.0)
    elif distribution == "extreme_mix":
        # Signs alternate per row at the FP16 magnitude boundary: the variance
        # is huge, so the mean stays well-conditioned while the reduction runs
        # at full scale.
        signs = np.where(np.arange(n) % 2 == 0, 65504.0, -65504.0)
        values = np.repeat(signs[:, None], d, axis=1)
    elif distribution == "offset_small_variance":
        # Small spread around a large nonzero mean stresses the catastrophic
        # cancellation in m2 - mean^2 for reductions that do not center first.
        # 512 is exactly representable in FP16, so the sampled spread survives
        # input quantization.
        values = rng.normal(512.0, 1.0, size=(n, d))
    elif distribution == "large_magnitude":
        values = rng.uniform(-120, 120, size=(n, d))
    elif distribution == "per_dimension_scale":
        scales = np.geomspace(0.05, 32.0, d)
        values = rng.normal(size=(n, d)) * scales[None, :]
    elif distribution == "constant_rows":
        values = np.repeat(rng.uniform(-4, 4, size=(n, 1)), d, axis=1)
    else:
        values = np.repeat(rng.uniform(-4, 4, size=(1, d)), n, axis=0)
    return np.asarray(values, dtype=np.float16)


def generate_case(spec: dict[str, Any]) -> tuple[np.ndarray, np.ndarray, np.ndarray, dict[str, Any]]:
    if "adversarial" in spec:
        from tools.correctness.adversarial import generate_adversarial
        return generate_adversarial(spec)
    seed = int(spec["seed"])
    s = int(spec["S"])
    d = int(spec["D"])
    rng = np.random.default_rng(seed)
    lengths = generate_lengths(s, spec["length_distribution"], rng, spec.get("N"))
    offsets = np.concatenate(([0], np.cumsum(lengths, dtype=np.int64))).astype(np.int32)
    n = int(offsets[-1])
    score = generate_score(lengths, spec["score_distribution"], rng)
    x = generate_x(n, d, spec["x_distribution"], rng)
    special_case = spec.get("special_case")
    if special_case:
        # Small deterministic regressions for arithmetic paths that ordinary
        # random distributions almost never exercise. Keep these patterns in
        # the public generator so every implementation is checked against the
        # same legal FP16 inputs.
        score = np.zeros(n, dtype=np.float16)
        x = np.zeros((n, d), dtype=np.float16)
        if special_case == "signed_cancellation":
            x[:] = np.float16(0.1)
            x[0] = np.float16(65504.0)
            x[1] = np.float16(-65504.0)
        elif special_case == "signed_cancellation_reordered":
            x[:] = np.float16(0.1)
            x[0] = np.float16(65504.0)
            x[-1] = np.float16(-65504.0)
        elif special_case == "variance_overflow":
            x[:] = np.where(np.arange(n)[:, None] % 2 == 0, 65504.0, -65504.0)
        elif special_case == "exp_underflow":
            score[:-1] = np.float16(-91.0)
        elif special_case == "exp_underflow_boundary":
            score[:-1] = np.float16(-100.0)
        elif special_case == "low_weight_outlier":
            score[0] = np.float16(-16.0)
            x[:] = np.float16(0.1)
            x[0] = np.float16(65504.0)
        else:
            raise ValueError(f"unknown special_case: {special_case}")
    if "low_weight_outlier" in (spec["score_distribution"], spec["x_distribution"]):
        if spec["score_distribution"] != spec["x_distribution"]:
            raise ValueError("low_weight_outlier requires matching score and x distributions")
        position = int(spec.get("outlier_position", 0))
        gap = float(spec.get("outlier_score_gap", 100.0))
        if position < 0 or not np.isfinite(gap) or not 0 < gap <= 65504:
            raise ValueError("invalid low_weight_outlier position or score gap")
        # Every 8-row block has a negligible-weight outlier. Alternating
        # feature signs cover both cancellation directions in one input.
        signs = np.where(np.arange(d) % 2 == 0, 1.0, -1.0)
        score.fill(0)
        x[:] = (-signs * (2.0 ** -8)).astype(np.float16)
        for begin, end in zip(offsets[:-1], offsets[1:]):
            for base in range(int(begin), int(end), 8):
                rows = min(8, int(end) - base)
                if rows == 1:
                    continue
                index = base + position % rows
                score[index] = np.float16(-gap)
                x[index] = (signs * 65504.0).astype(np.float16)
    if spec["x_distribution"] == "large_sparse_perturbation":
        signs = np.where(np.arange(d) % 2 == 0, 1.0, -1.0)
        value = float(spec.get("constant_value", 60032.0))
        perturbed = float(spec.get("perturbed_value", 60000.0))
        position = int(spec.get("perturbation_position", 0))
        if not np.isfinite([value, perturbed]).all() or max(abs(value), abs(perturbed)) > 65504:
            raise ValueError("sparse perturbation values must be finite FP16 values")
        x[:] = (signs * value).astype(np.float16)
        for begin, end in zip(offsets[:-1], offsets[1:]):
            index = int(begin) + position % int(end - begin)
            x[index] = (signs * perturbed).astype(np.float16)
    if "precision_fixture" in spec:
        fixtures = json.loads((Path(__file__).resolve().parents[1] /
                               "data/precision_regressions.json").read_text())["cases"]
        fixture = next((c for c in fixtures if c["case_id"] == spec["precision_fixture"]), None)
        if fixture is None:
            raise ValueError("unknown precision fixture")
        if s != 1 or n != len(fixture["score"]) or d != fixture["D"]:
            raise ValueError("precision fixture shape mismatch")
        if float(spec.get("epsilon", 1e-5)) != float(fixture["epsilon"]):
            raise ValueError("precision fixture epsilon mismatch")
        score = np.asarray(fixture["score"], dtype=np.float16)
        x = np.repeat(np.asarray(fixture["x_column"], dtype=np.float16)[:, None], d, axis=1)
    metadata = dict(spec)
    metadata.update(
        {
            "N": n,
            "dtype": "float16",
            "distribution_name": "/".join(
                [spec["length_distribution"], spec["score_distribution"], spec["x_distribution"]]
            ),
            "max_segment_length": int(lengths.max()),
            "mean_segment_length": float(lengths.mean()),
            "correctness_only": bool(spec.get("correctness_only", False)),
        }
    )
    return score, x, offsets, metadata


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    spec = json.loads(args.spec.read_text(encoding="utf-8"))
    score, x, offsets, metadata = generate_case(spec)
    write_inputs(args.output_dir, score, x, offsets, metadata)


if __name__ == "__main__":
    main()
