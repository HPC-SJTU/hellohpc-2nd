"""Trusted evaluator paths and atomic public/evidence output helpers."""

from __future__ import annotations

import json
import math
import os
import stat
import tempfile
from pathlib import Path
from typing import Mapping, Optional

import yaml


REPO_ROOT = Path(__file__).resolve().parents[2]
TRUSTED_OUTPUT_ENV = "CHEATSHEET_TRUSTED_OUTPUT_ROOT"


def no_symlink_components(path: Path) -> bool:
    absolute = Path(os.path.abspath(str(path)))
    current = Path(absolute.anchor)
    for part in absolute.parts[1:]:
        current /= part
        try:
            mode = os.lstat(current).st_mode
        except FileNotFoundError:
            continue
        except OSError:
            return False
        if stat.S_ISLNK(mode):
            return False
    return True


def is_relative_to(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def resolve_trusted_output_root(
    environ: Optional[Mapping[str, str]] = None,
) -> Path:
    env = os.environ if environ is None else environ
    raw = env.get(TRUSTED_OUTPUT_ENV, "")
    lexical = Path(raw)
    if (
        not raw
        or "\\" in raw
        or not lexical.is_absolute()
        or ".." in lexical.parts
        or os.path.normpath(raw) != raw
        or not no_symlink_components(lexical)
    ):
        raise RuntimeError("trusted output root must be an absolute normalized path")
    try:
        mode = os.lstat(lexical).st_mode
    except OSError as exc:
        raise RuntimeError("trusted output root must already exist") from exc
    if stat.S_ISLNK(mode) or not stat.S_ISDIR(mode):
        raise RuntimeError("trusted output root must be a non-symlink directory")
    resolved = lexical.resolve(strict=True)
    if is_relative_to(resolved, REPO_ROOT.resolve()):
        raise RuntimeError("trusted output root must be outside the workspace")
    if next(resolved.iterdir(), None) is not None:
        raise RuntimeError("trusted output root must be empty")
    return resolved


def read_json_object(path: Path, label: str) -> Mapping[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"{label} is missing or invalid") from exc
    if not isinstance(value, dict):
        raise RuntimeError(f"{label} must be an object")
    return value


def relative_artifact_dir(root: Path, artifact: Path) -> str:
    if not artifact.is_absolute() or not artifact.is_dir() or artifact.is_symlink():
        raise RuntimeError("Runner artifact directory is invalid")
    resolved = artifact.resolve(strict=True)
    if not is_relative_to(resolved, root):
        raise RuntimeError("Runner artifact directory escapes trusted output")
    relative = resolved.relative_to(root)
    normalized = relative.as_posix()
    if not normalized or normalized.startswith("/") or ".." in relative.parts:
        raise RuntimeError("Runner artifact path is not normalized")
    return normalized


def public_summary(normalized_score: float) -> Mapping[str, object]:
    normalized = min(max(float(normalized_score), 0.0), 1.0)
    if not math.isfinite(normalized):
        raise RuntimeError("public score is not finite")
    return {
        "status": "success",
        "normalized_score": normalized,
    }


def hellohpc_step_payload(normalized_score: float) -> Mapping[str, object]:
    normalized = min(max(float(normalized_score), 0.0), 1.0)
    if not math.isfinite(normalized):
        raise RuntimeError("public score is not finite")
    return {
        "outputs": {
            "status": "success",
            "normalized_score": normalized,
            "score_input": 2.0 / (1.0 + normalized),
        }
    }


def safe_output_path(path: Path) -> Path:
    value = Path(path)
    if not value.is_absolute():
        value = (Path.cwd() / value).absolute()
    if ".." in value.parts or not no_symlink_components(value):
        raise RuntimeError("output path is unsafe")
    if value.exists() and (value.is_symlink() or not value.is_file()):
        raise RuntimeError("output path must be a regular file")
    value.parent.mkdir(parents=True, exist_ok=True)
    if not no_symlink_components(value.parent):
        raise RuntimeError("output parent contains a symbolic link")
    return value


def atomic_write_json(path: Path, payload: Mapping[str, object]) -> None:
    destination = safe_output_path(path)
    temporary_name: Optional[str] = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=destination.parent,
            prefix="." + destination.name + ".",
            suffix=".tmp",
            delete=False,
        ) as stream:
            json.dump(payload, stream, ensure_ascii=False, indent=2, sort_keys=True)
            stream.write("\n")
            temporary_name = stream.name
        os.replace(temporary_name, destination)
    finally:
        if temporary_name and os.path.exists(temporary_name):
            os.unlink(temporary_name)


def atomic_write_yaml(path: Path, payload: Mapping[str, object]) -> None:
    destination = safe_output_path(path)
    temporary_name: Optional[str] = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=destination.parent,
            prefix="." + destination.name + ".",
            suffix=".tmp",
            delete=False,
        ) as stream:
            yaml.safe_dump(dict(payload), stream, sort_keys=False)
            temporary_name = stream.name
        os.replace(temporary_name, destination)
    finally:
        if temporary_name and os.path.exists(temporary_name):
            os.unlink(temporary_name)


def write_hellohpc_output(normalized_score: float) -> Path:
    raw = os.environ.get("HELLOHPC_OUTPUT", "")
    if not raw:
        raise RuntimeError("--hellohpc-output requires HELLOHPC_OUTPUT")
    path = safe_output_path(Path(raw))
    atomic_write_json(path, hellohpc_step_payload(normalized_score))
    return path
