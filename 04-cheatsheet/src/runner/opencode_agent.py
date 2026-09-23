"""Thin wrapper around ``opencode run`` for one headless optimization round.

Each run receives a private ``XDG_CONFIG_HOME`` beside its workspace. The
repository-owned provider configuration is copied there before OpenCode starts,
so ambient user configuration cannot affect evaluation.

The submission selects a model from the pinned provider registry. Its medium
reasoning settings and iteration cap are organizer-owned. ``--format json`` emits a raw JSON event stream we parse for
token usage / tool-call counts. The agent itself reads/edits
workspace/kernel.cpp plus the validated compile_options.txt surface and may
call the shell tools; this
wrapper does NOT trust anything it says about timing — the Runner re-measures.

The event stream shape varies across opencode versions, so the parser is
defensive: it walks every JSON object it can extract (whole-doc, JSONL, or
concatenated objects) and pulls token/tool signals from whatever keys exist,
falling back to plain text if JSON parsing yields nothing.

run_round() kills the whole process group on timeout.
"""

from __future__ import annotations

import json
import math
import os
import re
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Callable, Dict, List, Mapping, Optional

try:
    from . import sandbox as _sandbox
except ImportError:  # allow running as a top-level module
    import sandbox as _sandbox  # type: ignore


OPENCODE_BIN_ENV = "CHEATSHEET_OPENCODE_BIN"
RIPGREP_BIN_ENV = "CHEATSHEET_RIPGREP_BIN"
MODEL_API_KEY_ENV = "SJTU_API_KEY"
VERSION_CHECK_TIMEOUT_SECONDS = 10.0
OPENCODE_VERSION_CHECK_MAX_ATTEMPTS = 2
OPENCODE_VERSION_CHECK_RETRY_DELAY_SECONDS = 0.25
PINNED_MODEL_REQUEST_TIMEOUT_MS = 900_000
_DIRECT_ENV_ALLOWLIST = (
    "PATH",
    "LANG",
    "LC_ALL",
    "TMPDIR",
    OPENCODE_BIN_ENV,
    RIPGREP_BIN_ENV,
    "CHEATSHEET_MODEL_BASE_URL",
    "HTTPS_PROXY",
    "https_proxy",
)

# Immutable repository-owned configuration template. Runtime state is installed
# into a private per-run config home; OpenCode never uses this directory as its
# writable XDG_CONFIG_HOME.
_RUNNER_DIR = os.path.dirname(os.path.abspath(__file__))
CANONICAL_OPENCODE_CONFIG_HOME = os.path.join(_RUNNER_DIR, "opencode_home")
FORBIDDEN_CONFIG_ENTRIES = (
    "config",
    "config.json",
    "opencode.jsonc",
    "AGENTS.md",
    "agent",
    "agents",
    "command",
    "commands",
    "mode",
    "modes",
    "plugin",
    "plugins",
    "skill",
    "skills",
    "tool",
    "tools",
)
def _find_opencode(env: Mapping[str, str], explicit: Optional[str] = None) -> str:
    """Resolve only the explicitly configured OpenCode binary.

    Falling back to ``PATH`` or ``~/.opencode`` would make the chosen binary
    depend on the account running the evaluator, so this intentionally fails
    closed when neither ``opencode_bin`` nor ``CHEATSHEET_OPENCODE_BIN`` is provided.
    """
    configured = explicit or env.get(OPENCODE_BIN_ENV, "")
    if not configured:
        raise FileNotFoundError(
            f"OpenCode binary is not configured; set {OPENCODE_BIN_ENV} to the "
            "absolute path of the organizer-managed opencode executable"
        )

    expanded = os.path.expanduser(configured)
    if not os.path.isabs(expanded):
        raise ValueError(
            f"{OPENCODE_BIN_ENV} must be an absolute path, got {configured!r}"
        )
    binpath = os.path.abspath(expanded)
    if not os.path.isfile(binpath):
        source = "opencode_bin" if explicit else OPENCODE_BIN_ENV
        raise FileNotFoundError(
            f"configured OpenCode binary does not exist or is not a file: "
            f"{binpath} (from {source})"
        )
    if not os.access(binpath, os.X_OK):
        raise PermissionError(f"configured OpenCode binary is not executable: {binpath}")
    return binpath


def _find_ripgrep(env: Mapping[str, str], explicit: Optional[str] = None) -> str:
    """Resolve the evaluator-pinned ``rg`` without falling back to ambient PATH."""
    configured = explicit or env.get(RIPGREP_BIN_ENV, "")
    if not configured:
        raise FileNotFoundError(
            f"ripgrep is not configured; set {RIPGREP_BIN_ENV} to the absolute "
            "path of the organizer-managed ripgrep executable"
        )
    expanded = os.path.expanduser(configured)
    if not os.path.isabs(expanded):
        raise ValueError(f"{RIPGREP_BIN_ENV} must be an absolute path, got {configured!r}")
    binpath = os.path.abspath(expanded)
    if not os.path.isfile(binpath):
        raise FileNotFoundError(f"configured ripgrep binary does not exist: {binpath}")
    if not os.access(binpath, os.X_OK):
        raise PermissionError(f"configured ripgrep binary is not executable: {binpath}")
    if Path(binpath).name.lower() not in {"rg", "rg.exe"}:
        raise ValueError(
            f"{RIPGREP_BIN_ENV} must point to a binary named rg (or rg.exe), got {binpath}"
        )
    return binpath


