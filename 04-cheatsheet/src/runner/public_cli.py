"""HelloHPC public-test adapter; OJ keeps its existing evaluator and storage."""

from __future__ import annotations

import os
import shutil
import sys
import tarfile
import tempfile
from pathlib import Path
from typing import Optional, Sequence

from . import evaluator
from .evaluation_io import TRUSTED_OUTPUT_ENV, safe_output_path


# Export only diagnostic artifacts, never OpenCode's private homes/configuration
# or compiled programs. Official evaluation must never enter this export path.
_RUN_FILES = (
    "PROMPT.md", "FINALIZATION_PROMPT.md", "initial-kernel.cpp",
    "final-kernel.cpp", "kernel.diff", "metadata.json", "run-summary.json",
    "run-error.log", "agent-result.json", "events.jsonl", "timeline.log",
    "tool-calls.jsonl", "agent-stderr.log", "agent-errors.log",
    "workspace/kernel.cpp", "workspace/compile_options.txt",
)


def _export_records(root: Path, output: Path) -> Path:
    destination = safe_output_path(
        output.parent / "cheatsheet-runs" / (root.name + ".tar.gz")
    )
    # Exclusive creation protects earlier runs, including failed exports.
    descriptor = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "wb") as stream, tarfile.open(fileobj=stream, mode="w:gz") as archive:
        paths = [root / "suite-manifest.json", root / "suite-error.log"]
        for run in sorted((root / "agent-runs").glob("*")):
            if run.is_symlink() or not run.is_dir():
                raise RuntimeError("unsafe public run directory")
            paths.extend(run / name for name in _RUN_FILES)
            paths.extend((run / "trusted-tools").glob("*/*.stdout"))
            paths.extend((run / "trusted-tools").glob("*/*.stderr"))
        paths.extend((root / "agent-logs").glob("*.log"))
        for path in paths:
            # Missing records are normal after an early evaluation failure.
            if path.is_symlink() or any(parent.is_symlink() for parent in path.parents):
                raise RuntimeError("symlink in public diagnostic records")
            if path.exists():
                if not path.is_file():
                    raise RuntimeError("non-file public diagnostic record")
                archive.add(path, arcname=str(path.relative_to(root)), recursive=False)
    return destination


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = evaluator.build_parser().parse_args(argv)
    # External callers retain ownership and exact existing behavior, including
    # rejection of empty/invalid settings. Never auto-export official evidence.
    if TRUSTED_OUTPUT_ENV in os.environ or evaluator.EVALUATION_SUITE != "public":
        return evaluator.main(argv)

    root = Path(tempfile.mkdtemp(prefix="cheatsheet-public-", dir="/tmp")).resolve()
    os.environ[TRUSTED_OUTPUT_ENV] = str(root)
    print(f"Public evaluation records: {root}", file=sys.stderr, flush=True)
    try:
        status = evaluator.main(argv)
    except BaseException:
        # An interrupt can leave child processes alive. Preserve their files;
        # do not race an export or cleanup against a still-running worker.
        print(f"Evaluation interrupted; records retained at {root}", file=sys.stderr, flush=True)
        raise
    finally:
        os.environ.pop(TRUSTED_OUTPUT_ENV, None)

    try:
        archive = _export_records(root, Path(args.out))
    except (OSError, RuntimeError, tarfile.TarError):
        print(f"Record export failed; records retained at {root}", file=sys.stderr, flush=True)
        return status or 4
    print(f"Public evaluation archive: {archive}", file=sys.stderr, flush=True)
    try:
        shutil.rmtree(root)
    except OSError:
        print(f"Temporary records could not be removed: {root}", file=sys.stderr, flush=True)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
