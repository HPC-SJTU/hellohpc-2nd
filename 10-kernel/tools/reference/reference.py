#!/usr/bin/env python3
"""CPU reference for Ragged Softmax Moments."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

import numpy as np


DEFAULT_EPSILON = 1.0e-5


def validate_inputs(score: np.ndarray, x: np.ndarray, offsets: np.ndarray) -> None:
    if score.dtype != np.float16 or score.ndim != 1:
        raise ValueError("score must be float16 [N]")
    if x.dtype != np.float16 or x.ndim != 2 or x.shape[0] != score.size:
        raise ValueError("x must be float16 [N, D]")
    if offsets.dtype != np.int32 or offsets.ndim != 1 or offsets.size < 2:
        raise ValueError("offsets must be int32 [S + 1]")
    if offsets[0] != 0 or offsets[-1] != score.size:
        raise ValueError("offset endpoints must be 0 and N")
    if np.any(offsets[1:] <= offsets[:-1]):
        raise ValueError("segments must be nonempty and strictly increasing")
    if not np.isfinite(score).all() or not np.isfinite(x).all():
        raise ValueError("inputs must not contain NaN or Inf")


# Increment whenever the mathematical oracle or acceptance contract changes.
PRECISION_CONTRACT = "rsm-math-fp64-conditioned-v2"


def reference_with_conditioning(
    score: np.ndarray,
    x: np.ndarray,
    offsets: np.ndarray,
    epsilon: float = DEFAULT_EPSILON,
    accumulation: str = "float64",
) -> tuple[tuple[np.ndarray, np.ndarray, np.ndarray], np.ndarray]:
    """Return moments and weighted mean absolute deviations.

    The formal oracle evaluates exp, normalization, the unrounded mean and
    centered variance in FP64; only the three final outputs are rounded to FP32.
    ``float32`` selects an entirely FP32 diagnostic path, never the judge oracle.
    The deviation is input-derived, per segment/feature, and independent of any
    submitted output. It measures sensitivity to small weight perturbations.
    """
    validate_inputs(score, x, offsets)
    if not np.isfinite(epsilon) or epsilon <= 0:
        raise ValueError("epsilon must be finite and positive")
    if accumulation not in {"float32", "float64"}:
        raise ValueError("accumulation must be float32 or float64")
    # The dispatch ABI carries one FP32 epsilon, regardless of oracle precision.
    epsilon32 = np.float32(epsilon)
    if not np.isfinite(epsilon32) or epsilon32 <= 0:
        raise ValueError("epsilon must be representable as a positive finite FP32")
    accumulator = np.float32 if accumulation == "float32" else np.float64
    segments, dimensions = offsets.size - 1, x.shape[1]
    mean = np.empty((segments, dimensions), dtype=np.float32)
    rstd = np.empty_like(mean)
    logsumexp = np.empty(segments, dtype=np.float32)
    deviation = np.empty((segments, dimensions), dtype=np.float64)

    for segment in range(segments):
        begin, end = int(offsets[segment]), int(offsets[segment + 1])
        segment_score = score[begin:end].astype(accumulator)
        segment_x = x[begin:end].astype(accumulator)
        maximum = np.max(segment_score)
        with np.errstate(under="ignore"):
            weights = np.exp(segment_score - maximum)
        normalizer = np.sum(weights, dtype=accumulator)
        segment_mean = np.sum(weights[:, None] * segment_x, axis=0,
                              dtype=accumulator) / normalizer
        centered = segment_x - segment_mean
        variance = np.sum(weights[:, None] * centered * centered, axis=0,
                          dtype=accumulator) / normalizer
        deviation[segment] = np.sum(weights[:, None] * np.abs(centered), axis=0,
                                    dtype=accumulator) / normalizer
        mean[segment] = segment_mean
        rstd[segment] = accumulator(1) / np.sqrt(variance + accumulator(epsilon32))
        logsumexp[segment] = np.log(normalizer) + maximum
    return (mean, rstd, logsumexp), deviation


def ragged_softmax_moments(
    score: np.ndarray,
    x: np.ndarray,
    offsets: np.ndarray,
    epsilon: float = DEFAULT_EPSILON,
    accumulation: str = "float64",
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Compute the mathematical FP64 oracle (FP32 is diagnostic only)."""
    outputs, _ = reference_with_conditioning(score, x, offsets, epsilon, accumulation)
    return outputs


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_inputs(
    directory: Path,
    score: np.ndarray,
    x: np.ndarray,
    offsets: np.ndarray,
    metadata: dict[str, Any] | None = None,
) -> dict[str, Any]:
    validate_inputs(score, x, offsets)
    directory.mkdir(parents=True, exist_ok=True)
    files = {
        "score": (directory / "score.bin", np.ascontiguousarray(score)),
        "x": (directory / "x.bin", np.ascontiguousarray(x)),
        "offsets": (directory / "offsets.bin", np.ascontiguousarray(offsets)),
    }
    for path, array in files.values():
        array.tofile(path)
    result: dict[str, Any] = dict(metadata or {})
    result.update(
        {
            "N": int(score.size),
            "D": int(x.shape[1]),
            "S": int(offsets.size - 1),
            "dtype": {"score": "float16", "x": "float16", "offsets": "int32"},
            "format": "raw-little-endian-v1",
            "files": {
                name: {"name": path.name, "sha256": _sha256(path)}
                for name, (path, _) in files.items()
            },
        }
    )
    (directory / "meta.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return result


def read_inputs(directory: Path) -> tuple[np.ndarray, np.ndarray, np.ndarray, dict[str, Any]]:
    metadata = json.loads((directory / "meta.json").read_text(encoding="utf-8"))
    n, d, s = metadata["N"], metadata["D"], metadata["S"]
    score = np.fromfile(directory / "score.bin", dtype=np.float16, count=n)
    x = np.fromfile(directory / "x.bin", dtype=np.float16, count=n * d).reshape(n, d)
    offsets = np.fromfile(directory / "offsets.bin", dtype=np.int32, count=s + 1)
    validate_inputs(score, x, offsets)
    return score, x, offsets, metadata


def write_outputs(
    directory: Path,
    mean: np.ndarray,
    rstd: np.ndarray,
    logsumexp: np.ndarray,
) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    arrays = {"mean": mean, "rstd": rstd, "logsumexp": logsumexp}
    metadata: dict[str, Any] = {"format": "raw-little-endian-v1", "files": {}}
    for name, array in arrays.items():
        value = np.ascontiguousarray(array, dtype=np.float32)
        path = directory / f"{name}.bin"
        value.tofile(path)
        metadata["files"][name] = {
            "name": path.name,
            "shape": list(value.shape),
            "dtype": "float32",
            "sha256": _sha256(path),
        }
    (directory / "meta.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--epsilon", type=float, default=DEFAULT_EPSILON)
    parser.add_argument("--accumulation", choices=("float32", "float64"), default="float64")
    args = parser.parse_args()
    score, x, offsets, _ = read_inputs(args.input_dir)
    outputs = ragged_softmax_moments(score, x, offsets, args.epsilon, args.accumulation)
    write_outputs(args.output_dir, *outputs)


if __name__ == "__main__":
    main()
