"""Output-shape regression for the multi-round player benchmark.

Runs main() with the device-coupled steps mocked: no NPU is needed, and the
test asserts the produced artifact's field types (the multi-round rework
briefly wrote the latency fields as the strings "baseline"/"solution" by
unpacking a dict).
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.benchmark import player_benchmark as pb


CASES = [
    {"case_id": "public_perf_a", "public_normalized_weight": 0.6,
     "correctness_only": False, "anonymous_id": "public_perf_a"},
    {"case_id": "public_perf_b", "public_normalized_weight": 0.4,
     "correctness_only": False, "anonymous_id": "public_perf_b"},
]


@pytest.fixture
def environment(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    root = tmp_path / "repo"
    for relative in ("tools/data", "baseline", "solution"):
        (root / relative).mkdir(parents=True)
    manifest = root / "tools/data/public_cases.json"
    manifest.write_text(json.dumps({"schema_version": 1, "cases": []}), encoding="utf-8")
    monkeypatch.setattr(pb, "load_public_performance_cases", lambda root, manifest: [dict(c) for c in CASES])
    monkeypatch.setattr(pb, "generate_suite", lambda manifest, generated: None)
    monkeypatch.setattr(pb, "_toolchain_identity", lambda root: {"sha256": "t", "cann_home": "x", "cann_version": "v"})
    monkeypatch.setattr(pb, "build_if_changed", lambda root, variant, force, toolchain: None)

    def fake_run_case(root, variant, case, generated, output, raw, groups, target_ms):
        raw.parent.mkdir(parents=True, exist_ok=True)
        # Deterministic but order-sensitive: a single sequential pass shows the
        # drift the multi-round median is supposed to damp.
        base = {"baseline": 2.0, "solution": 1.0}[variant]
        raw.write_text(json.dumps({"median_ms": base}), encoding="utf-8")
        return {"median_ms": base}

    monkeypatch.setattr(pb, "run_case", fake_run_case)
    return root


def test_output_fields_and_types(environment: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    output = environment / "artifacts" / "result.json"
    monkeypatch.chdir(environment)
    monkeypatch.setattr(sys, "argv", [
        "player_benchmark.py", "--root", str(environment), "--rounds", "3",
        "--output-json", str(output),
    ])
    pb.main()
    result = json.loads(output.read_text(encoding="utf-8"))
    assert result["kind"] == "local-verification"
    assert result["runner_protocol_version"] == 6
    assert result["measurement"]["rounds"] == 3
    assert result["measurement"]["conditioning_target_ms"] == 500.0
    assert result["measurement"]["continuous_event_intervals"] is True
    assert "median" in result["measurement"]["aggregation"]
    for case in result["cases"]:
        assert isinstance(case["baseline_ms"], float)
        assert isinstance(case["solution_ms"], float)
        assert case["baseline_ms"] == pytest.approx(2.0)
        assert case["solution_ms"] == pytest.approx(1.0)
        assert len(case["round_speedups"]) == 3
        assert len(case["round_latencies"]) == 3
        for entry in case["round_latencies"]:
            assert isinstance(entry["baseline_ms"], float)
            assert isinstance(entry["solution_ms"], float)
            assert entry["round"] in (1, 2, 3)
    # raw files are per-round and never overwrite each other
    raws = list((environment / "artifacts" / "public_benchmark_raw").rglob("*.json"))
    assert len(raws) == len(CASES) * 2 * 3


def test_median_damps_single_pass_drift(environment: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    # Round 1 inflates the solution latency (drift); the median across rounds
    # must not follow the single inflated round.
    calls = {"n": 0}

    def fake_run_case(root, variant, case, generated, output, raw, groups, target_ms):
        calls["n"] += 1
        raw.parent.mkdir(parents=True, exist_ok=True)
        value = 2.0 if variant == "baseline" else (5.0 if calls["n"] <= 2 else 1.0)
        raw.write_text(json.dumps({"median_ms": value}), encoding="utf-8")
        return {"median_ms": value}

    monkeypatch.setattr(pb, "run_case", fake_run_case)
    output = environment / "artifacts" / "result.json"
    monkeypatch.chdir(environment)
    monkeypatch.setattr(sys, "argv", [
        "player_benchmark.py", "--root", str(environment), "--rounds", "3",
        "--output-json", str(output),
    ])
    pb.main()
    result = json.loads(output.read_text(encoding="utf-8"))
    first_case = result["cases"][0]
    # rounds: 0.4x (drifted), 2.0x, 2.0x -> median 2.0x, not the drifted 0.4x
    assert first_case["round_speedups"][0] == pytest.approx(0.4)
    assert first_case["speedup"] == pytest.approx(2.0)



def test_geometric_score_matches_cli(environment: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    import subprocess
    from tools.benchmark.scoring import calculate_public_score

    def fake_run_case(root, variant, case, generated, output, raw, groups, target_ms):
        value = (2.0 if case["case_id"] == "public_perf_a" else 8.0) if variant == "baseline" else 1.0
        return {"median_ms": value}

    monkeypatch.setattr(pb, "run_case", fake_run_case)
    output = environment / "artifacts" / "result.json"
    monkeypatch.chdir(environment)
    monkeypatch.setattr(sys, "argv", ["player_benchmark.py", "--root", str(environment), "--rounds", "3", "--output-json", str(output)])
    pb.main()
    result = json.loads(output.read_text())
    assert result["G_ref_public"] == pytest.approx(2**0.6 * 8**0.4)
    assert result["scoring_method"] == "weighted_geometric_speedup_v1"
    assert calculate_public_score(result["cases"])["G_ref_public"] == result["G_ref_public"]
    completed = subprocess.run([sys.executable, str(Path(pb.__file__).resolve().parents[2] / "scripts/score_public_benchmark.py"), str(output)], text=True, capture_output=True)
    assert completed.returncode == 0
    assert json.loads(completed.stdout[completed.stdout.index("{"):])["G_ref_public"] == result["G_ref_public"]


def test_reported_latency_ratio_is_used_when_round_ratios_differ(environment: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    calls = {}
    def fake_run_case(root, variant, case, generated, output, raw, groups, target_ms):
        key = (case["case_id"], variant)
        index = calls.get(key, 0)
        calls[key] = index + 1
        values = [1.0, 10.0, 100.0] if variant == "baseline" else [1.0, 2.0, 100.0]
        return {"median_ms": values[index]}
    monkeypatch.setattr(pb, "run_case", fake_run_case)
    output = environment / "artifacts" / "result.json"
    monkeypatch.chdir(environment)
    monkeypatch.setattr(sys, "argv", ["player_benchmark.py", "--root", str(environment), "--rounds", "3", "--output-json", str(output)])
    pb.main()
    result = json.loads(output.read_text())
    assert result["cases"][0]["speedup"] == pytest.approx(5.0)
    assert result["cases"][0]["round_speedups"] == [1.0, 5.0, 1.0]
    assert result["G_ref_public"] == pytest.approx(5.0)
