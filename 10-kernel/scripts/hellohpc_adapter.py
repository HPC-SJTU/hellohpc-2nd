#!/usr/bin/env python3
"""Run the public checks and expose one simple development score."""

from __future__ import annotations

import argparse
import json
import math
import os
import subprocess
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Sequence


ROOT = Path(__file__).resolve().parents[1]
PUBLIC_RESULT = ROOT / "artifacts" / "public_benchmark.json"
INFRASTRUCTURE_MARKERS = (
    "cann ",
    "npu-smi is unavailable",
    "device is inaccessible",
    "acl resource",
    "no ascend device",
)


@dataclass(frozen=True)
class Evaluation:
    evaluation_category: str
    submission_eligible: bool
    performance: float
    performance_name: str
    phase: str
    reason: str

    def outputs(self) -> dict[str, object]:
        return asdict(self)


def _atomic_json(path: Path, document: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(document, stream, indent=2, sort_keys=True)
            stream.write("\n")
        os.replace(temporary, path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def _emit(outputs: dict[str, object]) -> None:
    output = os.environ.get("HELLOHPC_OUTPUT")
    if not output:
        raise RuntimeError("HELLOHPC_OUTPUT is not set")
    _atomic_json(Path(output), {"outputs": outputs})


def _run(command: Sequence[str]) -> tuple[int, str]:
    print("+ " + " ".join(command), flush=True)
    process = subprocess.Popen(
        list(command),
        cwd=ROOT,
        env={**os.environ, "PYTHONUNBUFFERED": "1"},
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
    )
    assert process.stdout is not None
    captured: list[str] = []
    for line in process.stdout:
        print(line, end="", flush=True)
        captured.append(line)
        if len(captured) > 400:
            del captured[:200]
    return process.wait(), "".join(captured)


def _failure(category: str, phase: str, reason: str) -> Evaluation:
    return Evaluation(category, False, 1.0, "G_ref_public", phase, reason)


def evaluate_public() -> Evaluation:
    for command, phase in (
        (["bash", "scripts/check.sh"], "compliance"),
        (["bash", "scripts/build.sh"], "build"),
        (["bash", "scripts/test.sh", "--robustness"], "correctness_and_robustness"),
        (["bash", "scripts/benchmark.sh"], "benchmark"),
    ):
        print(f"Phase: {phase} (evaluate limit 900s)", flush=True)
        code, output = _run(command)
        if code == 0:
            continue
        infrastructure = phase != "compliance" and any(
            marker in output.lower() for marker in INFRASTRUCTURE_MARKERS
        )
        return _failure(
            "infrastructure_error" if infrastructure else "invalid_submission",
            phase,
            "environment_unavailable" if infrastructure else f"{phase}_failed",
        )
    result = json.loads(PUBLIC_RESULT.read_text(encoding="utf-8"))
    performance = float(result["G_ref_public"])
    return Evaluation(
        "accepted",
        True,
        performance,
        "G_ref_public",
        "completed",
        "accepted",
    )


def _evaluate_command(_args: argparse.Namespace) -> int:
    try:
        evaluation = evaluate_public()
    except BaseException as error:
        evaluation = _failure(
            "infrastructure_error",
            "adapter",
            f"adapter_failure:{type(error).__name__}:{error}",
        )
    _emit(evaluation.outputs())
    try:
        _atomic_json(ROOT / "artifacts" / "hellohpc_summary.json", {
            key: value for key, value in evaluation.outputs().items()
            if key in {"evaluation_category", "submission_eligible", "performance", "performance_name"}
        })
    except OSError as error:
        print(f"Summary report unavailable: {error}", flush=True)
    print(json.dumps(evaluation.outputs(), indent=2, sort_keys=True))
    return 0


def _guard_command(args: argparse.Namespace) -> int:
    print(
        f"evaluation rejected: category={args.category} phase={args.phase} reason={args.reason}",
    )
    return 1


def _parse_bool(value: str) -> bool:
    normalized = value.strip().lower()
    if normalized in {"true", "1", "yes"}:
        return True
    if normalized in {"false", "0", "no"}:
        return False
    raise ValueError(f"invalid boolean value: {value}")


def _stage_command(args: argparse.Namespace) -> int:
    performance = float(args.performance)
    if not math.isfinite(performance) or performance <= 0:
        raise ValueError("performance must be positive")
    _emit({
        "score_value": 1.0 if _parse_bool(args.eligible) and args.profile == "official" else 2.0,
        "performance": performance,
    })
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    evaluate = commands.add_parser("evaluate")
    evaluate.add_argument("--profile", choices=("public",), default="public")
    evaluate.set_defaults(handler=_evaluate_command)
    guard = commands.add_parser("guard")
    guard.add_argument("--category", required=True)
    guard.add_argument("--phase", required=True)
    guard.add_argument("--reason", required=True)
    guard.set_defaults(handler=_guard_command)
    stage = commands.add_parser("stage")
    stage.add_argument("--eligible", required=True)
    stage.add_argument("--performance", required=True)
    stage.add_argument("--profile", choices=("official", "public"), required=True)
    stage.set_defaults(handler=_stage_command)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return int(args.handler(args))


if __name__ == "__main__":
    raise SystemExit(main())
