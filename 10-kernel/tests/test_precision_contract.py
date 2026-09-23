"""Numerical contract tests use independent high-precision and faulty outputs."""
from decimal import Decimal, localcontext
import json
from pathlib import Path

import numpy as np
import pytest

from tools.correctness.check_correctness import compare_case
from tools.correctness.generate_case import generate_case
from tools.reference.reference import (
    PRECISION_CONTRACT, ragged_softmax_moments, reference_with_conditioning,
    write_inputs, write_outputs,
)

DATA = Path(__file__).resolve().parents[1] / "tools/data"
FIXTURES = json.loads((DATA / "precision_regressions.json").read_text())["cases"]


def inputs(fixture):
    score = np.asarray(fixture["score"], np.float16)
    x = np.repeat(np.asarray(fixture["x_column"], np.float16)[:, None], fixture["D"], axis=1)
    return score, x, np.array([0, len(score)], np.int32)


def compare(tmp_path, score, x, offsets, outputs, epsilon=1e-5):
    write_inputs(tmp_path / "input", score, x, offsets, {"epsilon": epsilon})
    write_outputs(tmp_path / "output", *outputs)
    return compare_case(tmp_path / "input", tmp_path / "output")


@pytest.mark.parametrize("fixture", FIXTURES, ids=lambda c: c["case_id"])
def test_oracle_agrees_with_decimal80(fixture):
    score, x, offsets = inputs(fixture)
    with localcontext() as ctx:
        ctx.prec = 80
        q = [Decimal.from_float(float(v)) for v in score]
        maximum = max(q)
        w = [(v - maximum).exp() for v in q]
        z = sum(w)
        values = [Decimal.from_float(float(v)) for v in x[:, 0]]
        mean = sum(a * b for a, b in zip(w, values)) / z
        var = sum(a * (b - mean) ** 2 for a, b in zip(w, values)) / z
        eps = Decimal.from_float(float(np.float32(fixture["epsilon"])))
        rstd = 1 / (var + eps).sqrt()
        lse = maximum + z.ln()
    actual = ragged_softmax_moments(score, x, offsets, fixture["epsilon"])
    for array, expected in zip(actual, (mean, rstd, lse)):
        # FP64 reduction need not decide every final FP32 midpoint identically
        # to Decimal; its remaining error is far below the judging budget.
        np.testing.assert_allclose(array, np.float32(str(expected)),
                                   rtol=2 * np.finfo(np.float32).eps, atol=1e-8)


@pytest.mark.parametrize("fixture", FIXTURES, ids=lambda c: c["case_id"])
@pytest.mark.parametrize("mode", ["numpy32", "rounded32", "perturbed_weights"])
def test_reasonable_exp_rounding_is_accepted(tmp_path, fixture, mode):
    score, x, offsets = inputs(fixture)
    shifted = score.astype(np.float64) - float(score.max())
    w = np.exp(shifted)
    if mode == "numpy32":
        w = np.exp(shifted.astype(np.float32)).astype(np.float64)
    elif mode == "rounded32":
        w = w.astype(np.float32).astype(np.float64)
    else:
        # Perturb weights in opposite directions, correlated with feature sign:
        # this stresses cancellation instead of cancelling the perturbations.
        w *= 1 + np.sign(x[:, 0]) * (2.0 ** -23)
    mu = np.sum(w[:, None] * x.astype(np.float64), axis=0) / w.sum()
    var = np.sum(w[:, None] * (x.astype(np.float64) - mu) ** 2, axis=0) / w.sum()
    out = (mu[None].astype(np.float32),
           (1 / np.sqrt(var + float(np.float32(fixture["epsilon"]))))[None].astype(np.float32),
           np.array([np.log(w.sum()) + float(score.max())], np.float32))
    result = compare(tmp_path, score, x, offsets, out, fixture["epsilon"])
    assert result["precision_contract"] == PRECISION_CONTRACT
    assert result["passed"], result


def test_scale_is_weighted_and_per_feature(tmp_path):
    score = np.array([-100, 0], np.float16)
    x = np.full((2, 32), .25, np.float16)
    x[0, 0] = 65504
    offsets = np.array([0, 2], np.int32)
    out, scale = reference_with_conditioning(score, x, offsets)
    assert scale[0, 0] < 1e-35
    np.testing.assert_array_equal(scale[0, 1:], 0)
    out[0][0, 1] += .01
    result = compare(tmp_path, score, x, offsets, out)
    assert not result["passed"]
    assert result["outputs"]["mean"]["failure_count"] == 1
    assert result["outputs"]["mean"]["max_allowed_error"] < .003001


