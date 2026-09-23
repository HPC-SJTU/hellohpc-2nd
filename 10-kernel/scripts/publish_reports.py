#!/usr/bin/env python3
"""Publish only selected reports; never follow links in contestant-writable artifacts."""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import shutil
import stat

PUBLIC_REPORTS = (
    "public_correctness_results.json", "runtime_robustness_results.json",
    "public_benchmark.json", "public_benchmark_raw", "logs", "hellohpc_summary.json",
)
ROOT = Path(__file__).resolve().parents[1]


def _open(parent: int, name: str) -> int:
    # NONBLOCK prevents a raced-in FIFO from hanging before fstat rejects it.
    return os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=parent)


def _copy(parent: int, name: str, destination: Path) -> None:
    descriptor = _open(parent, name)
    try:
        mode = os.fstat(descriptor).st_mode
        if stat.S_ISDIR(mode):
            destination.mkdir()
            with os.scandir(descriptor) as entries:
                for entry in entries:
                    _copy(descriptor, entry.name, destination / entry.name)
        elif stat.S_ISREG(mode):
            with os.fdopen(os.dup(descriptor), "rb") as source, destination.open("xb") as target:
                shutil.copyfileobj(source, target, length=1024 * 1024)
        else:
            raise ValueError(f"non-regular report rejected: {name}")
    finally:
        os.close(descriptor)


def _summary(parent: int) -> dict:
    fallback = {"evaluation_category": "infrastructure_error", "submission_eligible": False,
                "performance": 1.0, "performance_name": "G"}
    try:
        descriptor = _open(parent, "hellohpc_summary.json")
    except FileNotFoundError:
        return fallback
    try:
        if not stat.S_ISREG(os.fstat(descriptor).st_mode):
            raise ValueError("summary must be a regular file")
        with os.fdopen(os.dup(descriptor), "r") as stream:
            data = json.loads(stream.read(4096))
        category = data.get("evaluation_category")
        if category not in {"accepted", "invalid_submission", "infrastructure_error"}:
            return fallback
        performance = float(data["performance"])
        if not math.isfinite(performance) or performance <= 0:
            return fallback
        # Never export arbitrary strings, commands, paths, shapes or case IDs.
        return {"evaluation_category": category,
                "submission_eligible": category == "accepted" and data.get("submission_eligible") is True,
                "performance": performance, "performance_name": "G"}
    except (ValueError, KeyError, TypeError, AttributeError):
        return fallback
    finally:
        os.close(descriptor)


def publish(root: Path, destination: Path, profile: str) -> None:
    root_fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        artifacts_fd = os.open("artifacts", os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=root_fd)
    finally:
        os.close(root_fd)
    try:
        if profile == "official":
            with (destination / "evaluation_summary.json").open("x") as stream:
                json.dump(_summary(artifacts_fd), stream, indent=2)
                stream.write("\n")
        else:
            for name in PUBLIC_REPORTS:
                try:
                    os.stat(name, dir_fd=artifacts_fd, follow_symlinks=False)
                except FileNotFoundError:
                    continue
                _copy(artifacts_fd, name, destination / name)
    finally:
        os.close(artifacts_fd)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", choices=("public", "official"), required=True)
    args = parser.parse_args()
    destination = os.environ.get("HELLOHPC_ARTIFACTS")
    if not destination:
        parser.error("HELLOHPC_ARTIFACTS is missing; use the CLI with artifact export support")
    # Artifacts may be absent when evaluation never started.
    (ROOT / "artifacts").mkdir(exist_ok=True)
    publish(ROOT, Path(destination), args.profile)


if __name__ == "__main__":
    main()