def _check_ripgrep(
    binpath: str, env: Mapping[str, str]
) -> None:
    """Check that the configured ripgrep starts before any model request."""
    cp = _sandbox.run_pg(
        [binpath, "--version"],
        timeout=VERSION_CHECK_TIMEOUT_SECONDS,
        env=dict(env),
        capture=True,
    )
    if cp.returncode != 0:
        detail = (cp.stderr or cp.stdout or "no diagnostic output").strip()[-1000:]
        raise RuntimeError(
            f"failed to execute configured ripgrep {binpath}: exit {cp.returncode}: {detail}"
        )


def prepare_ripgrep_environment(
    environment: Mapping[str, str],
    *,
    ripgrep_bin: Optional[str] = None,
) -> Dict[str, str]:
    """Verify pinned ripgrep and prepend only its directory to controlled PATH.

    OpenCode's native Skill tool calls ``Ripgrep.find`` to sample files
    adjacent to ``SKILL.md``. Its binary resolver checks PATH before attempting
    a network download, so the evaluator must make the verified binary visible
    there while retaining the caller's explicit, hermetic base PATH.
    """
    env = {str(key): str(value) for key, value in environment.items()}
    binpath = _find_ripgrep(env, ripgrep_bin)
    controlled = env.get("PATH", "/usr/bin:/bin")
    _check_ripgrep(binpath, {**env, "PATH": controlled})
    binary_dir = str(Path(binpath).parent)
    path_items = [item for item in controlled.split(os.pathsep) if item]
    env["PATH"] = os.pathsep.join([binary_dir, *[item for item in path_items if item != binary_dir]])
    env[RIPGREP_BIN_ENV] = binpath
    return env


def _check_opencode(
    binpath: str, env: Mapping[str, str]
) -> None:
    """Check that OpenCode starts before evaluation."""
    for attempt in range(OPENCODE_VERSION_CHECK_MAX_ATTEMPTS):
        cp = _sandbox.run_pg(
            [binpath, "--version"],
            timeout=VERSION_CHECK_TIMEOUT_SECONDS,
            env=dict(env),
            capture=True,
        )
        if cp.returncode != 124 or attempt + 1 == OPENCODE_VERSION_CHECK_MAX_ATTEMPTS:
            break
        # This probe runs before the Agent and before any provider request.  A
        # single local process-start timeout is safe to retry and must not turn
        # an otherwise valid multi-case sample into a failed Agent attempt.
        time.sleep(OPENCODE_VERSION_CHECK_RETRY_DELAY_SECONDS)
    if cp.returncode != 0:
        detail = (cp.stderr or cp.stdout or "no diagnostic output").strip()[-1000:]
        if cp.returncode == 124:
            raise RuntimeError(
                f"timed out while checking OpenCode version for {binpath}"
            )
        raise RuntimeError(
            f"failed to query OpenCode version for {binpath}: "
            f"exit {cp.returncode}: {detail}"
        )

def configured_build_model(config_path: str) -> str:
    """Read the single model source of truth used by ``--agent build``."""
    try:
        with open(config_path, "r", encoding="utf-8") as fh:
            cfg = json.load(fh)
    except FileNotFoundError:
        raise FileNotFoundError(f"OpenCode config not found: {config_path}") from None
    except (OSError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"cannot read OpenCode config {config_path}: {exc}") from exc

    if not isinstance(cfg, dict):
        raise RuntimeError(f"OpenCode config {config_path} must contain a JSON object")
    model = cfg.get("agent", {}).get("build", {}).get("model")
    if not isinstance(model, str) or not model.strip():
        raise RuntimeError(
            f"OpenCode config {config_path} must define agent.build.model; "
            "the evaluator does not override it with CLI -m"
        )
    return model.strip()


def _configured_output_limit(config_path: str, model_ref: str) -> int:
    """Read the build model's per-request output cap from opencode.json."""
    with open(config_path, "r", encoding="utf-8") as stream:
        cfg = json.load(stream)
    try:
        provider_id, model_id = model_ref.split("/", 1)
        value = cfg["provider"][provider_id]["models"][model_id]["limit"]["output"]
    except (KeyError, TypeError, ValueError) as exc:
        raise RuntimeError(
            f"OpenCode config {config_path} must define a numeric output limit "
            f"for agent.build.model={model_ref!r}"
        ) from exc
    if (
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or value <= 0
        or int(value) != value
    ):
        raise RuntimeError(
            f"invalid output limit for {model_ref!r} in {config_path}: {value!r}"
        )
    return int(value)


def _configured_model_request_timeout_seconds(
    config_path: str, model_ref: str
) -> float:
    """Read and enforce the pinned OpenCode total request deadline."""
    with open(config_path, "r", encoding="utf-8") as stream:
        cfg = json.load(stream)
    try:
        provider_id, _model_id = model_ref.split("/", 1)
        value = cfg["provider"][provider_id]["options"]["timeout"]
    except (KeyError, TypeError, ValueError) as exc:
        raise RuntimeError(
            f"OpenCode config {config_path} must define provider.options.timeout "
            f"for agent.build.model={model_ref!r}"
        ) from exc
    if (
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not math.isfinite(float(value))
        or float(value) <= 0.0
        or int(value) != value
    ):
        raise RuntimeError(
            f"invalid provider request timeout for {model_ref!r} in {config_path}"
        )
    if int(value) != PINNED_MODEL_REQUEST_TIMEOUT_MS:
        raise RuntimeError("pinned provider request timeout drifted")
    return int(value) / 1000.0


