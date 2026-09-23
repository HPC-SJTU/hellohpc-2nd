#!/usr/bin/env python3
"""Run complete device correctness suites and aggregate numerical evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import time
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.correctness.check_correctness import compare_case
from tools.correctness.generate_suite import generate_suite

from tools.reference.reference import PRECISION_CONTRACT


def _run_variant(
    root: Path,
    variant: str,
    suite: str,
    case_dir: Path,
    output_dir: Path,
    log_dir: Path,
    timeout_seconds: float,
) -> dict[str, Any]:
    metadata = json.loads((case_dir / "meta.json").read_text(encoding="utf-8"))
    epsilon = float(metadata.get("epsilon", 1.0e-5))
    output_dir.mkdir(parents=True, exist_ok=True)
    log_dir.mkdir(parents=True, exist_ok=True)
    command = [
        "bash",
        str(root / variant / "run.sh"),
        "--input-dir",
        str(case_dir),
        "--output-dir",
        str(output_dir),
        "--n",
        str(metadata["N"]),
        "--d",
        str(metadata["D"]),
        "--s",
        str(metadata["S"]),
        "--epsilon",
        str(epsilon),
    ]
    started = time.monotonic()
    try:
        completed = subprocess.run(
            command, cwd=root, text=True, capture_output=True, timeout=timeout_seconds
        )
    except subprocess.TimeoutExpired as error:
        def text(value):
            return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else (value or "")
        diagnostic = {"case": case_dir.name, "variant": variant, "command": command,
                      "elapsed_seconds": time.monotonic() - started, "timeout_seconds": timeout_seconds}
        try:
            (log_dir / f"correctness_{variant}_{suite}_{case_dir.name}.log").write_text(
                text(error.stdout) + text(error.stderr) + "\nTIMEOUT " + json.dumps(diagnostic) + "\n",
                encoding="utf-8",
            )
        except OSError as log_error:
            print(f"Timeout log unavailable: {log_error}", flush=True)
        print(f"Correctness timeout: {json.dumps(diagnostic)}", flush=True)
        return {
            "case_id": case_dir.name,
            "passed": False,
            "timed_out": True,
            "timeout_seconds": timeout_seconds,
        }
    (log_dir / f"correctness_{variant}_{suite}_{case_dir.name}.log").write_text(
        completed.stdout + completed.stderr, encoding="utf-8"
    )
    if completed.returncode != 0:
        return {
            "case_id": case_dir.name,
            "passed": False,
            "runner_exit_code": completed.returncode,
            "runner_error": completed.stderr[-4000:],
        }
    return compare_case(case_dir, output_dir, epsilon)


def run_suites(
    root: Path,
    manifests: list[Path],
    variants: list[str],
    generated_root: Path,
    timeout_seconds: float,
    case_ids: set[str] | None = None,
) -> dict[str, Any]:
    # Correctness evidence is scoped to the implementations actually executed.
    # Batch baseline and per-submission solution lifecycles must remain separate.
    evaluated_cases = []
    for manifest in manifests:
        suite = manifest.stem.removesuffix("_cases")
        for specification in json.loads(manifest.read_text(encoding="utf-8"))["cases"]:
            if case_ids and specification["case_id"] not in case_ids:
                continue
            evaluated_cases.append({**specification, "suite": suite})
    manifest_provenance = []
    for manifest in manifests:
        manifest_provenance.append(
            {
                "path": str(manifest),
                "sha256": hashlib.sha256(manifest.read_bytes()).hexdigest(),
            }
        )
    results: dict[str, Any] = {
        "schema_version": 2,
        "precision_contract": PRECISION_CONTRACT,
        "passed": True,
        "variants": {},
        "evaluated_cases": evaluated_cases,
        "manifest_provenance": manifest_provenance,
    }
    for manifest in manifests:
        suite = manifest.stem.removesuffix("_cases")
        generate_suite(manifest, generated_root / suite)

    for variant in variants:
        entries: list[dict[str, Any]] = []
        for manifest in manifests:
            suite = manifest.stem.removesuffix("_cases")
            specifications = [
                specification
                for specification in json.loads(manifest.read_text(encoding="utf-8"))["cases"]
                if not case_ids or specification["case_id"] in case_ids
            ]
            for specification in specifications:
                case_id = specification["case_id"]
                print(f"Correctness: {variant} {case_id}, limit {timeout_seconds:g}s", flush=True)
                result = _run_variant(
                    root,
                    variant,
                    suite,
                    generated_root / suite / case_id,
                    root / "artifacts" / "outputs" / variant / suite / case_id,
                    root / "artifacts" / "logs",
                    timeout_seconds,
                )
                result["suite"] = suite
                result["stress"] = bool(specification.get("stress", False))
                entries.append(result)
                results["passed"] = results["passed"] and bool(result["passed"])
                print(f"{variant:8s} {case_id:36s} {'PASS' if result['passed'] else 'FAIL'}")
        print(f"Correctness: {sum(bool(e['passed']) for e in entries)}/{len(entries)} passed ({variant})", flush=True)
        output_stats = {}
        for name in ("mean", "rstd", "logsumexp"):
            available = [entry["outputs"][name] for entry in entries if "outputs" in entry]
            if available:
                output_stats[name] = {
                    "max_abs_error": max(value["max_abs_error"] for value in available),
                    "max_rel_error": max(value["max_rel_error"] for value in available),
                    "max_p99_abs_error": max(value["p99_abs_error"] for value in available),
                }
            else:
                output_stats[name] = {
                    "max_abs_error": None,
                    "max_rel_error": None,
                    "max_p99_abs_error": None,
                }
        results["variants"][variant] = {
            "passed": all(bool(entry["passed"]) for entry in entries),
            "cases": entries,
            "summary": output_stats,
        }
    return results


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--manifest", type=Path, action="append", required=True)
    parser.add_argument("--variant", action="append", required=True)
    parser.add_argument("--generated-root", type=Path)
    parser.add_argument("--output-json", type=Path)
    parser.add_argument("--timeout-seconds", type=float, default=120.0)
    parser.add_argument(
        "--case-id",
        action="append",
        help="replay only this case ID (repeatable)",
    )
    args = parser.parse_args()
    root = args.root.resolve()
    generated_root = args.generated_root or root / "artifacts" / "generated"
    output_json = args.output_json or root / "artifacts" / (
        "correctness_replay.json" if args.case_id else "correctness_results.json"
    )
    results = run_suites(
        root, args.manifest, args.variant, generated_root, args.timeout_seconds,
        set(args.case_id) if args.case_id else None,
    )
    if args.case_id:
        found = {case["case_id"] for case in results["evaluated_cases"]}
        missing = set(args.case_id) - found
        if missing:
            parser.error(f"case ID not found in manifests: {', '.join(sorted(missing))}")
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(results, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    raise SystemExit(0 if results["passed"] else 1)


if __name__ == "__main__":
    main()
