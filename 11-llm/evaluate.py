#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import math
import os
import tempfile
from pathlib import Path
from typing import Sequence

import benchmark


SCORING_PROFILES = {
    "qwen-performance": {
        "max_score": 30.0,
        "p0_output_tps": 7.2,
        "pf_output_tps": 32.0,
        "math_accuracy_threshold": 1.0,
        "ttft_sla_seconds": None,
        "tpot_sla_milliseconds": None,
    },
}
DEFAULT_REQUEST_TIMEOUTS = {"qwen-performance": 300.0}
DEFAULT_READINESS_TIMEOUT = 600.0


def _write_json(path: Path, value: dict[str, object]) -> None:
    data = (json.dumps(value, ensure_ascii=False, allow_nan=False, indent=2) + "\n").encode()
    path = path.resolve()
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def zero_score(profile_name: str, reason: str) -> dict[str, object]:
    profile = SCORING_PROFILES[profile_name]
    return {
        "score": 0.0,
        "max_score": profile["max_score"],
        "normalized_progress": 0.0,
        "score_coordinate": 2.0,
        "passed_gates": False,
        "zero_reason": reason,
        "p0_output_tps": profile["p0_output_tps"],
        "pf_output_tps": profile["pf_output_tps"],
        "ttft_sla_seconds": profile["ttft_sla_seconds"],
        "tpot_sla_milliseconds": profile["tpot_sla_milliseconds"],
    }


def summarize_partial_result(partial_result: dict[str, object]) -> dict[str, object] | None:
    formal = partial_result.get("formal")
    if not isinstance(formal, dict) or not formal.get("requests"):
        return None
    summary = benchmark.summarize_formal_phase(formal)
    summary["partial"] = True
    return summary


def score_result(
    profile_name: str,
    result: dict[str, object],
    scoring_profile: dict[str, object] | None = None,
) -> dict[str, object]:
    profile = SCORING_PROFILES[profile_name] if scoring_profile is None else scoring_profile
    summary = result["summary"]
    if not isinstance(summary, dict):
        raise ValueError("benchmark summary is invalid")

    reason = None
    math_threshold = profile["math_accuracy_threshold"]
    if math_threshold is not None:
        required_math = int(benchmark.CLI_PROFILES[profile_name]["formal_math_requests"])
        gate = summary.get("correctness_gate")
        if (
            not isinstance(gate, dict)
            or gate.get("policy") != "all-formal-math-probes-must-pass"
            or gate.get("required_math_requests") != required_math
            or gate.get("math_total") != required_math
            or gate.get("math_correct") != required_math
            or gate.get("passed") is not True
            or int(summary["math_total"]) != required_math
            or int(summary["math_correct"]) != required_math
            or float(summary["math_accuracy"]) != 1.0
        ):
            reason = "math-accuracy"

    ttft_sla = profile["ttft_sla_seconds"]
    tpot_sla = profile["tpot_sla_milliseconds"]
    if reason is None and ttft_sla is not None and tpot_sla is not None:
        formal = result.get("formal")
        if not isinstance(formal, dict) or not isinstance(formal.get("requests"), list):
            raise ValueError("benchmark formal requests are invalid")
        formal_requests = formal["requests"]
        if any(float(request["ttft"]) > 2 * ttft_sla or float(request["tpot"]) > 2 * tpot_sla for request in formal_requests):
            reason = "hard-sla"
        elif float(summary["p99_ttft"]) > ttft_sla or float(summary["p99_tpot"]) > tpot_sla:
            reason = "sla"

    p0_value = profile["p0_output_tps"]
    pf_value = profile["pf_output_tps"]
    if reason is None and (p0_value is None or pf_value is None):
        reason = "calibration-pending"
    p0 = 0.0 if p0_value is None else float(p0_value)
    pf = 1.0 if pf_value is None else float(pf_value)
    throughput = float(summary["output_throughput"])
    if not math.isfinite(throughput) or throughput < 0:
        raise ValueError("scoring values are invalid")
    if reason != "calibration-pending" and (
        not all(math.isfinite(value) for value in (p0, pf)) or p0 <= 0 or pf <= p0
    ):
        raise ValueError("scoring values are invalid")
    progress = 0.0 if reason is not None else min(max((throughput - p0) / (pf - p0), 0.0), 1.0)
    quality = progress**1.5
    if reason is not None:
        score = 0.0
        score_coordinate = 2.0
    elif profile_name == "qwen-performance":
        score = 20.0 + 10.0 * quality
        score_coordinate = 6.0 / (5.0 + quality)
    else:
        score = float(profile["max_score"]) * quality
        score_coordinate = 2.0 / (1.0 + quality)
    return {
        "score": score,
        "max_score": profile["max_score"],
        "normalized_progress": progress,
        "score_coordinate": score_coordinate,
        "passed_gates": reason is None,
        "zero_reason": reason,
        "p0_output_tps": profile["p0_output_tps"],
        "pf_output_tps": profile["pf_output_tps"],
        "ttft_sla_seconds": ttft_sla,
        "tpot_sla_milliseconds": tpot_sla,
    }


