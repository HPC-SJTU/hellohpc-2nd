"""Regressions for the build-cache toolchain identity.

Run with pytest from the repository root (any interpreter with pytest
installed); no NPU or CANN installation is required — the probe runs against
a synthetic scripts/env.sh and a synthetic CANN home.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.benchmark.player_benchmark import _toolchain_identity

FAKE_CANN = "/fake-cann-home/cann-8.5.0"
DRIVER_RELATIVE = "aarch64-linux/ccec_compiler/bin/ccec_compiler"


def _make_repo(tmp_path: Path, driver_content: bytes | None) -> Path:
    root = tmp_path / "repo"
    (root / "scripts").mkdir(parents=True)
    cann_home = tmp_path / "fake-cann-home" / "cann-8.5.0"
    driver = cann_home / DRIVER_RELATIVE
    driver.parent.mkdir(parents=True, exist_ok=True)
    env_lines = [
        f"export ASCEND_HOME_PATH='{cann_home}'",
        "export CANN_VERSION=8.5.0",
        "export RSM_TOOLCHAIN_ROOT='/fake-toolchain'",
        "export CC='/bin/sh'",
        "export CXX='/bin/sh'",
    ]
    if driver_content is not None:
        driver.write_bytes(driver_content)
        # the driver must be executable for the [ -x ] fallback test
        driver.chmod(0o755)
    else:
        env_lines.append("unset ASCEND_HOME_PATH")
    (root / "scripts" / "env.sh").write_text("\n".join(env_lines) + "\n", encoding="utf-8")
    return root


def test_driver_off_path_is_found_through_ascend_home_path(tmp_path: Path) -> None:
    root = _make_repo(tmp_path, b"driver-content-A")
    identity = _toolchain_identity(root)
    assert identity is not None
    assert identity["cann_home"] == str(
        tmp_path / "fake-cann-home" / "cann-8.5.0"
    )
    assert identity["cann_version"] == "8.5.0"


def test_driver_content_change_changes_the_identity(tmp_path: Path) -> None:
    # The fallback driver lives under the CANN home, not on PATH; its content
    # is part of the identity, so swapping the compiler must invalidate the
    # build cache.
    root = _make_repo(tmp_path, b"driver-content-A")
    first = _toolchain_identity(root)
    driver = tmp_path / "fake-cann-home" / "cann-8.5.0" / DRIVER_RELATIVE
    driver.write_bytes(b"driver-content-B")
    second = _toolchain_identity(root)
    assert first is not None and second is not None
    assert first["sha256"] != second["sha256"]


def test_missing_device_compiler_disables_the_identity(tmp_path: Path) -> None:
    # No driver anywhere: the device compiler identity is unknown, and the
    # caller must treat the cache as unusable instead of caching anyway.
    root = _make_repo(tmp_path, None)
    assert _toolchain_identity(root) is None


def test_missing_host_compiler_disables_the_identity(tmp_path: Path) -> None:
    root = _make_repo(tmp_path, b"driver-content-A")
    env = root / "scripts" / "env.sh"
    env.write_text(
        env.read_text(encoding="utf-8").replace("export CC='/bin/sh'", "export CC='/nonexistent-cc'"),
        encoding="utf-8",
    )
    assert _toolchain_identity(root) is None