def install_canonical_config(config_path: str, *, model: str) -> Path:
    """Atomically install the repository-owned OpenCode config for this evaluator.

    Each run gets a private destination. Keeping the repository copy as the only
    source of truth avoids deployment drift without making the source tree a
    writable OpenCode home.
    """

    canonical = Path(CANONICAL_OPENCODE_CONFIG_HOME) / "opencode" / "opencode.json"
    try:
        payload = canonical.read_bytes()
    except OSError as exc:
        raise RuntimeError(f"cannot read canonical OpenCode config {canonical}: {exc}") from exc
    cfg = json.loads(payload)
    models = cfg["provider"]["sjtu"]["models"]
    if model not in models:
        raise ValueError(f"unsupported submission model: {model}")
    cfg["agent"]["build"]["model"] = "sjtu/" + model
    cfg["provider"]["sjtu"]["models"] = {model: models[model]}
    payload = (json.dumps(cfg, indent=2) + "\n").encode("utf-8")

    destination = Path(config_path).expanduser()
    if not destination.is_absolute():
        raise ValueError(f"OpenCode config path must be absolute: {config_path!r}")
    destination = Path(os.path.abspath(destination))
    if destination == canonical.resolve():
        raise ValueError("runtime configuration must not overwrite the canonical template")

    directory = destination.parent
    directory.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".opencode.json.", dir=directory)
    descriptor_open = True
    try:
        with os.fdopen(descriptor, "wb") as stream:
            descriptor_open = False
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, destination)
    except Exception:
        if descriptor_open:
            os.close(descriptor)
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise
    return destination.resolve(strict=True)


def _reject_ambient_config_sources(config_path: str) -> None:
    """Reject files OpenCode would merge beside the pinned JSON config.

    OpenCode scans its global config directory for additional
    config, agents, commands, plugins and skills. Fail closed if any such
    executable or instruction-bearing surface is present beside opencode.json.
    """
    config_dir = Path(config_path).resolve().parent
    found = [name for name in FORBIDDEN_CONFIG_ENTRIES if (config_dir / name).exists()]
    if found:
        raise RuntimeError(
            "OpenCode config home contains ambient sources outside the pinned "
            f"opencode.json: {', '.join(found)} under {config_dir}; remove them "
            "from the evaluator config home"
        )


def _pin_private_runtime_homes(workspace: Path, env: Dict[str, str]) -> None:
    """Isolate OpenCode state, auth, cache and home-scoped discovery per run."""
    runtime = workspace.parent / ".opencode-runtime"
    paths = {
        # The Global service explicitly supports this override. Do not
        # repurpose the process HOME, which would also affect child commands.
        "OPENCODE_TEST_HOME": runtime / "home",
        "XDG_DATA_HOME": runtime / "data",
        "XDG_CACHE_HOME": runtime / "cache",
        "XDG_STATE_HOME": runtime / "state",
        "OPENCODE_TEST_MANAGED_CONFIG_DIR": runtime / "managed",
    }
    for path in paths.values():
        path.mkdir(parents=True, exist_ok=True)
    env.update({name: str(path) for name, path in paths.items()})


def _prepare_config_home(workspace: Path) -> Path:
    """Create or validate the config home used only by this OpenCode run."""
    path = workspace.parent / ".opencode-config"
    if path.is_symlink():
        raise ValueError(f"OpenCode config home must not be a symlink: {path}")
    path.mkdir(mode=0o700, parents=True, exist_ok=True)
    if not path.is_dir():
        raise ValueError(f"OpenCode config home must be a directory: {path}")
    path = path.resolve(strict=True)

    config_dir = path / "opencode"
    if config_dir.is_symlink():
        raise ValueError(f"OpenCode config directory must not be a symlink: {config_dir}")
    config_dir.mkdir(mode=0o700, exist_ok=True)
    if not config_dir.is_dir():
        raise ValueError(f"OpenCode config directory must be a directory: {config_dir}")

    path.chmod(0o700)
    config_dir.chmod(0o700)
    return path


FINALIZATION_STEPS = 4
FINALIZATION_TIMEOUT_SECONDS = 600.0
FINALIZATION_PROMPT = """Optimization is over. This is the final submission phase, regardless of
whether your current experiment is complete, promising, or broken. Do not start
any new optimization. You have at most 4 model steps and 10 minutes; the last may have no tools.
Use the first response to identify and, if needed, restore your best validated files.
Test immediately after restoration; reserve a tool-enabled response for fallback.
A closing explanation is optional: leave final files ready within this budget.
Restore the fastest version of kernel.cpp AND its matching compile_options.txt
that you actually tested and found correct. Use your conversation history, not
an untested claim of correctness. If no optimized version passed, restore the
original starter and original compiler options supplied at the start.
Run `bash tools/test_candidate.sh` on those exact files (timeout 600000 ms).
If it fails, restore an earlier passing version, ultimately the starter, and
validate again. Prefer a correct slower submission over a broken faster one.
Leave the validated files in place before answering. A final message cannot
restore files. Only the files you leave are scored: compilation failure earns
zero, and failed sizes earn zero. Earlier successful versions are not restored
automatically. Stop after restoring and validating; do not make further edits.
"""


def _session_id(raw):
    ids = {obj.get("sessionID") for obj in _iter_json_objects(raw or "")
           if isinstance(obj, dict) and obj.get("sessionID")}
    if len(ids) != 1:
        raise RuntimeError("cannot resume without exactly one OpenCode session ID")
    session = ids.pop()
    if not isinstance(session, str) or not re.fullmatch(r"ses_[A-Za-z0-9]+", session):
        raise RuntimeError("invalid OpenCode session ID for continuation")
    return session


