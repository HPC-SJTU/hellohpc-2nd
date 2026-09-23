"""Load the fixed two-operator task list and checked-in YAML specs."""

from __future__ import annotations

import hashlib
import os
import re
import stat
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Optional, Tuple

import yaml


REPO_ROOT = Path(__file__).resolve().parents[2]
PUBLIC_REGISTRY_PATH = REPO_ROOT / "src" / "runner" / "data" / "public_training.yaml"
EXPECTED_PUBLIC_TASKS = ("fft", "bitmatrix")
_SIZE_TOKEN_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.:+x-]{0,127}$")


class RegistryError(RuntimeError):
    pass


@dataclass(frozen=True)
class PublicTaskRegistry:
    path: Path
    task_ids: Tuple[str, ...]
    sha256: str


def _read_yaml_mapping_and_sha256(path: Path) -> tuple[Mapping[str, object], str]:
    try:
        mode = os.lstat(path).st_mode
        if stat.S_ISLNK(mode) or not stat.S_ISREG(mode):
            raise RegistryError("YAML input must be a regular non-symlink file")
        raw = path.read_bytes()
        value = yaml.safe_load(raw.decode("utf-8"))
    except RegistryError:
        raise
    except (OSError, UnicodeError, yaml.YAMLError) as exc:
        raise RegistryError(f"cannot read YAML input: {path}") from exc
    if not isinstance(value, dict) or any(not isinstance(key, str) for key in value):
        raise RegistryError("YAML root must be a string-keyed mapping")
    return value, hashlib.sha256(raw).hexdigest()


def load_public_registry(
    path: Optional[Path] = None,
    *,
    repo_root: Optional[Path] = None,
    require_task_dirs: bool = True,
) -> PublicTaskRegistry:
    registry_path = Path(path or PUBLIC_REGISTRY_PATH)
    data, digest = _read_yaml_mapping_and_sha256(registry_path)
    if set(data) != {"public_training_tasks"}:
        raise RegistryError("public registry fields are invalid")
    raw_tasks = data["public_training_tasks"]
    if not isinstance(raw_tasks, list) or tuple(raw_tasks) != EXPECTED_PUBLIC_TASKS:
        raise RegistryError("public task list must match the fixed two operators")
    root = Path(repo_root or REPO_ROOT).resolve()
    if require_task_dirs:
        for task in EXPECTED_PUBLIC_TASKS:
            task_dir = root / "src" / "tasks" / task
            if task_dir.is_symlink() or not task_dir.is_dir():
                raise RegistryError(f"task directory is missing or unsafe: {task}")
    return PublicTaskRegistry(registry_path.resolve(), EXPECTED_PUBLIC_TASKS, digest)
