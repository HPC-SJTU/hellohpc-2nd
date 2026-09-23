"""Large means must retain sparse perturbations across chunk/tile boundaries."""
import json
from pathlib import Path

import numpy as np
import pytest

from tools.correctness.generate_case import generate_case
from tools.reference.reference import ragged_softmax_moments

MANIFEST = Path(__file__).resolve().parents[1] / "tools/data/public_cases.json"


@pytest.mark.parametrize("n", [4097, 8193, 16383, 16384])
@pytest.mark.parametrize("position", [0, 7, 8, 4095, 4096, -1])
def test_sparse_variance_uses_mathematical_mean(n, position):
    spec = dict(seed=1500, S=1, N=n, D=32, epsilon=3e-6,
                length_distribution="uniform_long", score_distribution="equal",
                x_distribution="large_sparse_perturbation", perturbation_position=position)
    score, x, offsets, _ = generate_case(spec)
    assert np.count_nonzero(x[:, 0] != np.float16(60032)) == 1
    assert x[position % n, 0] == np.float16(60000)
    np.testing.assert_array_equal(x[:, 1], -x[:, 0])
    np.testing.assert_array_equal(score, 0)
    mean, rstd, lse = ragged_softmax_moments(score, x, offsets, spec["epsilon"])
    exact_mean = 60032 - 32 / n
    variance = 32.0 ** 2 * (1.0 / n) * (1.0 - 1.0 / n)
    expected = np.float32(1 / np.sqrt(variance + float(np.float32(spec["epsilon"]))))
    assert mean[0, 0] == np.float32(exact_mean)
    np.testing.assert_allclose(rstd, expected, rtol=1e-6)
    np.testing.assert_allclose(lse, np.log(n), rtol=1e-6)


def test_regressions_are_correctness_only():
    cases = json.loads(MANIFEST.read_text())["cases"]
    regressions = [c for c in cases if c["x_distribution"] == "large_sparse_perturbation"]
    assert len(regressions) == 3
    assert all(c["correctness_only"] and c.get("weight", 0) == 0 for c in regressions)
