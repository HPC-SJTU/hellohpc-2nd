from __future__ import annotations

import math
from typing import Any

import numpy as np


def compute_field(
    points: Any,
    centers: Any,
    weights: Any,
    scales: Any,
    bias: Any,
    trig_scale: Any,
    trig_vec: Any,
) -> np.ndarray:
    """Contestant entry point with a correct, intentionally slow baseline."""
    q_count, dimension = _shape_2d(points, "points")
    c_count, center_dimension = _shape_2d(centers, "centers")
    trig_rows, trig_dimension = _shape_2d(trig_vec, "trig_vec")
    _shape_1d(weights, "weights", expected=c_count)
    _shape_1d(scales, "scales", expected=c_count)
    _shape_1d(bias, "bias", expected=c_count)
    _shape_1d(trig_scale, "trig_scale", expected=c_count)
    if center_dimension != dimension:
        raise ValueError(f"centers has dim {center_dimension}, expected {dimension}")
    if trig_rows != c_count or trig_dimension != dimension:
        raise ValueError(
            f"trig_vec must have shape ({c_count}, {dimension}), "
            f"got ({trig_rows}, {trig_dimension})"
        )

    output = np.empty(q_count, dtype=np.float32)
    for q_index in range(q_count):
        point = points[q_index]
        accumulator = 0.0
        for c_index in range(c_count):
            center = centers[c_index]
            trig_row = trig_vec[c_index]
            square_distance = 0.0
            dot_product = 0.0
            for d_index in range(dimension):
                point_value = float(point[d_index])
                delta = point_value - float(center[d_index])
                square_distance += delta * delta
                dot_product += point_value * float(trig_row[d_index])
            accumulator += float(weights[c_index]) * math.exp(
                -float(scales[c_index]) * square_distance
            )
            accumulator += float(bias[c_index]) * math.sin(
                float(trig_scale[c_index]) * dot_product
            )
        output[q_index] = accumulator
    return output


def _shape_2d(value: Any, name: str) -> tuple[int, int]:
    shape = getattr(value, "shape", None)
    if shape is not None:
        if len(shape) != 2:
            raise ValueError(f"{name} must be 2D, got shape {shape}")
        return int(shape[0]), int(shape[1])
    rows = len(value)
    columns = len(value[0]) if rows else 0
    if any(len(row) != columns for row in value):
        raise ValueError(f"{name} must be rectangular")
    return rows, columns


def _shape_1d(value: Any, name: str, expected: int | None = None) -> int:
    shape = getattr(value, "shape", None)
    if shape is not None:
        if len(shape) != 1:
            raise ValueError(f"{name} must be 1D, got shape {shape}")
        size = int(shape[0])
    else:
        size = len(value)
    if expected is not None and size != expected:
        raise ValueError(f"{name} must have length {expected}, got {size}")
    return size