def _accept_budget_stop(result, budget):
    """Only a verified local budget error is successful phase completion."""
    if budget is None or result.returncode not in (0, 1):
        return result
    marker = budget.exhausted_marker
    if not any(block["marker"] == marker for block in budget.blocks):
        return result
    errors = [event for event in _iter_json_objects(result.stdout or "")
              if event.get("type") == "error"]
    if not errors or marker not in json.dumps(errors[-1]):
        return result
    return subprocess.CompletedProcess(result.args, 0, result.stdout, result.stderr)


def _continue_phase_session(cmd, first, *, config_path, env, on_output,
                            phase, limit, deadline, budget=None):
    """Spend unused optimization rounds after text stops or output truncation.

    New user turns reset OpenCode's local counter, so reduce its private step
    limit on every resume. The phase deadline and external countdown stay intact.
    """
    result = first
    raw, stderr = first.stdout or "", first.stderr or ""
    continuations = []
    path = Path(config_path)
    original = path.read_bytes()
    try:
        while result.returncode == 0:
            finishes = [obj for obj in _iter_json_objects(result.stdout or "")
                        if obj.get("type") == "step_finish"]
            reason = finishes[-1].get("part", {}).get("reason") if finishes else None
            if reason != "length" and not (phase == "optimization" and reason == "stop"):
                break
            used = budget.used if budget is not None else int(_parse_events(raw)["steps"])
            if used < 1:
                raise RuntimeError("cannot resume phase without round accounting")
            remaining = limit - used
            if remaining <= 0:
                break
            session = _session_id(raw)
            seconds = deadline - time.monotonic()
            if seconds <= 0:
                result = subprocess.CompletedProcess(cmd, 124, "", "phase deadline exhausted")
                break
            config = json.loads(original)
            config["agent"]["build"]["steps"] = remaining
            path.write_text(json.dumps(config), encoding="utf-8")
            notice = (
                "Your previous response reached its output-token limit. It consumed "
                "one round, not the whole phase. "
                if reason == "length" else
                "Continue. Your previous text-only response ended the turn, "
                "but optimization rounds remain. "
            )
            notice += (
                f"{used}/{limit} {phase} rounds have been used; "
                f"{remaining} remain, including the next response. "
                "Use the permitted tools to modify files. "
            )
            if phase == "optimization":
                notice += ("Optimization is still active. "
                           "Use actual tool calls; code in a text reply does not edit files. "
                           "Finalization comes after the optimization round budget is exhausted.")
            else:
                notice += ("This is still finalization. Do not optimize. Restore your best "
                           "tested correct files (or the starter), validate, and finish.")
            entry = {"phase": phase, "session_id": session, "finish_reason": reason,
                     "rounds_used": used, "remaining_rounds": remaining,
                     "prompt": notice}
            continuations.append(entry)
            result = _sandbox.run_pg(
                [*cmd, "--session", session], timeout=seconds, env=env,
                capture=True, on_output=on_output, stdin_text=notice,
            )
            raw += "\n" + (result.stdout or "")
            stderr += "\n" + (result.stderr or "")
            entry["returncode"] = result.returncode
            progress = budget.used if budget is not None else int(_parse_events(raw)["steps"])
            if result.returncode == 0 and progress <= used:
                raise RuntimeError("phase continuation made no recorded round progress")
    finally:
        path.write_bytes(original)
    result = _accept_budget_stop(result, budget)
    return subprocess.CompletedProcess(cmd, result.returncode, raw, stderr), continuations


def _finalize_session(cmd, first, *, config_path, env, on_output, budget=None):
    """Resume the exact session with a fresh bounded, tool-enabled user turn."""
    session = _session_id(first.stdout or "")
    path = Path(config_path)
    original = path.read_bytes()
    config = json.loads(original)
    config["agent"]["build"]["steps"] = FINALIZATION_STEPS
    try:
        path.write_text(json.dumps(config), encoding="utf-8")
        deadline = time.monotonic() + FINALIZATION_TIMEOUT_SECONDS
        result = _sandbox.run_pg(
            [*cmd, "--session", session], timeout=FINALIZATION_TIMEOUT_SECONDS,
            env=env, capture=True, on_output=on_output,
            stdin_text=FINALIZATION_PROMPT,
        )
        result, continuations = _continue_phase_session(
            cmd, result, config_path=config_path, env=env, on_output=on_output,
            phase="finalization", limit=FINALIZATION_STEPS, deadline=deadline, budget=budget,
        )
    finally:
        path.write_bytes(original)
    return result, {"attempted": True, "session_id": session,
                    "step_limit": FINALIZATION_STEPS,
                    "continuations": continuations,
                    "returncode": result.returncode,
                    "timed_out": result.returncode == 124}


def _can_finalize(result):
    # A killed optimization process still has a persistent session to restore.
    # Do not resume unrelated execution errors or invent missing session IDs.
    if result.returncode not in (0, 124):
        return False
    try:
        _session_id(result.stdout or "")
    except RuntimeError:
        return False
    return True


