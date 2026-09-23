"""Weighted geometric speedup for public paired timing artifacts."""
from __future__ import annotations

import math

SCORING_METHOD = "weighted_geometric_speedup_v1"


def calculate_public_score(cases: list[dict]) -> dict:
    speedups = []
    for case in cases:
        baseline, solution = float(case["baseline_ms"]), float(case["solution_ms"])
        if not all(math.isfinite(t) and t > 0 for t in (baseline, solution)):
            raise ValueError(f"invalid latency for {case['case_id']}")
        speedup = baseline / solution
        if not math.isfinite(speedup) or speedup <= 0:
            raise ValueError(f"invalid latency ratio for {case['case_id']}")
        case["speedup"] = speedup
        speedups.append(speedup)

    # The public performance set contains one case per workload family.  When
    # family labels are present, aggregate families equally; the fallback keeps
    # the helper usable by older unit fixtures that only provide case weights.
    if all("category" in case for case in cases):
        families = {}
        for case in cases:
            families.setdefault(str(case["category"]), []).append(case)
        if not families or any(not members for members in families.values()):
            raise ValueError("case families must be non-empty")
        family_logs = []
        for members in families.values():
            weights = [float(case["public_normalized_weight"]) for case in members]
            if not all(math.isfinite(w) and w > 0 for w in weights):
                raise ValueError("case weights must be finite and positive")
            total = math.fsum(weights)
            family_logs.append(math.fsum(
                (weight / total) * math.log(case["speedup"])
                for case, weight in zip(members, weights)
            ))
        aggregate_log = math.fsum(family_logs) / len(family_logs)
    else:
        weights = [float(case["public_normalized_weight"]) for case in cases]
        if not weights or not all(math.isfinite(w) and w > 0 for w in weights):
            raise ValueError("case weights must be non-empty, finite and positive")
        if not math.isclose(math.fsum(weights), 1.0, rel_tol=0.0, abs_tol=1e-9):
            raise ValueError("case weights must sum to 1")
        aggregate_log = math.fsum(
            weight * math.log(case["speedup"])
            for case, weight in zip(cases, weights)
        )
    return {
        "G_ref_public": math.exp(aggregate_log),
        "worst_case_speedup": min(speedups),
        "scoring_method": SCORING_METHOD,
    }