def test_mean_scale_is_translation_invariant_and_not_submission_derived(tmp_path):
    score = np.zeros(2, np.float16)
    x = np.tile(np.array([[-2], [2]], np.float16), (1, 32))
    offsets = np.array([0, 2], np.int32)
    out, scale = reference_with_conditioning(score, x, offsets)
    _, shifted_scale = reference_with_conditioning(score, x + np.float16(1024), offsets)
    np.testing.assert_array_equal(scale, shifted_scale)
    out[0].fill(1e30)
    result = compare(tmp_path, score, x, offsets, out)
    assert not result["passed"]
    assert result["outputs"]["mean"]["max_allowed_error"] < .004


def test_large_mean_bias_is_rejected(tmp_path):
    score = np.zeros(4, np.float16)
    x = np.full((4, 32), 60032, np.float16)
    offsets = np.array([0, 4], np.int32)
    out = ragged_softmax_moments(score, x, offsets)
    out[0][...] += np.float32(.1)
    assert not compare(tmp_path, score, x, offsets, out)["outputs"]["mean"]["passed"]


@pytest.mark.parametrize("value", [0, -1e-5, np.nan, np.inf, -np.inf])
def test_invalid_rstd_is_rejected_even_for_full_scale_variance(tmp_path, value):
    score = np.zeros(2, np.float16)
    x = np.tile(np.array([[-65504], [65504]], np.float16), (1, 32))
    offsets = np.array([0, 2], np.int32)
    out = ragged_softmax_moments(score, x, offsets)
    assert out[1][0, 0] < .002  # The old absolute tolerance accepted zero here.
    out[1].fill(value)
    assert not compare(tmp_path, score, x, offsets, out)["passed"]


@pytest.mark.parametrize("bias,accepted", [(.0035, True), (.01, False)])
def test_rstd_relative_accuracy_across_scales(tmp_path, bias, accepted):
    score = np.zeros(2, np.float16)
    v = np.geomspace(2**-20, 65504, 32).astype(np.float16)
    x = np.stack([-v, v])
    offsets = np.array([0, 2], np.int32)
    out = ragged_softmax_moments(score, x, offsets)
    out[1][...] *= 1 + bias
    assert compare(tmp_path, score, x, offsets, out)["passed"] is accepted


def test_constant_false_variance_is_rejected(tmp_path):
    fixture = next(c for c in FIXTURES if c["case_id"] == "constant_requires_accurate_mean_16")
    score, x, offsets = inputs(fixture)
    out = ragged_softmax_moments(score, x, offsets, fixture["epsilon"])
    out[0].fill(-60447.9921875)
    out[1].fill(124.75)  # Measured no-refinement ablation, not a synthetic threshold.
    result = compare(tmp_path, score, x, offsets, out, fixture["epsilon"])
    assert not result["outputs"]["rstd"]["passed"]


def test_wrong_epsilon_and_unweighted_statistics_are_rejected(tmp_path):
    score = np.array([-5, 5], np.float16)
    x = np.tile(np.array([[-2], [4]], np.float16), (1, 32))
    offsets = np.array([0, 2], np.int32)
    unweighted = ragged_softmax_moments(np.zeros_like(score), x, offsets)
    assert not compare(tmp_path, score, x, offsets, unweighted)["passed"]
    x.fill(4)
    wrong_epsilon = ragged_softmax_moments(score, x, offsets, 1e-5)
    assert not compare(tmp_path, score, x, offsets, wrong_epsilon, 3e-6)["passed"]


def test_precision_fixtures_are_public_correctness_only_and_deterministic():
    specs = json.loads((DATA / "public_cases.json").read_text())["cases"]
    specs = [c for c in specs if "precision_fixture" in c]
    assert len(specs) == len(FIXTURES)
    for spec in specs:
        assert spec["correctness_only"] and spec.get("weight", 0) == 0
        first, second = generate_case(spec), generate_case(spec)
        for a, b in zip(first[:3], second[:3]):
            np.testing.assert_array_equal(a, b)
        assert np.isfinite(first[0]).all() and np.isfinite(first[1]).all()
        assert all(np.isfinite(a).all() for a in ragged_softmax_moments(*first[:3], spec["epsilon"]))


def test_large_score_does_not_hide_missing_log_normalizer(tmp_path):
    score = np.full(2, 60000, np.float16)
    x = np.zeros((2, 32), np.float16)
    offsets = np.array([0, 2], np.int32)
    out = ragged_softmax_moments(score, x, offsets)
    out[2].fill(60000)  # max(score), omitting log(2), passed the old 1e-4 rtol.
    result = compare(tmp_path, score, x, offsets, out)
    assert not result["outputs"]["logsumexp"]["passed"]
