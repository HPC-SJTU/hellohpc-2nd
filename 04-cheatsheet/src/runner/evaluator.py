#!/usr/bin/env python3
"""Command-line entry point for checked-in Cheatsheet evaluation."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Optional, Sequence

from src.runner import public_evaluator
from src.runner.evaluation_io import (
    atomic_write_json,
    atomic_write_yaml,
    evaluation_message,
    public_summary,
    resolve_trusted_output_root,
    write_hellohpc_output,
)
from src.runner.runtime_config import load_config


REPO = Path(__file__).resolve().parents[2]
EVALUATION_SUITE = "public"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--calibrate",
        action="store_true",
        help="measure paired speedups without requiring or applying score anchors",
    )
    parser.add_argument("--skill", required=True)
    parser.add_argument("--out", default=str(REPO / "result.yaml"))
    parser.add_argument("--hellohpc-output", action="store_true")
    return parser


def _calibration_summary(manifest: dict) -> dict:
    return {
        "status": "calibration",
        "suite": manifest["suite"],
        "cases": [
            {
                "task": case["task"],
                "correct_run_count": case["correct_run_count"],
                "averaged_S": case["averaged_S"],
            }
            for case in manifest["cases"]
        ],
    }


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    trusted_root: Optional[Path] = None
    mode = EVALUATION_SUITE
    try:
        if args.calibrate and args.hellohpc_output:
            raise ValueError("calibration does not produce a HelloHPC score")
        trusted_root = resolve_trusted_output_root()
        result = public_evaluator.run_suite(
            load_config(),
            Path(args.skill),
            trusted_root,
            suite=mode,
            calibrate=args.calibrate,
        )
        if result.normalized_score is None:
            summary = _calibration_summary(dict(result.manifest))
        else:
            summary = dict(public_summary(result.normalized_score))
            if args.hellohpc_output:
                write_hellohpc_output(
                    result.normalized_score, evaluation_message(result.manifest)
                )
        atomic_write_yaml(Path(args.out), summary)
        atomic_write_json(trusted_root / "suite-manifest.json", result.manifest)
    except (OSError, RuntimeError, ValueError) as exc:
        if trusted_root is not None:
            try:
                (trusted_root / "suite-error.log").write_text(
                    f"{type(exc).__name__}: {exc}\n", encoding="utf-8"
                )
            except OSError:
                pass
        print(f"{mode} evaluation failed", file=sys.stderr)
        return 4

    print(json.dumps(summary, ensure_ascii=False, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
