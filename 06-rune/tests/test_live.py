from __future__ import annotations

import asyncio
from fractions import Fraction

import pytest
from rich.console import Console

from run_live import build_parser
from rune_scheduler.api.v1 import (
    ActiveAttempt,
    AttemptPhase,
    AttemptRef,
    DurationMultiplier,
    Observation,
    PublicScenarioModel,
    Ratio,
    SafetyLimits,
    WorkerSpec,
    WorkerState,
)
from rune_scheduler.live import ScenarioLiveView
from rune_scheduler.matrix import profile_name_from_path
from rune_scheduler.seeds import scenario_seed


def model() -> PublicScenarioModel:
    return PublicScenarioModel(
        protocol="rune-http-v1",
        ruleset="rune-deterministic-v2",
        profile="profile-01",
        profile_digest="digest",
        task_types=(),
        initial_tasks=(),
        releases=(),
        workflow_templates=(),
        derivations=(),
        workers=(
            WorkerSpec(
                "w",
                4,
                16,
                (),
                (DurationMultiplier("A", Ratio(2, 1)),),
                None,
            ),
        ),
        safety_limits=SafetyLimits(100, 10, 96, 20, 100),
    )


def state(time: int = 0) -> Observation:
    attempt = ActiveAttempt(
        AttemptRef("task", 1),
        "A",
        None,
        None,
        None,
        None,
        "w",
        AttemptPhase.RUNNING,
        0,
        0,
        3,
        0,
        0,
        None,
    )
    return Observation(
        decision_id=1,
        logical_time=time,
        ready_tasks=(),
        active_attempts=(attempt,),
        setup_groups=(),
        workers=(WorkerState("w", True, 8, Ratio(1, 2), 2, (attempt.attempt,), ()),),
        recent_events=(),
        generated_task_count=1,
        completed_task_count=0,
        failed_attempt_count=0,
        interrupted_attempt_count=0,
        worker_outage_count=0,
    )


def test_seed_key_and_ordinal_match_evaluation_derivation() -> None:
    assert scenario_seed("00" * 32, "profile-01", 0) == (
        "94ba21ae3a07f7f7883ccba04103e4796e3284b1158a838a7b785a3c7859cbe0"
    )


def test_profile_path_uses_toml_profile_identity(tmp_path) -> None:
    profile_path = tmp_path / "custom-name.toml"
    profile_path.write_text('profile = "custom-profile"\n')
    assert profile_name_from_path(profile_path) == "custom-profile"


def test_profile_path_requires_profile_field(tmp_path) -> None:
    profile_path = tmp_path / "custom-name.toml"
    profile_path.write_text("initial_tasks = []\n")
    with pytest.raises(ValueError, match="profile TOML"):
        profile_name_from_path(profile_path)


def test_interpolation_uses_public_factor_and_duration_multiplier() -> None:
    live = ScenarioLiveView(model(), console=Console(), terminal_hold=0)
    live._interpolate(state(), Fraction(8))
    assert live._display_service[AttemptRef("task", 1)] == 5


def test_positive_rate_caps_wait_and_restores_authoritative_service() -> None:
    sleeps: list[float] = []

    async def sleep(delay: float) -> None:
        sleeps.append(delay)

    live = ScenarioLiveView(
        model(),
        console=Console(file=None, force_terminal=False),
        display_rate=1,
        sleep=sleep,
        terminal_hold=0,
    )

    async def exercise() -> None:
        await live.start(state())
        try:
            await live.transition(state(), state(100))
        finally:
            await live.close()

    asyncio.run(exercise())
    assert sleeps == [0.1] * 20
    assert live._display_service == {}


def test_live_render_is_bounded_and_does_not_claim_completion_percentage() -> None:
    current = state()
    attempts = tuple(
        ActiveAttempt(
            AttemptRef(f"task-{index}", 1),
            "A",
            None,
            None,
            None,
            None,
            "w",
            AttemptPhase.RUNNING,
            0,
            0,
            index,
            0,
            0,
            None,
        )
        for index in range(5)
    )
    current = Observation(
        decision_id=current.decision_id,
        logical_time=current.logical_time,
        ready_tasks=(),
        active_attempts=attempts,
        setup_groups=(),
        workers=current.workers,
        recent_events=(),
        generated_task_count=5,
        completed_task_count=0,
        failed_attempt_count=0,
        interrupted_attempt_count=0,
        worker_outage_count=0,
    )
    console = Console(record=True, width=140)
    console.print(ScenarioLiveView(model(), console=console).render(current))
    text = console.export_text()
    assert "+2 more" in text
    assert "completion" not in text.lower()
    assert "%" not in text


def test_live_cli_uses_seed_key_and_ordinal() -> None:
    args = build_parser().parse_args(
        [
            "--policy",
            "policy.py",
            "--profile",
            "profiles/profile-01.toml",
            "--seed-key",
            "00" * 32,
            "--seed-ordinal",
            "3",
        ]
    )
    assert args.profile.name == "profile-01.toml"
    assert args.seed_ordinal == 3
    assert args.display_rate == 20
    assert args.no_live is False


@pytest.mark.parametrize(
    ("option", "value"),
    (
        ("--seed-key", "xyz"),
        ("--seed-ordinal", "-1"),
        ("--seed-ordinal", str(2**32)),
        ("--display-rate", "-1"),
        ("--display-rate", "nan"),
        ("--display-rate", "inf"),
    ),
)
def test_live_cli_rejects_invalid_values(option: str, value: str) -> None:
    arguments = [
        "--policy",
        "policy.py",
        "--profile",
        "profiles/profile-01.toml",
        "--seed-key",
        "00" * 32,
        "--seed-ordinal",
        "3",
    ]
    if option in arguments:
        arguments[arguments.index(option) + 1] = value
    else:
        arguments.extend((option, value))
    with pytest.raises(SystemExit):
        build_parser().parse_args(arguments)


def test_live_cli_rejects_direct_seed() -> None:
    parser = build_parser()
    with pytest.raises(SystemExit):
        parser.parse_args(
            [
                "--policy",
                "policy.py",
                "--profile",
                "profiles/profile-01.toml",
                "--seed",
                "00" * 32,
            ]
        )
