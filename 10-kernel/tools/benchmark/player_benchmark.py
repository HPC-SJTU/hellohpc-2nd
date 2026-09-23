#!/usr/bin/env python3
"""Measure the contestant solution against the repository baseline."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import shlex
import time
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.benchmark.scoring import calculate_public_score
from tools.correctness.check_correctness import compare_case
from tools.correctness.generate_suite import generate_suite


FINGERPRINT_STORE = Path("artifacts") / "build_fingerprints.json"

# Source trees each variant's build depends on.  The shared runner engine and
# timing code live under tools/ and are part of both variants' builds.
_BUILD_INPUTS = {
    "baseline": ("baseline", "tools/common/runner", "tools/benchmark",
                 "baseline/build.sh"),
    "solution": ("solution", "tools/common/runner", "tools/benchmark",
                 "solution/build.sh"),
}

# Probe scripts/env.sh the way a real build would and hash the compilers it
# actually selects (CANN device toolchain plus the Host CC/CXX), so a CANN or
# host-toolchain switch invalidates cached builds.
_TOOLCHAIN_PROBE = r"""
source scripts/env.sh >/dev/null 2>&1 || exit 3
CCEC="$(command -v ccec_compiler 2>/dev/null || true)"
if [ -z "$CCEC" ] && [ -n "${ASCEND_HOME_PATH:-}" ]; then
  CCEC_CANDIDATE="${ASCEND_HOME_PATH}/aarch64-linux/ccec_compiler/bin/ccec_compiler"
  [ -x "$CCEC_CANDIDATE" ] && CCEC="$CCEC_CANDIDATE"
fi
printf '%s\0' \
  "${ASCEND_HOME_PATH:-}" "${CANN_VERSION:-}" "${RSM_TOOLCHAIN_ROOT:-}" \
  "$CCEC" \
  "$(command -v "${CC:-cc}" 2>/dev/null || true)" \
  "$(command -v "${CXX:-c++}" 2>/dev/null || true)"