def run_round(
    workspace: str,
    prompt: str,
    timeout: float = 3600.0,
    api_key_value: Optional[str] = None,
    opencode_bin: Optional[str] = None,
    ripgrep_bin: Optional[str] = None,
    extra_env: Optional[Mapping[str, str]] = None,
    on_output: Optional[Callable[[str, str], None]] = None,
    finalize: bool = False,
    *,
    model: str,
) -> Dict[str, object]:
    """Run one headless opencode round in `workspace`.

    Returns:
        {
          "text":        assistant final text (best-effort),
          "tokens_in":   int, "tokens_out": int,   (0 if unavailable)
          "tool_calls":  int,                        (0 if unavailable)
          "tool_names":  [str, ...],
          "returncode":  int,
          "timed_out":   bool,
          "raw":         raw stdout (truncated),
        }

    The API key is passed only through the configured environment variable and
    is never written to the repository or run artifacts.
    """
    workspace_path = Path(workspace).resolve()
    if not workspace_path.is_dir():
        raise FileNotFoundError(f"OpenCode workspace not found: {workspace_path}")

    # Formal callers provide a complete, explicit environment. Direct local
    # callers receive only the small documented allowlist below rather than
    # the complete login environment.
    env = (
        {str(key): str(value) for key, value in extra_env.items()}
        if extra_env is not None
        else {
            key: os.environ[key]
            for key in _DIRECT_ENV_ALLOWLIST
            if key in os.environ
        }
    )
    env.setdefault("PATH", "/usr/bin:/bin")
    env.setdefault("LANG", "C")
    env.setdefault("LC_ALL", "C")
    # Start from no ambient OpenCode flags; the exact allowed set is assigned
    # below.  This also prevents hidden DB/model/plugin path overrides.
    for key in tuple(env):
        if key.startswith("OPENCODE_"):
            env.pop(key, None)
    _pin_private_runtime_homes(workspace_path, env)
    binpath = _find_opencode(env, opencode_bin)
    if not api_key_value:
        raise EnvironmentError("model API key is not configured")
    if not env.get("CHEATSHEET_MODEL_BASE_URL"):
        raise EnvironmentError("model base URL is not configured")
    env[MODEL_API_KEY_ENV] = api_key_value
    note = ""

    # Pin OpenCode to a private per-run copy of the canonical configuration.
    cfg_home = _prepare_config_home(workspace_path)
    env["XDG_CONFIG_HOME"] = str(cfg_home)
    # Compute nodes may not have reliable access to the public models catalog.
    # Every model used by this evaluator is declared in the pinned config, so a
    # remote catalog fetch is unnecessary and can otherwise hang bootstrap.
    env["OPENCODE_DISABLE_MODELS_FETCH"] = "true"
    env["OPENCODE_DISABLE_AUTOUPDATE"] = "true"
    env["OPENCODE_DISABLE_DEFAULT_PLUGINS"] = "true"
    env["OPENCODE_DISABLE_EXTERNAL_SKILLS"] = "true"
    # The one allowed project surface is the native Skill path explicitly
    # declared in the global pinned config. Ignore any ambient project config,
    # commands or instructions in the workspace itself.
    env["OPENCODE_DISABLE_PROJECT_CONFIG"] = "true"
    env["OPENCODE_DISABLE_CLAUDE_CODE"] = "true"
    env["OPENCODE_DISABLE_LSP_DOWNLOAD"] = "true"
    config_path = str(cfg_home / "opencode" / "opencode.json")
    config_path = str(install_canonical_config(config_path, model=model))
    _reject_ambient_config_sources(config_path)
    configured_model = configured_build_model(config_path)
    _configured_model_request_timeout_seconds(config_path, configured_model)
    env["OPENCODE_EXPERIMENTAL_OUTPUT_TOKEN_MAX"] = str(
        _configured_output_limit(config_path, configured_model)
    )

    env = prepare_ripgrep_environment(
        env,
        ripgrep_bin=ripgrep_bin,
    )
    _check_opencode(binpath, env)

    cmd = [
        binpath, "--pure", "run",
        "--dir", str(workspace_path),
        "--agent", "build",
        "--format", "json",
    ]
    # Positional messages are shell-escaped again by this OpenCode CLI.
    # stdin preserves source quotes, backslashes and newlines exactly.

    from .round_countdown import CountdownRelay

    phase_limit = json.loads(Path(config_path).read_text())["agent"]["build"]["steps"]
    with CountdownRelay(
        env["CHEATSHEET_MODEL_BASE_URL"], api_key_value, phase_limit,
        proxy=env.get("HTTPS_PROXY") or env.get("https_proxy"),
        timeout=_configured_model_request_timeout_seconds(config_path, configured_model),
    ) as relay:
        env["CHEATSHEET_MODEL_BASE_URL"] = relay.base_url
        env[MODEL_API_KEY_ENV] = relay.token
        # Loopback requests must not pass through a cluster HTTP proxy. The
        # adapter retains the explicit proxy for the real model endpoint.
        env["NO_PROXY"] = env["no_proxy"] = "127.0.0.1,localhost"
        optimization_deadline = time.monotonic() + timeout
        cp = _sandbox.run_pg(
            cmd,
            timeout=timeout,
            env=env,
            capture=True,
            on_output=on_output,
            stdin_text=prompt,
        )
        cp, optimization_continuations = _continue_phase_session(
            cmd, cp, config_path=config_path, env=env, on_output=on_output,
            phase="optimization", limit=phase_limit, deadline=optimization_deadline, budget=relay.countdown,
        )
        optimization_returncode = cp.returncode
        raw = cp.stdout or ""
        finalization = {"attempted": False}
        if finalize and _can_finalize(cp):
            relay.countdown.begin_phase("finalization", FINALIZATION_STEPS)
            cp, finalization = _finalize_session(
                cmd, cp, config_path=config_path, env=env, on_output=on_output, budget=relay.countdown,
            )
            finalization["trigger"] = "optimization_timeout" if optimization_returncode == 124 else "optimization_complete"
            raw += "\n" + (cp.stdout or "")
        elif finalize:
            finalization["reason"] = "optimization process did not exit successfully"
        countdown_records = list(relay.countdown.records)
        budget_blocks = list(relay.countdown.blocks)
    timed_out = cp.returncode == 124

    parsed = _parse_events(raw)
    if not parsed["text"] and cp.stderr:
        # last resort: some builds print the answer to stderr
        parsed["text"] = _strip_dsw_banner(cp.stderr)[-4000:]

    # OpenCode exposes one step_finish.part.tokens record per model request. If
    # an upstream/provider failure omits those records, retain a clearly marked
    # length estimate for telemetry; this value is never presented as a hard
    # real-time session budget.
    tin = int(parsed["tokens_in"])
    tout = int(parsed["tokens_out"])
    treasoning = int(parsed.get("tokens_reasoning", 0))
    ttotal = int(parsed.get("tokens_total", 0))
    estimated = False
    if not ttotal and not tin and not tout and not treasoning:
        estimated = True
        tin = _estimate_tokens(prompt)
        tout = _estimate_tokens(parsed["text"]) if parsed["text"] else 0
        ttotal = tin + tout

    return {
        "text": parsed["text"],
        "model": configured_model,
        "reasoning_effort": "medium",
        "tokens_in": tin,
        "tokens_out": tout,
        "tokens_reasoning": treasoning,
        "tokens_total": ttotal or (tin + tout + treasoning),
        "tokens_estimated": estimated,
        "tool_calls": parsed["tool_calls"],
        "tool_names": parsed["tool_names"],
        "tool_counts": parsed.get("tool_counts", {}),
        "tool_error_count": parsed.get("tool_error_count", 0),
        "tool_errors": parsed.get("tool_errors", []),
        "skill_status": parsed.get("skill_status", "not_called"),
        "skill_error": parsed.get("skill_error", ""),
        "bash_commands": parsed.get("bash_commands", []),
        "returncode": cp.returncode,
        "process_ok": cp.returncode == 0,
        "steps_used": int(parsed.get("steps", 0)),
        "optimization_returncode": optimization_returncode,
        "optimization_continuations": optimization_continuations,
        "round_countdown": countdown_records,
        "round_budget_blocks": budget_blocks,
        "phase_rounds_used": {phase: sum(r["phase"] == phase and not r["retry"]
                                         for r in countdown_records)
                              for phase in ("optimization", "finalization")},
        "finalization": finalization,
        "timed_out": timed_out,
        "note": note,
        "raw": raw[:20000],
    }