def evaluate(
    output: Path,
    *,
    profile_name: str,
    dataset: Path,
    base_url: str,
    model: str,
    readiness_timeout: float,
    request_timeout: float,
) -> dict[str, object]:
    benchmark_profile = benchmark.CLI_PROFILES[profile_name]
    try:
        result = benchmark.execute(
            output,
            dataset=dataset,
            case=profile_name,
            base_url=base_url,
            model=model,
            concurrency=benchmark_profile["concurrency"],
            max_tokens=benchmark_profile["max_tokens"],
            warmup_requests=benchmark_profile["warmup_requests"],
            formal_requests=benchmark_profile["formal_requests"],
            formal_math_requests=benchmark_profile["formal_math_requests"],
            readiness_timeout=readiness_timeout,
            request_timeout=request_timeout,
        )
    except benchmark.TrafficError as exc:
        partial_result = exc.partial_result
        result = {
            "schema_version": 3,
            "status": "failed",
            "case": profile_name,
            "model": model,
            "error": str(exc),
            "summary": summarize_partial_result(partial_result),
            "warmup": partial_result.get("warmup"),
            "formal": partial_result.get("formal"),
            "failed_phase": exc.failed_phase,
            "failed_submission_index": exc.failed_submission_index,
            "expected_formal_requests": exc.expected_formal_requests,
            "evaluation": zero_score(profile_name, "traffic-failure"),
        }
        _write_json(output, result)
        return result
    result["evaluation"] = score_result(profile_name, result)
    _write_json(output, result)
    return result


def main(argv: Sequence[str] | None = None) -> int:
    dispatch_argv = list(os.sys.argv[1:] if argv is None else argv)
    if dispatch_argv and dispatch_argv[0] == "dpsk-stage2":
        import dpsk_benchmark
        return dpsk_benchmark.main(dispatch_argv[1:])
    parser = argparse.ArgumentParser(description="Run one public serving profile and calculate its score")
    parser.add_argument("profile", choices=tuple(SCORING_PROFILES))
    parser.add_argument("--dataset", type=Path, default=Path("data/public-dataset.jsonl"))
    parser.add_argument(
        "--base-url",
        default=os.environ.get("HELLOHPC_SERVICE_BASE_URL", "http://127.0.0.1:8000"),
    )
    parser.add_argument("--model", default=os.environ.get("HELLOHPC_MODEL_ID"))
    parser.add_argument("--readiness-timeout", type=float, default=DEFAULT_READINESS_TIMEOUT)
    parser.add_argument("--request-timeout", type=float)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args(argv)
    if not arguments.model:
        parser.error("--model or HELLOHPC_MODEL_ID is required")
    output = arguments.output or Path(f"evaluation-{arguments.profile}-result.json")
    request_timeout = DEFAULT_REQUEST_TIMEOUTS[arguments.profile] if arguments.request_timeout is None else arguments.request_timeout
    if not math.isfinite(arguments.readiness_timeout) or arguments.readiness_timeout <= 0:
        parser.error("--readiness-timeout must be finite and positive")
    if not math.isfinite(request_timeout) or request_timeout <= 0:
        parser.error("--request-timeout must be finite and positive")
    try:
        result = evaluate(
            output,
            profile_name=arguments.profile,
            dataset=arguments.dataset,
            base_url=arguments.base_url,
            model=arguments.model,
            readiness_timeout=arguments.readiness_timeout,
            request_timeout=request_timeout,
        )
    except (benchmark.BenchmarkError, OSError, ValueError, KeyError, TypeError) as exc:
        print(f"evaluation failed: {exc}", file=os.sys.stderr)
        return 2
    summary = result["summary"]
    evaluation = result["evaluation"]
    print(
        json.dumps(
            {
                "output": str(output),
                "profile": arguments.profile,
                "score": evaluation["score"],
                "max_score": evaluation["max_score"],
                "zero_reason": evaluation["zero_reason"],
                "output_throughput": summary["output_throughput"] if isinstance(summary, dict) else None,
                "p99_ttft": summary["p99_ttft"] if isinstance(summary, dict) else None,
                "p99_tpot": summary["p99_tpot"] if isinstance(summary, dict) else None,
                "correctness_gate": summary.get("correctness_gate") if isinstance(summary, dict) else None,
            },
            ensure_ascii=False,
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
