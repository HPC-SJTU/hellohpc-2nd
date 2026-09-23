"""Validation for agent-selected kernel-only compiler options.

The options are intentionally narrower than arbitrary compiler command-line
access.  They may tune GCC's optimizer and target, but may not add inputs,
paths, plugins, linker commands, instrumentation runtimes, or output files.
"""

from __future__ import annotations

import argparse
import re
import shlex
from pathlib import Path
from typing import Iterable, List


MAX_OPTIONS = 24
MAX_BYTES = 1024

_SAFE_PATTERNS = (
    re.compile(r"-O(?:[0-3sg]|fast)"),
    re.compile(r"-m[a-zA-Z0-9][a-zA-Z0-9_.=+,-]*"),
    re.compile(r"--param=[a-zA-Z0-9_.-]+=[a-zA-Z0-9_.+-]+"),
    re.compile(r"-f[a-zA-Z0-9][a-zA-Z0-9_.=+,-]*"),
    re.compile(r"-D[A-Za-z_][A-Za-z0-9_]*(?:=[A-Za-z0-9_.+,-]+)?"),
    re.compile(r"-U[A-Za-z_][A-Za-z0-9_]*"),
)

# These families can execute compiler-side code, add arbitrary filesystem
# inputs/outputs, weaken the diagnostic build, or require a runtime profile
# that the current read-only worker deliberately cannot produce.
_FORBIDDEN_PREFIXES = (
    "-fplugin",
    "-fprofile",
    "-fsanitize",
    "-fno-sanitize",
    "-finstrument-functions",
    "-fuse-ld",
    "-fdump",
    "-I",
    "-L",
    "-B",
    "-l",
    "-Wl,",
    "-Xlinker",
    "-specs",
    "-wrapper",
    "-include",
    "-imacros",
    "-idirafter",
    "-iquote",
    "-isystem",
    "-isysroot",
    "--sysroot",
    "-nostdinc",
    "-nostdlib",
    "-nodefaultlibs",
    "-shared",
    "-static",
    "-pie",
    "-no-pie",
    "-x",
    "-o",
)


class CompileOptionsError(ValueError):
    """Raised when the agent compiler-option surface is invalid."""


def validate_options(options: Iterable[str]) -> List[str]:
    result = list(options)
    if len(result) > MAX_OPTIONS:
        raise CompileOptionsError(
            f"at most {MAX_OPTIONS} compiler options are allowed, got {len(result)}"
        )
    if sum(len(item.encode("utf-8")) + 1 for item in result) > MAX_BYTES:
        raise CompileOptionsError(f"compiler options exceed {MAX_BYTES} bytes")

    for option in result:
        if not option or any(character.isspace() for character in option):
            raise CompileOptionsError("each compiler option must be one token")
        if any(character in option for character in ("/", "\\", "@", ":")):
            raise CompileOptionsError(f"paths and response files are not allowed: {option}")
        if option.startswith(_FORBIDDEN_PREFIXES):
            raise CompileOptionsError(f"compiler option is outside the tuning surface: {option}")
        if not any(pattern.fullmatch(option) for pattern in _SAFE_PATTERNS):
            raise CompileOptionsError(f"unsupported compiler option: {option}")
    return result


def parse_options(text: str) -> List[str]:
    if len(text.encode("utf-8")) > MAX_BYTES:
        raise CompileOptionsError(f"compile_options.txt exceeds {MAX_BYTES} bytes")
    logical_lines = []
    for line in text.splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            logical_lines.append(stripped)
    try:
        options = shlex.split(" ".join(logical_lines), posix=True)
    except ValueError as exc:
        raise CompileOptionsError(f"cannot parse compile_options.txt: {exc}") from exc
    return validate_options(options)


def load_options(path: Path | str) -> List[str]:
    option_path = Path(path)
    if not option_path.is_file():
        return []
    try:
        if option_path.stat().st_size > MAX_BYTES:
            raise CompileOptionsError(
                f"compile_options.txt exceeds {MAX_BYTES} bytes"
            )
        return parse_options(option_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError) as exc:
        raise CompileOptionsError(f"cannot read compile options: {exc}") from exc


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path")
    args = parser.parse_args()
    try:
        for option in load_options(args.path):
            print(option)
    except CompileOptionsError as exc:
        parser.exit(2, f"compile options error: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
