from __future__ import annotations

import argparse
import asyncio
import json
import os
import re
import secrets
import sys
from collections.abc import Mapping
from pathlib import Path

from rich.console import Console

from rune_scheduler.client import ClusterClient
from rune_scheduler.live import ScenarioLiveView
from rune_scheduler.matrix import load_policy_factory, profile_name_from_path
from rune_scheduler.scheduler import PolicyViolation, Scheduler
from rune_scheduler.seeds import scenario_seed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run one deterministic Scenario with a live state view."
    )
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--seed-key", type=_seed_key, required=True)
    parser.add_argument("--seed-ordinal", type=_ordinal, required=True)
    parser.add_argument(
        "--cluster-binary",
        type=Path,
        default=None,
        help="Cluster executable; defaults to bin/rune-cluster.",
    )
    parser.add_argument(
        "--display-rate",
        type=_non_negative_rate,
        default=20.0,
        metavar="LU/S",
        help="Logical Time units per display second (default: 20).",
    )
    parser.add_argument(
        "--no-live",
        action="store_true",
        help="Disable live rendering while retaining single-Scenario execution.",
    )
    return parser


def _seed_key(value: str) -> str:
    if re.fullmatch(r"[0-9a-fA-F]{64}", value) is None:
        raise argparse.ArgumentTypeError("must be exactly 64 hexadecimal characters")
    return value


def _ordinal(value: str) -> int:
    try:
        ordinal = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be an integer") from error
    if not 0 <= ordinal < 2**32:
        raise argparse.ArgumentTypeError("must fit an unsigned 32-bit integer")
    return ordinal


def _non_negative_rate(value: str) -> float:
    try:
        rate = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be a number") from error
    if not 0 <= rate < float("inf"):
        raise argparse.ArgumentTypeError("must be a finite non-negative number")
    return rate


async def run(args: argparse.Namespace) -> int:
    root = Path(__file__).parent
    profile_path = args.profile
    if not profile_path.is_file():
        raise ValueError(f"profile not found: {profile_path}")
    profile = profile_name_from_path(profile_path)
    cluster_binary = args.cluster_binary or root / "bin" / "rune-cluster"
    if not cluster_binary.is_file():
        raise RuntimeError(f"cluster executable not found: {cluster_binary}")
    seed = scenario_seed(args.seed_key, profile, args.seed_ordinal)
    token = secrets.token_hex(32)
    process = await asyncio.create_subprocess_exec(
        cluster_binary,
        "--listen",
        "127.0.0.1:0",
        "--profile",
        profile_path,
        "--seed",
        seed,
        "--bearer-token",
        token,
        "--uds-socket",
        str(Path("/tmp") / f"rune-cluster-{seed[:12]}-{os.getpid()}.sock"),
        stdout=asyncio.subprocess.PIPE,
    )
    try:
        assert process.stdout is not None
        line = await asyncio.wait_for(process.stdout.readline(), timeout=10)
        handshake = json.loads(line)
        if not isinstance(handshake, Mapping):
            raise RuntimeError("cluster handshake must be an object")
        if handshake.get("protocol") != "rune-http-v1":
            raise RuntimeError("cluster protocol mismatch")
        address = handshake.get("address")
        if not isinstance(address, str):
            raise RuntimeError("cluster handshake has no address")
        uds = handshake.get("uds")
        uds_path = uds if isinstance(uds, str) else None
        client = ClusterClient(address, token, uds_path=uds_path)
        model = await client.model()
        if (
            model.profile != profile
            or model.profile_digest != handshake.get("profile_digest")
            or model.ruleset != handshake.get("ruleset")
        ):
            raise RuntimeError("cluster handshake mismatch")
        live = (
            ScenarioLiveView(
                model,
                console=Console(file=sys.stderr),
                display_rate=args.display_rate,
            )
            if not args.no_live
            else None
        )
        try:
            selected_policy = load_policy_factory("candidate", args.policy).factory(
                model
            )
        except Exception as error:
            raise PolicyViolation(
                f"Policy failed with {type(error).__name__}"
            ) from error
        scheduler = Scheduler(client, model, selected_policy, live)
        try:
            code = await scheduler.run()
        except PolicyViolation as error:
            print(f"policy violation: {error}", file=sys.stderr)
            return 1
        terminal = scheduler.terminal
        if terminal is None:
            raise RuntimeError("Scheduler ended without a Terminal Result")
        if terminal.logical_makespan is not None:
            print(
                f"completed profile={profile} ordinal={args.seed_ordinal} "
                f"logical_makespan={terminal.logical_makespan}"
            )
        else:
            print(f"scenario invalid: {terminal.reason}", file=sys.stderr)
        return code
    finally:
        if process.returncode is None:
            process.terminate()
            try:
                await asyncio.wait_for(process.wait(), timeout=5)
            except TimeoutError:
                process.kill()
                await process.wait()
        else:
            await process.wait()


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return asyncio.run(run(args))
    except PolicyViolation as error:
        print(f"policy violation: {error}", file=sys.stderr)
        return 1
    except (
        TimeoutError,
        OSError,
        RuntimeError,
        ValueError,
        json.JSONDecodeError,
    ) as error:
        detail = str(error) or type(error).__name__
        print(f"run_live: {detail}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
