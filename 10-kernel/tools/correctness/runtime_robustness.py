#!/usr/bin/env python3
"""Exercise repeated launches, address changes, and dynamic shapes in one process."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.correctness.check_correctness import compare_case
from tools.reference.reference import PRECISION_CONTRACT, read_inputs, write_inputs


def _run_process(command, *, label="check", **kwargs):
    limit = kwargs.get("timeout")
    variant = Path(command[1]).parent.name
    print(f"Robustness: {variant} {label}, limit {limit}s", flush=True)
    started = time.monotonic()
    try:
        return subprocess.run(command, **kwargs)
    except subprocess.TimeoutExpired as error:
        def text(value):
            return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else (value or "")
        diagnostic = {"phase": "robustness", "check": label, "variant": variant, "command": command,
                      "elapsed_seconds": time.monotonic() - started, "timeout_seconds": limit,
                      "stdout": text(error.stdout), "stderr": text(error.stderr)}
        path = Path(kwargs["cwd"]) / "artifacts/logs/robustness_timeout.json"
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(diagnostic, indent=2) + "\n", encoding="utf-8")
        except OSError as log_error:
            print(f"Timeout log unavailable: {log_error}", flush=True)
        print(f"Robustness timeout: {json.dumps(diagnostic)}", flush=True)
        raise


def _runner_protocol_version(root: Path) -> str:
    """Hash the shared runner engine so stale installs are detectable."""
    engine = root / "tools" / "common" / "runner" / "runner_impl.h"
    return hashlib.sha256(engine.read_bytes()).hexdigest()[:16]


def _binary_fingerprints(root: Path, variants: list[str]) -> dict[str, str | None]:
    fingerprints: dict[str, str | None] = {}
    for variant in variants:
        binaries = sorted(
            (root / "build" / f"{variant}-install" / "bin").glob(f"rsm_{variant}_runner")
        )
        fingerprints[variant] = (
            hashlib.sha256(binaries[0].read_bytes()).hexdigest() if binaries else None
        )
    return fingerprints


def _execution_fingerprint(root: Path, variants: list[str]) -> str:
    digest = hashlib.sha256()
    paths = [root / name for name in (
        "tools/common/runner/runner_impl.h", "tools/reference/reference.py",
        "tools/correctness/check_correctness.py", "tools/data/precision_regressions.json",
    )]
    for variant in variants:
        paths.extend(sorted((root / variant).rglob("*")))
    for path in paths:
        if path.is_file():
            digest.update(str(path.relative_to(root)).encode("utf-8"))
            digest.update(path.read_bytes())
    return digest.hexdigest()


def _metadata(case_dir: Path) -> dict:
    return json.loads((case_dir / "meta.json").read_text(encoding="utf-8"))


def _shape(case_dir: Path) -> tuple[int, int, int]:
    metadata = _metadata(case_dir)
    return metadata["N"], metadata["D"], metadata["S"]


def run_variant(
    root: Path, variant: str, generated_root: Path, work: Path, timeout_seconds: float
) -> dict:
    base_same_shape = generated_root / "public" / "public_perf_medium"
    score, x, offsets, metadata = read_inputs(base_same_shape)
    mutated_score = score.copy()
    mutated_x = x.copy()
    mutated_score[: int(offsets[1])] = (
        mutated_score[: int(offsets[1])].astype(np.float32) + 1.0
    ).astype(np.float16)
    mutated_x[:, 0] = (mutated_x[:, 0].astype(np.float32) + 0.5).astype(np.float16)
    same_shape_mutation = work / "shared_inputs" / "same_shape_mutation"
    mutation_metadata = dict(metadata)
    mutation_metadata["case_id"] = "runtime_same_shape_mutation"
    write_inputs(same_shape_mutation, mutated_score, mutated_x, offsets, mutation_metadata)
    # Same N/D/S but different boundaries must rebuild any Host task plan.
    boundary_lengths = np.diff(offsets).copy()
    boundary_lengths = np.roll(boundary_lengths, 1)
    if np.array_equal(boundary_lengths, np.diff(offsets)):
        raise ValueError("runtime boundary probe requires unequal segment lengths")
    changed_offsets = np.concatenate(([0], np.cumsum(boundary_lengths))).astype(np.int32)
    boundary_case = work / "shared_inputs" / "changed_offsets"
    write_inputs(boundary_case, score, x, changed_offsets,
                 dict(metadata, case_id="runtime_changed_offsets"))
    epsilon_cases = []
    for epsilon in (3e-6, 1e-2):
        case = work / "shared_inputs" / f"epsilon_{epsilon}"
        # Constant columns make the output strongly depend on epsilon.
        constant_x = np.broadcast_to(x[0], x.shape).copy()
        write_inputs(case, score, constant_x, offsets,
                     dict(metadata, case_id=f"runtime_epsilon_{epsilon}", epsilon=epsilon))
        epsilon_cases.append(case)
    selected = [
        generated_root / "public" / "public_singletons",
        base_same_shape,
        same_shape_mutation,
        boundary_case,
        *epsilon_cases,
        base_same_shape,
        generated_root / "public" / "public_alignment",
        generated_root / "public" / "public_perf_small_s",
        generated_root / "public" / "public_dynamic",
        generated_root / "public" / "public_singletons",
    ]
    variant_work = work / variant
    variant_work.mkdir(parents=True, exist_ok=True)
    sequence = variant_work / "sequence.tsv"
    lines = ["# input_dir output_dir N D S epsilon"]
    outputs: list[tuple[Path, Path]] = []
    for index, case_dir in enumerate(selected):
        output_dir = variant_work / f"sequence_output_{index}"
        n, d, s = _shape(case_dir)
        epsilon = float(_metadata(case_dir).get("epsilon", 1.0e-5))
        lines.append(f"{case_dir} {output_dir} {n} {d} {s} {epsilon}")
        outputs.append((case_dir, output_dir))
    sequence.write_text("\n".join(lines) + "\n", encoding="utf-8")
    completed = _run_process(
        ["bash", str(root / variant / "run.sh"), "--sequence-file", str(sequence)],
        cwd=root,
        text=True,
        capture_output=True,
        timeout=timeout_seconds,
        label="same-process sequence",
    )
    (variant_work / "sequence.log").write_text(
        completed.stdout + completed.stderr, encoding="utf-8"
    )
    if completed.returncode != 0:
        raise RuntimeError(f"{variant} sequence failed: {completed.stderr[-2000:]}")
    sequence_results = [compare_case(case_dir, output_dir) for case_dir, output_dir in outputs]
    if not all(result["passed"] for result in sequence_results):
        raise AssertionError(f"{variant} same-process sequence produced an incorrect result")

    alternating_a = generated_root / "public" / "public_alignment"
    alternating_score, alternating_x, alternating_offsets, alternating_metadata = read_inputs(
        alternating_a
    )
    alternating_score_b = alternating_score.copy()
    alternating_x_b = alternating_x.copy()
    alternating_score_b[: int(alternating_offsets[1])] = (
        alternating_score_b[: int(alternating_offsets[1])].astype(np.float32) + 0.75
    ).astype(np.float16)
    alternating_x_b[:, 0] = (
        alternating_x_b[:, 0].astype(np.float32) - 0.25
    ).astype(np.float16)
    alternating_b = work / "shared_inputs" / "alternating_input_b"
    alternating_b_metadata = dict(alternating_metadata)
    alternating_b_metadata["case_id"] = "runtime_alternating_input_b"
    write_inputs(
        alternating_b,
        alternating_score_b,
        alternating_x_b,
        alternating_offsets,
        alternating_b_metadata,
    )

    reuse_output = variant_work / "alternating_state_output"
    n, d, s = _shape(alternating_a)
    alternating_started = time.monotonic()
    reused = _run_process(
        [
            "bash",
            str(root / variant / "run.sh"),
            "--input-dir",
            str(alternating_a),
            "--reuse-input-dir",
            str(alternating_b),
            "--output-dir",
            str(reuse_output),
            "--n",
            str(n),
            "--d",
            str(d),
            "--s",
            str(s),
        ],
        cwd=root,
        text=True,
        capture_output=True,
        timeout=timeout_seconds,
        label="alternating inputs",
    )
    alternating_process_elapsed = time.monotonic() - alternating_started
    (variant_work / "reused_state.log").write_text(
        reused.stdout + reused.stderr, encoding="utf-8"
    )
    if reused.returncode != 0:
        raise RuntimeError(f"{variant} reused-state run failed: {reused.stderr[-2000:]}")
    alternating_timing = json.loads(
        (reuse_output / "alternating_sequence" / "timing.json").read_text(
            encoding="utf-8"
        )
    )
    alternating_elapsed = float(alternating_timing["elapsed_seconds"])
    if alternating_elapsed >= 3.0:
        raise AssertionError(
            f"{variant} A->B->A->B gate took {alternating_elapsed:.6f}s "
            "and exceeded the 3s hard limit"
        )
    alternating_inputs = [alternating_a, alternating_b, alternating_a, alternating_b]
    alternating_results = [
        compare_case(
            input_dir,
            reuse_output / "alternating_sequence" / f"step_{step}",
        )
        for step, input_dir in enumerate(alternating_inputs)
    ]
    if not all(result["passed"] for result in alternating_results):
        raise AssertionError(f"{variant} A->B->A->B sequence produced an incorrect result")
    reused_result = compare_case(alternating_b, reuse_output)
    if not reused_result["passed"]:
        raise AssertionError(f"{variant} final alternating-state output was incorrect")

    repeat_input = generated_root / "public" / "public_perf_medium"
    repeat_output = variant_work / "repeated_output"
    n, d, s = _shape(repeat_input)
    repeat = _run_process(
        [
            "bash",
            str(root / variant / "run.sh"),
            "--input-dir",
            str(repeat_input),
            "--output-dir",
            str(repeat_output),
            "--n",
            str(n),
            "--d",
            str(d),
            "--s",
            str(s),
            "--epsilon",
            str(float(_metadata(repeat_input).get("epsilon", 1.0e-5))),
            "--robustness-runs",
            "4",
        ],
        cwd=root,
        text=True,
        capture_output=True,
        timeout=timeout_seconds,
        label="repeated addresses",
    )
    (variant_work / "repeated.log").write_text(repeat.stdout + repeat.stderr, encoding="utf-8")
    if repeat.returncode != 0:
        raise RuntimeError(f"{variant} repeated-address run failed: {repeat.stderr[-2000:]}")
    repeat_result = compare_case(repeat_input, repeat_output)
    if not repeat_result["passed"]:
        raise AssertionError(f"{variant} final repeated-address output was incorrect")

    initial_state_results = []
    for index, (output_fill, workspace_fill) in enumerate(
        ((0x00, 0x00), (0xA5, 0x3C), (0xFF, 0x5A))
    ):
        output_dir = variant_work / f"initial_state_output_{index}"
        completed = _run_process(
            [
                "bash",
                str(root / variant / "run.sh"),
                "--input-dir",
                str(repeat_input),
                "--output-dir",
                str(output_dir),
                "--n",
                str(n),
                "--d",
                str(d),
                "--s",
                str(s),
                "--epsilon",
                str(float(_metadata(repeat_input).get("epsilon", 1.0e-5))),
                "--output-fill-byte",
                str(output_fill),
                "--workspace-fill-byte",
                str(workspace_fill),
            ],
            cwd=root,
            text=True,
            capture_output=True,
            timeout=timeout_seconds,
            label="initial-state variation",
        )
        (variant_work / f"initial_state_{index}.log").write_text(
            completed.stdout + completed.stderr, encoding="utf-8"
        )
        if completed.returncode != 0:
            raise RuntimeError(
                f"{variant} initial-state run failed: {completed.stderr[-2000:]}"
            )
        result = compare_case(repeat_input, output_dir)
        if not result["passed"]:
            raise AssertionError(
                f"{variant} output/workspace initial-state variation was incorrect"
            )
        initial_state_results.append(result)
    return {
        "passed": True,
        "same_process_case_count": len(sequence_results),
        "same_shape_changed_input_runs": 4,
        "reused_output_workspace_state": True,
        "alternating_state_sequence": "A->B->A->B",
        "alternating_state_launches": 4,
        "alternating_state_elapsed_seconds": alternating_elapsed,
        "alternating_state_process_elapsed_seconds": alternating_process_elapsed,
        "alternating_state_target_under_one_second": alternating_elapsed < 1.0,
        "alternating_state_hard_limit_seconds": 3.0,
        "different_shapes": len({(_shape(path)) for path in selected}),
        "repeated_address_runs": 4,
        "initial_state_variations": len(initial_state_results),
        "sequence_cases": sequence_results,
        "reused_state_case": reused_result,
        "alternating_state_cases": alternating_results,
        "repeated_case": repeat_result,
        "initial_state_cases": initial_state_results,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--generated-root", type=Path)
    parser.add_argument("--variant", action="append", required=True)
    parser.add_argument("--work-dir", type=Path)
    parser.add_argument("--output-json", type=Path)
    parser.add_argument("--timeout-seconds", type=float, default=120.0)
    args = parser.parse_args()
    root = args.root.resolve()
    generated = args.generated_root or root / "artifacts" / "generated"
    work = args.work_dir or root / "artifacts" / "runtime_robustness"
    variants = sorted(set(args.variant))
    fingerprint_before = _execution_fingerprint(root, variants)
    result = {
        "schema_version": 2,
        "precision_contract": PRECISION_CONTRACT,
        "passed": True,
        "variants": {},
        "execution_fingerprint": fingerprint_before,
        "runner_protocol_version": _runner_protocol_version(root),
        "binary_fingerprints": _binary_fingerprints(root, variants),
    }
    for variant in args.variant:
        result["variants"][variant] = run_variant(
            root, variant, generated, work, args.timeout_seconds
        )
        print(f"{variant}: same-process and address-rotation checks passed")
    result["source_unchanged_during_run"] = (
        fingerprint_before == _execution_fingerprint(root, variants)
    )
    result["passed"] = result["passed"] and result["source_unchanged_during_run"]
    output = args.output_json or root / "artifacts" / "runtime_robustness_results.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
