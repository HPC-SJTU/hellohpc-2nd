"""Regressions for the build-cache binary binding (no device needed)."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import pytest

from tools.benchmark.player_benchmark import _installed_digest


@pytest.fixture
def install(tmp_path: Path) -> Path:
    install = tmp_path / "build" / "demo-install"
    (install / "bin").mkdir(parents=True)
    (install / "lib64").mkdir(parents=True)
    (install / "bin" / "rsm_demo_runner").write_bytes(b"runner-bytes")
    (install / "lib64" / "librsm_demo_kernels.so").write_bytes(b"kernel-bytes-A")
    return install


def test_digest_covers_runner_and_kernel_library(install: Path, tmp_path: Path) -> None:
    first = _installed_digest(tmp_path, "demo")
    assert first is not None
    # Swapping ONLY the device kernel library must change the digest: this is
    # the "external build B while sources look like A" attack the cache has to
    # refuse.
    (install / "lib64" / "librsm_demo_kernels.so").write_bytes(b"kernel-bytes-B")
    second = _installed_digest(tmp_path, "demo")
    assert first != second


def test_missing_kernel_library_disables_the_digest(install: Path, tmp_path: Path) -> None:
    (install / "lib64" / "librsm_demo_kernels.so").unlink()
    assert _installed_digest(tmp_path, "demo") is None
    (install / "bin" / "rsm_demo_runner").unlink()
    (install / "lib64" / "librsm_demo_kernels.so").write_bytes(b"kernel-bytes-A")
    assert _installed_digest(tmp_path, "demo") is None
