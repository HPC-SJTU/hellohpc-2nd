from __future__ import annotations

import argparse
import asyncio
import json
import multiprocessing
import os
import re
from dataclasses import asdict
from pathlib import Path

from rune_scheduler.matrix import (
    ScenarioRequest,
    ScenarioResult,
    canonical_report,
    execute_scenario,
    load_policy_factory,
    profile_name_from_path,
    run_matrix,
)
from rune_scheduler.seeds import scenario_seed

PROFILE_NAMES = (
    "profile-01",
    "profile-02",
    "profile-03",
    "profile-04",
    "profile-05",
    "profile-06",
    "profile-07",
    "profile-08",
    "profile-09",
    "profile-10",
    "profile-11",
    "profile-12",
    "profile-13",
    "profile-14",
    "profile-15",
    "profile-16",
    "profile-17",
    "profile-18",
)


def _run_shard(
    args: argparse.Namespace, ordinals: list[int]
) -> list[dict[str, object]]:
    """Worker: run one seed slice in a subprocess, return raw result dicts."""
    root = Path(__file__).parent
    profile_path = Path(args.profile)
    profile = profile_name_from_path(profile_path)
    requests = tuple(
        ScenarioRequest(
            "scoring-contract-v6",
            profile,
            profile_path,
            scenario_seed(args.seed_key, profile, ordinal),
            ordinal,
        )
        for ordinal in ordinals
    )
    candidate = load_policy_factory("candidate", root / args.policy)

    async def execute(request: ScenarioRequest, policy):
        return await execute_scenario(
            request,
            policy,
            cluster_binary=root / "bin" / "rune-cluster",
            seed_set_kind="keyed",
            seed_set_version="keyed-v1",
        )

    results = asyncio.run(
        run_matrix(
            requests,
            candidate,
            execute=execute,
            concurrency=max(1, args.concurrency // args.shards),
            profile_order=PROFILE_NAMES,
        )
    )
    return [asdict(result) for result in results]


def run(args: argparse.Namespace) -> int:
    profile_path = Path(args.profile)
    if not profile_path.is_file():
        raise ValueError(f"profile not found: {profile_path}")
    slices = [
        list(range(args.seed_count)[i :: args.shards]) for i in range(args.shards)
    ]
    if args.shards == 1:
        payloads = [_run_shard(args, slices[0])]
    else:
        with multiprocessing.Pool(args.shards) as pool:
            payloads = pool.starmap(_run_shard, [(args, s) for s in slices])
    merged = [payload for shard_payloads in payloads for payload in shard_payloads]
    results = tuple(
        ScenarioResult(**payload)
        for payload in sorted(merged, key=lambda p: p["seed_ordinal"])
    )
    report = json.loads(canonical_report(results, profile_order=PROFILE_NAMES))
    valid = all(
        result.validity == "valid" and result.logical_makespan is not None
        for result in results
    )
    candidate_makespan = (
        sum(result.logical_makespan for result in results) / len(results)
        if valid
        else 0
    )
    output = Path(os.environ["HELLOHPC_OUTPUT"])
    output.write_text(
        json.dumps(
            {
                "outputs": {"report": report},
                "metrics": {
                    "candidate_makespan": {
                        "value": candidate_makespan,
                        "unit": "logical-time",
                    },
                    "scenario_valid": {
                        "value": int(valid),
                        "unit": "boolean",
                    },
                    "scoring_makespan": {
                        "value": candidate_makespan if valid else args.zero_time,
                        "unit": "logical-time",
                    },
                },
            },
            sort_keys=True,
        )
    )
    print(json.dumps(report, sort_keys=True, separators=(",", ":")))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--zero-time", type=float, required=True)
    parser.add_argument("--seed-key", required=True)
    parser.add_argument("--seed-count", type=int, required=True)
    parser.add_argument("--shards", type=int, default=1)
    parser.add_argument("--concurrency", type=int, default=16)
    args = parser.parse_args()
    if re.fullmatch(r"[0-9a-fA-F]{64}", args.seed_key) is None:
        parser.error("--seed-key must be exactly 64 hexadecimal characters")
    if not 1 <= args.seed_count <= 64:
        parser.error("--seed-count must be between 1 and 64")
    if not 1 <= args.shards <= 32:
        parser.error("--shards must be between 1 and 16")
    if not 1 <= args.concurrency <= 256:
        parser.error("--concurrency must be between 1 and 64")
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
