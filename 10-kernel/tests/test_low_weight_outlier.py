"""Regression inputs for negligible-weight, full-scale outliers."""
import json
from pathlib import Path

import numpy as np
import pytest

from tools.correctness.generate_case import generate_case
from tools.reference.reference import ragged_softmax_moments

MANIFEST = Path(__file__).resolve().parents[1] / "tools/data/public_cases.json"


def specification(**overrides):
    return {
        "case_id": "regression", "seed": 1400, "S": 1, "N": 8, "D": 32,
        "epsilon": 3e-6, "length_distribution": "uniform_long",
        "score_distribution": "low_weight_outlier",
        "x_distribution": "low_weight_outlier", **overrides,
    }


@pytest.mark.parametrize("position", range(8))
@pytest.mark.parametrize("n", [8, 17, 4099])
def test_outlier_oracle_and_locations(position, n):
    spec = specification(N=n, outlier_position=position)
    score, x, offsets, _ = generate_case(spec)
    again = generate_case(spec)
    for actual, repeated in zip((score, x, offsets), again[:3]):
        np.testing.assert_array_equal(actual, repeated)
    signs = np.where(np.arange(32) % 2 == 0, 1.0, -1.0)
    for base in range(0, n, 8):
        rows = min(8, n - base)
        assert np.count_nonzero(score[base:base + rows] < 0) == (rows > 1)
        if rows > 1:
            index = base + position % rows
            assert score[index] == -100
            np.testing.assert_array_equal(x[index], signs * 65504)
    mean, rstd, lse = ragged_softmax_moments(score, x, offsets, spec["epsilon"])
    np.testing.assert_allclose(mean[0], -signs / 256, rtol=0, atol=1e-8)
    np.testing.assert_allclose(rstd, 1 / np.sqrt(np.float32(spec["epsilon"])), rtol=1e-6)
    np.testing.assert_allclose(lse, np.log(np.count_nonzero(score == 0)), rtol=1e-6)


def test_public_regressions_have_no_performance_weight():
    cases = json.loads(MANIFEST.read_text())["cases"]
    regressions = [c for c in cases if c["score_distribution"] == "low_weight_outlier"]
    assert len(regressions) == 3
    for spec in regressions:
        assert spec["correctness_only"] and spec["weight"] == 0
        score, x, offsets, _ = generate_case(spec)
        assert x.dtype == score.dtype == np.float16
        assert np.isfinite(x).all() and np.isfinite(score).all()
        assert max(np.diff(offsets)) <= 16384


def test_unpaired_distribution_is_rejected():
    with pytest.raises(ValueError, match="matching"):
        generate_case(specification(x_distribution="normal"))
