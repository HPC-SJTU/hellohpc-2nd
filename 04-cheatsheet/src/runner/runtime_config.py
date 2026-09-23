"""Load the runtime configuration and construct one OpenCode Runner."""

from __future__ import annotations

import os
from pathlib import Path
from typing import Mapping

import yaml

from src.runner import opencode_agent
from src.runner import runner as runner_mod


REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = REPO_ROOT / "src"
MODEL_API_KEY_ENV = opencode_agent.MODEL_API_KEY_ENV


def load_config() -> dict:
    value = yaml.safe_load((REPO_ROOT / "config.yaml").read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise RuntimeError("runtime configuration is invalid")
    if value.get("pinning", {}).get("cores") == "auto":
        if not hasattr(os, "sched_getaffinity"):
            raise RuntimeError("CPU affinity is unavailable")
        allocated = sorted(os.sched_getaffinity(0))
        if not allocated:
            raise RuntimeError("no allocated CPU is available")
        value["pinning"]["cores"] = str(allocated[0])
    return value


def _required_absolute_file(env_key: str) -> Path:
    raw = os.environ.get(env_key, "")
    path = Path(raw).expanduser()
    if not raw or not path.is_absolute() or path.is_symlink() or not path.is_file():
        raise EnvironmentError(f"{env_key} must name an existing absolute regular file")
    return path.resolve(strict=True)


def build_runner(
    cfg: Mapping[str, object], *, live_trace: bool = False
) -> runner_mod.Runner:
    model = cfg["model"]
    budget = cfg["budget"]
    tools = cfg["tools"]
    compile_config = cfg["compile"]
    pinning = cfg["pinning"]
    scoring = cfg["scoring"]

    api_key_value = os.environ.pop(MODEL_API_KEY_ENV, None)
    if not api_key_value:
        raise EnvironmentError("required model API credential is not configured")
    opencode_bin = _required_absolute_file("CHEATSHEET_OPENCODE_BIN")
    ripgrep_bin = _required_absolute_file("CHEATSHEET_RIPGREP_BIN")
    canonical_config = (
        Path(opencode_agent.CANONICAL_OPENCODE_CONFIG_HOME)
        / "opencode"
        / "opencode.json"
    )
    opencode_agent.configured_build_model(str(canonical_config))
    round_timeout = float(budget.get("wall_clock_min", 20)) * 60.0
    https_proxy = os.environ.get("HTTPS_PROXY") or os.environ.get("https_proxy")

    return runner_mod.Runner(
        source_root=SOURCE_ROOT,
        api_key_value=api_key_value,
        model_base_url=os.environ.get("CHEATSHEET_MODEL_BASE_URL", str(model["base_url"])),
        https_proxy=https_proxy,
        opencode_bin=str(opencode_bin),
        ripgrep_bin=str(ripgrep_bin),
        cxx=compile_config["cxx"],
        cxxflags=compile_config["cxxflags"],
        san_flags=compile_config["san_flags"],
        vec_flags=compile_config["vec_flags"],
        cores=pinning["cores"],
        omp_threads=pinning["omp_num_threads"],
        omp_bind=pinning["omp_proc_bind"],
        omp_places=pinning["omp_places"],
        round_timeout=round_timeout,
        tool_timeout=float(tools["subprocess_timeout_seconds"]),
        bench_timeout_seconds=float(tools["bench_timeout_seconds"]),
        profile_timeout_seconds=float(tools["profile_timeout_seconds"]),
        variance_threshold_pct=float(scoring["variance_threshold_pct"]),
        tool_lock_path=os.environ.get("CHEATSHEET_TOOL_LOCK"),
        live_trace=live_trace,
    )