"""


def _toolchain_identity(root: Path) -> dict | None:
    """Identity of the toolchain a build would actually use, or None.

    None means the environment could not be resolved (env.sh failed, the CANN
    device compiler or a Host compiler is missing); the caller must then
    refuse cache hits instead of silently caching a build of unknown
    provenance.
    """
    try:
        completed = subprocess.run(
            ["bash", "-c", _TOOLCHAIN_PROBE],
            cwd=root,
            text=True,
            capture_output=True,
            timeout=120,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    if completed.returncode != 0:
        return None
    values = completed.stdout.split("\0")
    if len(values) < 6:
        return None
    # Every compiler the build touches must resolve: the CANN device driver
    # (from PATH or the ASCEND_HOME_PATH fallback) and both Host compilers.
    if not all(values[3:6]):
        return None
    digest = hashlib.sha256()
    for value in values[:6]:
        digest.update(value.encode("utf-8"))
        digest.update(b"\0")
    for path_text in values[3:6]:
        try:
            digest.update(Path(path_text).read_bytes())
        except OSError:
            return None
    return {
        "sha256": digest.hexdigest(),
        "cann_home": values[0],
        "cann_version": values[1],
    }


def load_public_performance_cases(root: Path, manifest: Path) -> list[dict]:
    """Load public cases and normalize their published formal weights."""
    formal_weights = {
        str(row["anonymous_id"]): float(row["weight"])
        for row in json.loads(
            (root / "tools/data/scoring_weights.json").read_text(encoding="utf-8")
        )["cases"]
    }
    cases = [
        dict(case)
        for case in json.loads(manifest.read_text(encoding="utf-8"))["cases"]
        if not case.get("correctness_only", False)
        and str(case.get("anonymous_id", case["case_id"])) in formal_weights
    ]
    for case in cases:
        case["anonymous_id"] = str(case.get("anonymous_id", case["case_id"]))
    public_weight_sum = sum(formal_weights[case["anonymous_id"]] for case in cases)
    if not cases or public_weight_sum <= 0:
        raise ValueError("public manifest has no formally weighted performance cases")
    for case in cases:
        case["public_normalized_weight"] = (
            formal_weights[case["anonymous_id"]] / public_weight_sum
        )
    return cases


def _source_fingerprint(root: Path, variant: str) -> str:
    digest = hashlib.sha256()
    for relative in _BUILD_INPUTS[variant]:
        base = root / relative
        paths = [base] if base.is_file() else sorted(
            path for path in base.rglob("*") if path.is_file()
        )
        for path in paths:
            if "__pycache__" in path.parts:
                continue
            digest.update(str(path.relative_to(root)).encode("utf-8"))
            digest.update(path.read_bytes())
    return digest.hexdigest()


def _load_fingerprints(root: Path) -> dict:
    store = root / FINGERPRINT_STORE
    if store.is_file():
        return json.loads(store.read_text(encoding="utf-8"))
    return {}


def _runner_binary(root: Path, variant: str) -> Path:
    return root / "build" / f"{variant}-install" / "bin" / f"rsm_{variant}_runner"


def _kernel_library(root: Path, variant: str) -> Path:
    return (
        root / "build" / f"{variant}-install" / "lib64"
        / f"librsm_{variant}_kernels.so"
    )


def _installed_digest(root: Path, variant: str) -> str | None:
    """Combined digest of the runner AND the device kernel library.

    The Ascend C device code lives in the kernel library, not in the runner,
    so hashing the runner alone cannot detect a kernel-source change.  Either
    artifact missing means the identity is unknown.
    """
    runner = _runner_binary(root, variant)
    library = _kernel_library(root, variant)
    if not runner.is_file() or not library.is_file():
        return None
    digest = hashlib.sha256()
    digest.update(runner.read_bytes())
    digest.update(library.read_bytes())
    return digest.hexdigest()


def _binary_sha256(binary: Path) -> str | None:
    if not binary.is_file():
        return None
    digest = hashlib.sha256()
    with binary.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def build_if_changed(root: Path, variant: str, force: bool, toolchain: dict | None) -> None:
    """Rebuild a variant only when its installed binary no longer matches.

    Frequent tuning loops otherwise pay two full clean builds per test/bench
    cycle.  The cache records the source fingerprint, the resolved toolchain
    identity, AND the hash of the binary it produced, so the skip only fires
    when the currently installed binary is exactly the one built from the
    current sources with the current toolchain.  A build performed outside
    this entry point (scripts/build.sh, a branch switch) changes the
    installed binary without updating the cache, and the hash mismatch forces
    a rebuild — the cache can never pin a stale binary.  An unresolvable
    toolchain disables the cache entirely.
    """
    fingerprint = _source_fingerprint(root, variant)
    installed_hash = _installed_digest(root, variant)
    stored = _load_fingerprints(root).get(variant)
    matches = (
        toolchain is not None
        and isinstance(stored, dict)
        and stored.get("source_fingerprint") == fingerprint
        and stored.get("toolchain_sha256") == toolchain["sha256"]
        and stored.get("binary_sha256") is not None
        and stored.get("binary_sha256") == installed_hash
    )
    if not force and matches:
        print(f"{variant}: sources unchanged, reusing installed build")
        return
    subprocess.run(["bash", str(root / variant / "build.sh")], cwd=root, check=True, timeout=600)
    built_hash = _installed_digest(root, variant)
    if built_hash is None:
        raise RuntimeError(
            f"{variant}: build.sh finished without installing both the runner "
            f"and the device kernel library under {_runner_binary(root, variant).parent.parent}"
        )
    if toolchain is None:
        print(
            f"{variant}: toolchain identity unresolved; skipping the build cache for this run"
        )
        return
    store = root / FINGERPRINT_STORE
    fingerprints = _load_fingerprints(root)
    fingerprints[variant] = {
        "source_fingerprint": fingerprint,
        "toolchain_sha256": toolchain["sha256"],
        "binary_sha256": built_hash,
    }
    store.parent.mkdir(parents=True, exist_ok=True)
    store.write_text(json.dumps(fingerprints, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def run_case(root: Path, variant: str, case: dict, generated: Path, output: Path, raw: Path, groups: int, target_ms: float) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    raw.parent.mkdir(parents=True, exist_ok=True)
    command = ["bash", str(root / variant / "run.sh"), "--input-dir", str(generated), "--output-dir", str(output), "--benchmark-json", str(raw), "--n", str(case["N"]), "--d", str(case["D"]), "--s", str(case["S"]), "--epsilon", str(float(case.get("epsilon", 1e-5))), "--warmup", "20", "--groups", str(groups), "--target-ms", str(target_ms)]
    started = time.monotonic()
    try:
        subprocess.run(command, cwd=root, check=True, timeout=300)
    except subprocess.TimeoutExpired:
        diagnostic = {
            "phase": "benchmark", "case": case["case_id"], "variant": variant,
            "round": int(raw.stem.rpartition(".round")[2]) if raw.stem.rpartition(".round")[2].isdigit() else None,
            "elapsed_seconds": time.monotonic() - started, "timeout_seconds": 300,
            "command": command,
        }
        try:
            raw.with_suffix(".timeout.json").write_text(json.dumps(diagnostic, indent=2) + "\n")
        except OSError as log_error:
            print(f"Timeout log unavailable: {log_error}", flush=True)
        print(f"Benchmark timeout: {json.dumps(diagnostic)}; command={shlex.join(command)}", flush=True)
        raise
    timing = json.loads(raw.read_text(encoding="utf-8"))
    if not compare_case(generated, output, float(case.get("epsilon", 1e-5)))["passed"]:
        raise RuntimeError(f"correctness failed for {case['case_id']}")
    if (
        float(timing.get("conditioning_ms", 0.0)) < 500.0
        or int(timing.get("conditioning_repetitions", 0)) < 1
    ):
        raise RuntimeError(f"timing protocol v6 conditioning failed for {case['case_id']}")
    return timing


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, default=Path("tools/data/public_cases.json"))
    parser.add_argument("--output-json", type=Path, default=Path("artifacts/public_benchmark.json"))
    parser.add_argument("--no-build", action="store_true",
                        help="skip build steps entirely and use installed binaries")
    parser.add_argument("--force-build", action="store_true",
                        help="rebuild both variants even if sources are unchanged")
    parser.add_argument("--quick", action="store_true",
                        help="single short round for fast iteration; not comparable to formal numbers")
    parser.add_argument("--root", type=Path, default=None,
                        help="repository root override (defaults to the checkout containing this file)")
    parser.add_argument("--rounds", type=int, default=4,
                        help="measurement rounds per case; variant order alternates per round "
                        "and each variant latency is the median across rounds")
    args = parser.parse_args()
    root = args.root.resolve() if args.root else Path(__file__).resolve().parents[2]
    manifest = args.manifest if args.manifest.is_absolute() else root / args.manifest
    output_json = args.output_json if args.output_json.is_absolute() else root / args.output_json
    groups, target_ms = (2, 5.0) if args.quick else (5, 20.0)
    rounds = 1 if args.quick else max(1, args.rounds)
    generated = root / "artifacts" / "generated" / "public"
    generate_suite(manifest, generated)
    cases = load_public_performance_cases(root, manifest)
    if not args.no_build:
        toolchain = _toolchain_identity(root)
        build_if_changed(root, "baseline", args.force_build, toolchain)
        build_if_changed(root, "solution", args.force_build, toolchain)
    # Multiple rounds with alternating variant order damp the drift that a
    # single sequential pass shows on identical implementations (a same-code
    # A/B once measured ~13% apart); each variant latency is the median across
    # its rounds before their ratio is computed.
    speedup_rounds: dict[str, list[float]] = {}
    latency_rounds: dict[str, list[dict[str, float]]] = {}
    for round_index in range(rounds):
        ordered = cases if round_index % 2 == 0 else list(reversed(cases))
        for case_index, case in enumerate(ordered, 1):
            case_id = case["case_id"]
            variant_order = ("baseline", "solution") if round_index % 2 == 0 else ("solution", "baseline")
            latencies = {}
            for variant in variant_order:
                print(f"Benchmark: round {round_index + 1}/{rounds}, "
                      f"case {case_index}/{len(cases)} {case_id}, {variant}, limit 300s", flush=True)
                # Per-round raw files: identical names would overwrite the
                # previous round's raw timing record.
                raw = root / "artifacts/public_benchmark_raw" / variant / f"{case_id}.round{round_index + 1}.json"
                latencies[variant] = run_case(
                    root, variant, case, generated / case_id,
                    root / "artifacts/public_benchmark_outputs" / variant / case_id,
                    raw, groups, target_ms,
                )["median_ms"]
            ratio = latencies["baseline"] / latencies["solution"]
            speedup_rounds.setdefault(case_id, []).append(ratio)
            latency_rounds.setdefault(case_id, []).append({
                "round": round_index + 1,
                "baseline_ms": float(latencies["baseline"]),
                "solution_ms": float(latencies["solution"]),
                "speedup": float(ratio),
            })
            print(
                f"round={round_index + 1} {case_id:28s} "
                f"{latencies['baseline']:9.4f} ms {latencies['solution']:9.4f} ms {ratio:7.2f}x"
            )

    def _median(values: list[float]) -> float:
        ordered_values = sorted(values)
        mid = len(ordered_values) // 2
        return (
            ordered_values[mid] if len(ordered_values) % 2
            else (ordered_values[mid - 1] + ordered_values[mid]) / 2.0
        )

    rows = []
    for case in cases:
        case_id = case["case_id"]
        # Derive speedup from the reported per-variant median latencies, so
        # the score can be reproduced directly from the paired timing rows.
        baseline_ms = _median([r["baseline_ms"] for r in latency_rounds[case_id]])
        solution_ms = _median([r["solution_ms"] for r in latency_rounds[case_id]])
        median_speedup = baseline_ms / solution_ms
        row = {
            "case_id": case_id,
            "public_normalized_weight": float(case["public_normalized_weight"]),
            "baseline_ms": float(baseline_ms),
            "solution_ms": float(solution_ms),
            "speedup": float(median_speedup),
            "round_speedups": [float(s) for s in speedup_rounds[case_id]],
            "round_latencies": latency_rounds[case_id],
        }
        if "category" in case:
            row["category"] = str(case["category"])
        rows.append(row)
        print(f"{case_id:28s} median {median_speedup:7.2f}x  rounds={['%.2f' % s for s in speedup_rounds[case_id]]}")
    summary = calculate_public_score(rows)
    g_public = summary["G_ref_public"]
    worst = summary["worst_case_speedup"]
    result = {
        "kind": "local-verification",
        "runner_protocol_version": 6,
        "measurement": {
            "rounds": rounds,
            "groups": groups,
            "target_ms": target_ms,
            "conditioning_target_ms": 500.0,
            "continuous_event_intervals": True,
            "quick": bool(args.quick),
            "variant_order": "alternating",
            "aggregation": (
                "per-variant median across rounds for baseline_ms and solution_ms; "
                "speedup is baseline_ms / solution_ms; "
                "per-round values retained in round_latencies"
            ),
        },
        "cases": rows,
        **summary,
    }
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"G_ref_public: {g_public:.4f}x\nworst case:   {worst:.4f}x")


if __name__ == "__main__":
    main()
