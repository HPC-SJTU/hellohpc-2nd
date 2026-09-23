#!/usr/bin/env python3
"""Run the submission-selected public Cheatsheet task with one OpenCode agent."""

from __future__ import annotations

import argparse
import json
import sys
import tempfile
from pathlib import Path
from typing import Optional, Sequence

from src.runner import runtime_config
from src.runner import sandbox
from src.runner import submission_surface


REPO = Path(__file__).resolve().parent


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skill", default=".", help="SKILL.md or its containing directory")
    parser.add_argument("--output", help="new directory for the run artifacts")
    parser.add_argument("--cores", help="override the public diagnostic CPU set")
    return parser


def _write_summary(output: Path, record: object) -> None:
    sessions = list(getattr(record, "sessions", []))
    session = sessions[-1] if sessions else None
    lines = [
        "# Cheatsheet public debug report",
        "",
        f"- task: `{record.case_key}`",
        f"- model: `{record.model}`",
        f"- reasoning effort: `{record.reasoning_effort}`",
        f"- agent: `{record.agent_status}`",
        f"- skill: `{record.skill_status}`",
        f"- correct: `{record.best_correct}`",
        f"- improved: `{bool(session and session.improved)}`",
        f"- selected public time: `{record.best_time_ms}`",
        f"- tokens: `{record.tokens_used}`",
        f"- tool calls: `{record.tool_calls}`",
        "",
        "The selected kernel is in `workspace/kernel.cpp`; `kernel.diff` contains the change.",
    ]
    (output / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main(
    argv: Optional[Sequence[str]] = None,
) -> int:
    args = _parser().parse_args(argv)
    surface = submission_surface.require_eligible_surface(
        Path(args.skill), repository_layout=True
    )
    task = surface.operator
    output = Path(args.output or (REPO / "runs" / task)).resolve()
    if output.exists():
        raise FileExistsError(f"refusing to overwrite existing run: {output}")

    definition = sandbox.load_task_definition(
        task, REPO / "src", repo_root=REPO
    )
    owner = runtime_config.build_runner(
        runtime_config.load_config(), live_trace=True
    )
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".skill-", dir=output.parent) as temporary:
            frozen_skill = Path(temporary) / "submission"
            submission_surface.materialize_surface(surface, frozen_skill)
            record = owner.run(
                definition,
                frozen_skill,
                output.parent,
                cores=args.cores,
                run_id=output.name,
                root_override=output,
            )
    except Exception as exc:
        print(f"debug failed: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 4

    _write_summary(output, record)
    improved = bool(record.sessions and record.sessions[-1].improved)
    print(
        json.dumps(
            {
                "task": record.case_key,
                "model": record.model,
                "reasoning_effort": record.reasoning_effort,
                "correct": record.best_correct,
                "improved": improved,
                "output": str(output),
            },
            ensure_ascii=False,
        )
    )
    return 0 if record.best_correct else 1


if __name__ == "__main__":
    raise SystemExit(main())
