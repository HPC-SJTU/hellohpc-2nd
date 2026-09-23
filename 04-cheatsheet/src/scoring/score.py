"""Speedup-based scoring for trusted Cheatsheet evaluation.

Candidate time is paired with frozen-starter time from the same outer run.
Each size is calibrated from a configured zero-score boundary and the organizer's
best reference implementation. Absolute timings never enter the score.
"""

from __future__ import annotations

import math
from typing import Dict, Mapping, Optional, Sequence

from src.runner import submission_surface as surface_mod


_REQUIRED_OUTER_RUNS = 2


def _positive_finite(value: object) -> bool:
    return (
        isinstance(value, (int, float))
        and not isinstance(value, bool)
        and math.isfinite(float(value))
        and float(value) > 0.0
    )


def paired_speedup(candidate_ms: float, starter_ms: float) -> Optional[float]:
    """Return starter/candidate for valid positive timings, otherwise None."""
    if not _positive_finite(candidate_ms) or not _positive_finite(starter_ms):
        return None
    S = float(starter_ms) / float(candidate_ms)
    return S if _positive_finite(S) else None


def size_score(
    B: float,
    S: float,
    T: float,
) -> float:
    """Score speedup linearly between zero-score and best-reference anchors."""
    if not all(
        _positive_finite(value)
        for value in (B, S, T)
    ):
        return 0.0
    B, S, T = float(B), float(S), float(T)
    if T <= B:
        return 0.0
    if S <= B:
        return 0.0
    if S >= T:
        return 1.0
    position = (S - B) / (T - B)
    return max(0.0, min(1.0, position))


def weighted_mean(
    values: Sequence[float],
    weights: Optional[Sequence[float]] = None,
) -> float:
    """Return a validated weighted arithmetic mean."""
    vals = [float(value) for value in values]
    if not vals:
        return 0.0
    if any(not math.isfinite(value) for value in vals):
        raise ValueError("values must be finite numbers")
    if weights is None:
        return sum(vals) / len(vals)
    ws = [float(weight) for weight in weights]
    if len(ws) != len(vals):
        raise ValueError("weights length must match values length")
    if any(not _positive_finite(weight) for weight in ws):
        raise ValueError("weights must be positive finite numbers")
    return sum(value * weight for value, weight in zip(vals, ws)) / sum(ws)


class RunResult:
    """One run's valid paired speedups; omit failed sizes, correct marks completeness."""

    def __init__(
        self,
        correct: bool,
        per_size_S: Optional[Mapping[str, float]] = None,
    ):
        self.correct = bool(correct)
        self.per_size_S = dict(per_size_S or {})


def _validated_tokens(
    B: Mapping[str, float],
    T: Mapping[str, float],
    weights: Optional[Mapping[str, float]],
) -> tuple[tuple[str, ...], list[float]]:
    tokens = tuple(B)
    if not tokens or set(tokens) != set(T):
        raise ValueError("B and T anchors must have exact keys")
    for token in tokens:
        baseline = B[token]
        target = T[token]
        if not (
            _positive_finite(baseline)
            and _positive_finite(target)
            and float(target) > float(baseline)
        ):
            raise ValueError("speedup anchors must satisfy 0 < B < T")
    if weights is None:
        return tokens, [1.0 for _token in tokens]
    if set(weights) != set(tokens):
        raise ValueError("weights must have exactly the scored size tokens")
    size_weights = [float(weights[token]) for token in tokens]
    if any(not _positive_finite(weight) for weight in size_weights):
        raise ValueError("weights must be positive finite numbers")
    return tokens, size_weights


def run_score(
    run: RunResult,
    B: Mapping[str, float],
    T: Mapping[str, float],
    weights: Optional[Mapping[str, float]] = None,
) -> Dict[str, object]:
    """Score one run, assigning zero only to failed or missing sizes."""
    tokens, size_weights = _validated_tokens(B, T, weights)
    speeds = run.per_size_S
    if not set(speeds).issubset(tokens):
        raise ValueError("run speeds contain unknown scored size tokens")
    if any(not _positive_finite(value) for value in speeds.values()):
        raise ValueError("run speeds must be positive finite numbers")
    per_size = {
        token: (
            size_score(B[token], speeds[token], T[token])
            if token in speeds
            else 0.0
        )
        for token in tokens
    }
    score = weighted_mean([per_size[token] for token in tokens], size_weights)
    return {
        "score": min(1.0, max(0.0, score)),
        "all_correct": run.correct and set(speeds) == set(tokens),
        "any_correct": bool(speeds),
        "correct_size_count": len(speeds),
        "per_size_scores": per_size,
    }


def stability_fold(
    run_results: Sequence[RunResult],
    B: Mapping[str, float],
    T: Mapping[str, float],
    weights: Optional[Mapping[str, float]] = None,
) -> Dict[str, object]:
    """Score two independent runs and keep the better run without size mixing."""
    runs = list(run_results)
    if len(runs) != _REQUIRED_OUTER_RUNS:
        raise ValueError("official scoring requires exactly two outer runs")
    per_run = [run_score(run, B, T, weights) for run in runs]
    correct_count = sum(bool(item["all_correct"]) for item in per_run)
    scored_count = sum(bool(item["any_correct"]) for item in per_run)
    selected_index = max(
        range(_REQUIRED_OUTER_RUNS),
        key=lambda index: (
            float(per_run[index]["score"]),
            int(per_run[index]["correct_size_count"]),
            -index,
        ),
    )
    selected = per_run[selected_index]
    selected_S = dict(runs[selected_index].per_size_S)
    return {
        "effective_score": float(selected["score"]),
        "correct_run_count": correct_count,
        "correctness_rate": correct_count / _REQUIRED_OUTER_RUNS,
        "scored_run_count": scored_count,
        "selected_run_index": selected_index + 1 if selected_S else None,
        "selected_S": selected_S,
        "selected": selected,
        "run_scores": [float(item["score"]) for item in per_run],
        "per_run": per_run,
    }


def length_adjustment(
    performance_score: float,
    estimated_tokens: int,
) -> Dict[str, object]:
    """Apply the canonical submission length policy to a performance score."""
    if (
        isinstance(performance_score, bool)
        or not isinstance(performance_score, (int, float))
        or not math.isfinite(float(performance_score))
    ):
        raise ValueError("performance score must be finite")
    performance = min(1.0, max(0.0, float(performance_score)))
    policy = surface_mod.length_policy(estimated_tokens)
    if policy.rejected:
        final = 0.0
    else:
        final = performance * policy.length_multiplier
    return {
        "valid": not policy.rejected,
        "estimated_tokens": policy.estimated_tokens,
        "soft_limit_tokens": policy.soft_limit,
        "hard_limit_tokens": policy.hard_limit,
        "remaining_tokens": policy.hard_limit_remaining,
        "length_multiplier": policy.length_multiplier,
        "penalized": policy.penalized,
        "final_score": min(1.0, max(0.0, final)),
    }