def _estimate_tokens(s: str) -> int:
    """Fallback telemetry estimate (~3.6 chars/token for mixed text/code)."""
    return int(len(s) / 3.6) if s else 0


# --------------------------------------------------------------------------- #
# event-stream parsing (defensive)
# --------------------------------------------------------------------------- #
_BANNER_RE = re.compile(r"^\s*[_|/\\ ].*$")


def _strip_dsw_banner(text: str) -> str:
    """Drop the PAI DSW login banner lines that some shells inject."""
    lines = text.splitlines()
    out = []
    skipping = True
    for ln in lines:
        if skipping and ("Welcome to PAI DSW" in ln or "Mounted datasets" in ln
                          or ln.strip().startswith(("│", "╭", "╰", "├"))):
            continue
        skipping = False
        out.append(ln)
    return "\n".join(out)


def _iter_json_objects(raw: str):
    """Yield JSON objects from raw stdout, tolerating JSONL or concatenated docs."""
    raw = _strip_dsw_banner(raw).strip()
    if not raw:
        return
    # 1) whole document is one JSON value
    try:
        obj = json.loads(raw)
        if isinstance(obj, list):
            for o in obj:
                yield o
        else:
            yield obj
        return
    except json.JSONDecodeError:
        pass
    # 2) line-delimited JSON
    any_line = False
    for line in raw.splitlines():
        line = line.strip()
        if not line or line[0] not in "{[":
            continue
        try:
            yield json.loads(line)
            any_line = True
        except json.JSONDecodeError:
            continue
    if any_line:
        return
    # 3) concatenated objects: use a decoder to walk the buffer
    dec = json.JSONDecoder()
    idx = 0
    n = len(raw)
    while idx < n:
        while idx < n and raw[idx] not in "{[":
            idx += 1
        if idx >= n:
            break
        try:
            obj, end = dec.raw_decode(raw, idx)
            yield obj
            idx = end
        except json.JSONDecodeError:
            idx += 1


def _walk(obj, texts, usage, tools):
    """Recursively harvest text / token usage / tool-call signals from any shape."""
    if isinstance(obj, dict):
        t = obj.get("type") or obj.get("event") or ""
        # token usage — many shapes: {"usage":{...}} or {"tokens":{...}}
        for key in ("usage", "tokens"):
            u = obj.get(key)
            if isinstance(u, dict):
                _harvest_usage(u, usage)
        # tool call events
        if "tool" in str(t).lower() or obj.get("tool") or obj.get("toolName"):
            name = obj.get("tool") or obj.get("toolName") or obj.get("name")
            if isinstance(name, str) and name and "toolinvocation" not in name.lower():
                tools.append(name)
        # assistant text
        for tkey in ("text", "content", "message", "output"):
            v = obj.get(tkey)
            if isinstance(v, str) and v.strip():
                texts.append(v)
        for v in obj.values():
            _walk(v, texts, usage, tools)
    elif isinstance(obj, list):
        for v in obj:
            _walk(v, texts, usage, tools)


