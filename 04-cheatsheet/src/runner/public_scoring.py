"""Load checked-in public or official score anchors from task specs."""

from __future__ import annotations

import hashlib
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Optional, Tuple

from src.runner import task_registry as registry_mod


RUNS_PER_SKILL = 2
SUITES = ("public", "official")


@dataclass(frozen=True)
class ScoreSize:
    token: str
    B: Optional[float]
    T: Optional[float]

    @property
    def complete(self) -> bool:
        return self.B is not None and self.T is not None


@dataclass(frozen=True)
class TaskScoreProfile:
    task_id: str
    threads: int
    sizes: Tuple[ScoreSize, ...]
    spec_path: Path


@dataclass(frozen=True)
class ScoreProfile:
    suite: str
    tasks: Tuple[TaskScoreProfile, ...]
    sha256: str


def _mapping(value: object, label: str) -> Mapping[str, object]:
    if not isinstance(value, dict) or any(not isinstance(key, str) for key in value):
        raise registry_mod.RegistryError(f"{label} must be a string-keyed mapping")
    return value


def _speedup(value: object, label: str) -> Optional[float]:
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise registry_mod.RegistryError(f"{label} must be numeric or null")
    result = float(value)
    if not math.isfinite(result) or result <= 0.0:
        raise registry_mod.RegistryError(f"{label} must be finite and positive")
    return result


def _task_profile(
    task_id: str,
    spec: Mapping[str, object],
    spec_path: Path,
    suite: str,
    require_complete: bool,
) -> TaskScoreProfile:
    base_fields = {"name", "threads", "sizes", "public_score"}
    allowed_fields = base_fields | {"official_score"}
    fields = set(spec)
    if not base_fields.issubset(fields) or not fields.issubset(allowed_fields):
        raise registry_mod.RegistryError(
            f"{task_id} spec fields are invalid: "
            f"expected {sorted(base_fields)} with optional official_score"
        )
    if spec.get("name") != task_id:
        raise registry_mod.RegistryError(f"{task_id} spec name does not match its task")
    threads = spec.get("threads")
    if isinstance(threads, bool) or not isinstance(threads, int) or threads < 1:
        raise registry_mod.RegistryError(f"{task_id} threads must be a positive integer")

    sizes = _mapping(spec.get("sizes"), f"{task_id} sizes")
    expected_suites = {"public", "official"} if "official_score" in spec else {"public"}
    if set(sizes) != expected_suites:
        raise registry_mod.RegistryError(
            f"{task_id} sizes must contain exactly {sorted(expected_suites)}"
        )
    raw_tokens = sizes.get(suite)
    if (
        not isinstance(raw_tokens, list)
        or not raw_tokens
        or any(
            not isinstance(token, str)
            or registry_mod._SIZE_TOKEN_RE.fullmatch(token) is None
            for token in raw_tokens
        )
    ):
        raise registry_mod.RegistryError(f"{task_id} sizes.{suite} is invalid")
    tokens = tuple(raw_tokens)
    if len(tokens) != len(set(tokens)):
        raise registry_mod.RegistryError(f"{task_id} sizes.{suite} must be unique")

    score_key = f"{suite}_score"
    scores = _mapping(spec.get(score_key), f"{task_id} {score_key}")
    if set(scores) != set(tokens):
        raise registry_mod.RegistryError(
            f"{task_id} {score_key} keys must exactly match sizes.{suite}"
        )

    parsed = []
    for token in tokens:
        item = _mapping(scores[token], f"{task_id} {score_key}[{token}]")
        if set(item) != {"B", "T"}:
            raise registry_mod.RegistryError(
                f"{task_id}/{token} must contain only B and T"
            )
        blank = _speedup(item["B"], f"{task_id}/{token} B")
        target = _speedup(item["T"], f"{task_id}/{token} T")
        if require_complete and (blank is None or target is None):
            raise registry_mod.RegistryError(
                f"{task_id}/{token} {suite} score anchors are not filled"
            )
        if blank is not None and target is not None and not target > blank:
            raise registry_mod.RegistryError(
                f"{task_id}/{token} anchors must satisfy 0 < B < T"
            )
        parsed.append(ScoreSize(token, blank, target))
    return TaskScoreProfile(task_id, threads, tuple(parsed), spec_path.resolve())


def load_score_profile(
    registry: registry_mod.PublicTaskRegistry,
    *,
    suite: str,
    operator: str,
    repo_root: Optional[Path] = None,
    require_complete: bool = True,
) -> ScoreProfile:
    if suite not in SUITES:
        raise ValueError(f"unsupported checked-in suite: {suite}")
    root = Path(repo_root or registry_mod.REPO_ROOT).resolve()
    digest = hashlib.sha256(suite.encode("ascii") + b"\0")
    if operator not in registry.task_ids:
        raise ValueError(f"unsupported operator: {operator}")
    path = root / "src" / "tasks" / operator / "spec.yaml"
    spec, spec_hash = registry_mod._read_yaml_mapping_and_sha256(path)
    profile = _task_profile(operator, spec, path, suite, require_complete)
    digest.update(operator.encode("ascii") + b"\0" + bytes.fromhex(spec_hash))
    return ScoreProfile(suite, (profile,), digest.hexdigest())