def _harvest_usage(u: dict, usage: dict):
    for k, v in u.items():
        if not isinstance(v, (int, float)):
            continue
        kl = k.lower()
        if "input" in kl or "prompt" in kl:
            usage["in"] = max(usage.get("in", 0), int(v))
        elif "output" in kl or "completion" in kl:
            usage["out"] = max(usage.get("out", 0), int(v))
        elif "reasoning" in kl:
            usage["reasoning"] = max(usage.get("reasoning", 0), int(v))
        elif kl in ("tokens", "total", "total_tokens"):
            usage["total"] = max(usage.get("total", 0), int(v))


def _number(value: object) -> Optional[float]:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    return float(value)


def _tool_event_record(obj: object) -> Optional[Dict[str, object]]:
    """Normalize one OpenCode tool event without depending on a single schema."""
    if not isinstance(obj, dict):
        return None
    part = obj.get("part") if isinstance(obj.get("part"), dict) else obj
    event_type = str(obj.get("type") or obj.get("event") or "").lower()
    part_type = str(part.get("type") or "").lower()
    tool = part.get("tool") or part.get("toolName")
    if not isinstance(tool, str) or not tool:
        if "tool" not in event_type and part_type != "tool":
            return None
        tool = obj.get("tool") or obj.get("toolName") or obj.get("name")
    if not isinstance(tool, str) or not tool:
        return None

    state = part.get("state") if isinstance(part.get("state"), dict) else {}
    status = state.get("status") or part.get("status") or obj.get("status") or "unknown"
    status = str(status).lower()
    input_value = state.get("input", part.get("input", obj.get("input", {})))
    if not isinstance(input_value, dict):
        input_value = {"value": input_value}
    output = state.get("output", part.get("output", obj.get("output", "")))
    error = state.get("error", part.get("error", obj.get("error", "")))
    metadata = state.get("metadata") if isinstance(state.get("metadata"), dict) else {}
    returncode = None
    for source in (metadata, state, part, obj):
        for key in ("exit", "exitCode", "returncode", "returnCode", "code"):
            value = source.get(key) if isinstance(source, dict) else None
            if isinstance(value, (int, float)) and not isinstance(value, bool):
                returncode = int(value)
                break
        if returncode is not None:
            break
    timing = state.get("time") if isinstance(state.get("time"), dict) else {}
    started = _number(timing.get("start"))
    ended = _number(timing.get("end"))
    duration = None
    if started is not None and ended is not None and ended >= started:
        duration = (ended - started) / 1000.0
    call_id = (
        part.get("callID")
        or part.get("callId")
        or part.get("id")
        or obj.get("callID")
        or obj.get("id")
    )
    return {
        "call_id": str(call_id or ""),
        "timestamp": obj.get("timestamp"),
        "tool": tool,
        "status": status,
        "input": input_value,
        "returncode": returncode,
        "duration_seconds": duration,
        "output": output if output is not None else "",
        "error": error if error is not None else "",
    }


def _summarize_tool_records(records: List[Dict[str, object]]) -> Dict[str, object]:
    latest: Dict[str, Dict[str, object]] = {}
    order: List[str] = []
    for position, record in enumerate(records):
        identity = str(record.get("call_id") or f"event-{position}")
        if identity not in latest:
            order.append(identity)
        latest[identity] = record
    final = [latest[identity] for identity in order]
    counts: Dict[str, int] = {}
    errors: List[Dict[str, object]] = []
    skill_status = "not_called"
    skill_error = ""
    bash_commands: List[str] = []
    for record in final:
        name = str(record.get("tool") or "unknown")
        counts[name] = counts.get(name, 0) + 1
        status = str(record.get("status") or "unknown")
        rc = record.get("returncode")
        failed = status in {"error", "failed", "failure", "cancelled"} or (
            isinstance(rc, int) and rc != 0
        )
        if failed:
            errors.append(record)
        inputs = record.get("input") if isinstance(record.get("input"), dict) else {}
        if name == "skill":
            if failed:
                skill_status = "error"
                skill_error = str(record.get("error") or record.get("output") or "skill failed")
            elif status in {"completed", "success", "succeeded", "done"}:
                skill_status = "loaded"
        if name == "bash":
            command = inputs.get("command") if isinstance(inputs, dict) else None
            if isinstance(command, str):
                bash_commands.append(command)
    return {
        "tool_counts": counts,
        "tool_error_count": len(errors),
        "tool_errors": errors,
        "skill_status": skill_status,
        "skill_error": skill_error,
        "bash_commands": bash_commands,
    }


def _parse_events(raw: str) -> Dict[str, object]:
    fallback_texts: List[str] = []
    fallback_usage: Dict[str, int] = {}
    fallback_tools: List[str] = []
    exact_texts: List[str] = []
    exact_usage = {"in": 0, "out": 0, "reasoning": 0, "cache_read": 0,
                   "cache_write": 0, "total": 0}
    exact_step_ids = set()
    exact_tool_ids = set()
    exact_tool_names: List[str] = []
    saw_exact_step = False
    saw_json = False
    tool_records: List[Dict[str, object]] = []
    for obj in _iter_json_objects(raw):
        saw_json = True
        _walk(obj, fallback_texts, fallback_usage, fallback_tools)
        normalized_tool = _tool_event_record(obj)
        if normalized_tool is not None:
            tool_records.append(normalized_tool)
        if not isinstance(obj, dict):
            continue
        event_type = obj.get("type")
        part = obj.get("part")
        if not isinstance(part, dict):
            continue
        if event_type == "text" and isinstance(part.get("text"), str):
            exact_texts.append(part["text"])
        elif event_type == "tool_use":
            identity = (
                obj.get("sessionID"),
                part.get("id") or part.get("callID") or part.get("messageID"),
            )
            if identity not in exact_tool_ids:
                exact_tool_ids.add(identity)
                name = part.get("tool")
                if isinstance(name, str):
                    exact_tool_names.append(name)
        elif event_type == "step_finish" and part.get("type") == "step-finish":
            identity = (obj.get("sessionID"), part.get("id") or part.get("messageID"))
            if identity in exact_step_ids:
                continue
            exact_step_ids.add(identity)
            tokens = part.get("tokens")
            if not isinstance(tokens, dict):
                continue
            saw_exact_step = True
            input_tokens = _nonnegative_int(tokens.get("input"))
            output_tokens = _nonnegative_int(tokens.get("output"))
            reasoning_tokens = _nonnegative_int(tokens.get("reasoning"))
            cache = tokens.get("cache") if isinstance(tokens.get("cache"), dict) else {}
            cache_read = _nonnegative_int(cache.get("read"))
            cache_write = _nonnegative_int(cache.get("write"))
            exact_usage["in"] += input_tokens
            exact_usage["out"] += output_tokens
            exact_usage["reasoning"] += reasoning_tokens
            exact_usage["cache_read"] += cache_read
            exact_usage["cache_write"] += cache_write
            step_total = tokens.get("total")
            exact_usage["total"] += (
                _nonnegative_int(step_total)
                if isinstance(step_total, (int, float))
                else input_tokens + output_tokens + reasoning_tokens + cache_read + cache_write
            )

    if not saw_json:
        # plain text fallback
        return {
            "text": _strip_dsw_banner(raw).strip()[-8000:],
            "tokens_in": 0, "tokens_out": 0,
            "steps": 0,
            "tool_calls": 0, "tool_names": [],
            **_summarize_tool_records([]),
        }

    tool_summary = _summarize_tool_records(tool_records)
    if saw_exact_step:
        text = exact_texts[-1] if exact_texts else (
            max(fallback_texts, key=len) if fallback_texts else ""
        )
        return {
            "text": text[-8000:],
            "tokens_in": exact_usage["in"],
            "tokens_out": exact_usage["out"],
            "tokens_reasoning": exact_usage["reasoning"],
            "tokens_cache_read": exact_usage["cache_read"],
            "tokens_cache_write": exact_usage["cache_write"],
            "tokens_total": exact_usage["total"],
            "steps": len(exact_step_ids),
            "tool_calls": len(exact_tool_names),
            "tool_names": exact_tool_names,
            **tool_summary,
        }

    # Compatibility fallback for non-pinned/partial event shapes.
    text = max(fallback_texts, key=len) if fallback_texts else ""
    tin = fallback_usage.get("in", 0)
    tout = fallback_usage.get("out", 0)
    reasoning = fallback_usage.get("reasoning", 0)
    total = fallback_usage.get("total", 0)
    if not tin and not tout and total:
        tout = total  # split unavailable in this legacy shape
    return {
        "text": text[-8000:],
        "tokens_in": tin,
        "tokens_out": tout,
        "tokens_reasoning": reasoning,
        "tokens_total": total or (tin + tout + reasoning),
        "steps": 0,
        "tool_calls": len(fallback_tools),
        "tool_names": fallback_tools,
        **tool_summary,
    }


def _nonnegative_int(value: object) -> int:
    if not isinstance(value, (int, float)):
        return 0
    return max(0, int(value))


# --------------------------------------------------------------------------- #
# unit test for the JSON parser (no API cost)
# --------------------------------------------------------------------------- #
def _selftest() -> None:
    sample = "\n".join([
        '{"type":"session.start","session":"abc"}',
        '{"type":"tool.call","tool":"edit","args":{"path":"kernel.cpp"}}',
        '{"type":"tool.call","tool":"bash","args":{"command":"bash tools/build.sh"}}',
        '{"type":"message","content":"I vectorized the inner loop with ARM SIMD."}',
        '{"type":"session.end","usage":{"input_tokens":1200,"output_tokens":350}}',
    ])
    r = _parse_events(sample)
    print("JSONL parse:", {k: r[k] for k in ("tokens_in", "tokens_out", "tool_calls")})
    assert r["tokens_in"] == 1200, r
    assert r["tokens_out"] == 350, r
    assert r["tool_calls"] == 2, r
    assert "ARM SIMD" in r["text"], r

    # whole-document array shape
    arr = json.dumps([
        {"type": "step", "usage": {"prompt_tokens": 10, "completion_tokens": 5}},
        {"type": "tool", "toolName": "read"},
        {"type": "text", "text": "done"},
    ])
    r2 = _parse_events(arr)
    print("array parse:", {k: r2[k] for k in ("tokens_in", "tokens_out", "tool_calls")})
    assert r2["tokens_in"] == 10 and r2["tokens_out"] == 5 and r2["tool_calls"] == 1, r2

    # plain-text fallback
    r3 = _parse_events("not json at all, just prose")
    assert r3["text"].startswith("not json"), r3
    assert r3["tokens_in"] == 0, r3

    print("opencode_agent JSON-parse self-test PASSED")


if __name__ == "__main__":
    _selftest()
