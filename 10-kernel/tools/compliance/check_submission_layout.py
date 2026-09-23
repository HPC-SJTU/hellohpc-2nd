#!/usr/bin/env python3
"""Verify that contestant-controlled launch glue cannot replace the judge runner."""

from __future__ import annotations

import argparse
import json
import posixpath
import re
from pathlib import Path, PurePosixPath


EXPECTED_BUILD = """#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT_DIR}/scripts/env.sh"

BUILD_DIR="${ROOT_DIR}/build/solution"
INSTALL_DIR="${ROOT_DIR}/build/solution-install"
rm -rf "${BUILD_DIR}" "${INSTALL_DIR}"
mkdir -p "${BUILD_DIR}" "${INSTALL_DIR}" "${ROOT_DIR}/artifacts/logs"

cmake -S "${ROOT_DIR}/solution" -B "${BUILD_DIR}" -G "Unix Makefiles" \\
  -DSOC_VERSION=Ascend910B3 -DRUN_MODE=npu -DCMAKE_BUILD_TYPE=Release \\
  -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \\
  -DASCEND_CANN_PACKAGE_PATH="${ASCEND_HOME_PATH}"
cmake --build "${BUILD_DIR}" -j "${RSM_BUILD_JOBS:-8}"
cmake --install "${BUILD_DIR}"
"""

EXPECTED_RUN = """#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT_DIR}/scripts/env.sh"
INSTALL_DIR="${ROOT_DIR}/build/solution-install"
export LD_LIBRARY_PATH="${INSTALL_DIR}/lib64:${ASCEND_HOME_PATH}/lib64:${LD_LIBRARY_PATH:-}"
exec "${INSTALL_DIR}/bin/rsm_solution_runner" "$@"
"""

DANGEROUS_CMAKE = re.compile(
    r"\b(?:execute_process|add_custom_command|add_custom_target|configure_file|"
    r"file|cmake_language|include_guard|find_program|find_package|try_compile|"
    r"try_run|ExternalProject_Add|FetchContent_Declare)\s*\(",
    re.IGNORECASE,
)
ALLOWED_CMAKE_COMMANDS = {
    "cmake_minimum_required", "project", "set", "if", "elseif", "else", "endif",
    "message", "include", "ascendc_library", "add_executable", "add_dependencies",
    "target_include_directories", "target_link_directories", "target_link_libraries", "install",
}
ALLOWED_CMAKE_SET_VARIABLES = {
    "CMAKE_CXX_STANDARD", "CMAKE_CXX_STANDARD_REQUIRED", "SOC_VERSION",
    "ASCEND_CANN_PACKAGE_PATH", "RUN_MODE", "ASCENDC_CMAKE_DIR",
}
ALLOWED_SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"}
ALLOWED_LINK_TOKEN = re.compile(r"^(?:rsm_solution[A-Za-z0-9_]*|ascendcl|PRIVATE|PUBLIC|INTERFACE)$")


def check(root: Path, variant: str = "solution") -> dict:
    directory = root / variant
    findings: list[dict[str, str]] = []
    required = ["run.sh", "build.sh", "CMakeLists.txt", "runner/main.cpp"]
    for relative in required:
        if not (directory / relative).is_file():
            findings.append({"kind": "missing_file", "value": relative})
    if findings:
        return {"schema_version": 1, "passed": False, "findings": findings}

    for path in directory.rglob("*"):
        if path.is_symlink():
            findings.append({"kind": "submission_symlink", "value": str(path.relative_to(directory))})
        if not path.is_file():
            continue
        relative = path.relative_to(directory)
        allowed_root_file = relative.as_posix() in {
            "CMakeLists.txt", "README.md", "build.sh", "run.sh"
        }
        allowed_source = (
            relative.parts[0] in {"op_host", "op_kernel", "runner"}
            and path.suffix.lower() in ALLOWED_SOURCE_SUFFIXES
        )
        if not allowed_root_file and not allowed_source:
            findings.append({"kind": "unexpected_submission_file", "value": relative.as_posix()})
        if allowed_source:
            source = path.read_text(encoding="utf-8", errors="replace")
            for included in re.findall(r"^\s*#\s*include\s*[<\"]([^>\"]+)[>\"]", source, re.M):
                if included == "../../tools/common/runner/runner_impl.h":
                    continue
                include_path = PurePosixPath(included)
                resolved = PurePosixPath(posixpath.normpath((relative.parent / included).as_posix()))
                if include_path.is_absolute() or not resolved.parts or resolved.parts[0] == "..":
                    findings.append(
                        {"kind": "external_source_include", "value": f"{relative}:{included}"}
                    )

    run = (directory / "run.sh").read_text(encoding="utf-8")
    if run != EXPECTED_RUN:
        findings.append({"kind": "frozen_run_script", "value": "run.sh differs from judge template"})
    if run.count('exec "${INSTALL_DIR}/bin/rsm_solution_runner"') != 1:
        findings.append({"kind": "run_wrapper", "value": "run.sh must exec the frozen runner once"})
    if f'bin/rsm_{variant}_runner" "$@"' not in run:
        findings.append({"kind": "run_arguments", "value": "run.sh must forward all arguments unchanged"})
    forbidden_shell = re.compile(r"benchmark[_-]json|mean\.bin|rstd\.bin|logsumexp\.bin|python|sed\s+-i")
    if forbidden_shell.search(run):
        findings.append({"kind": "run_side_effect", "value": "run.sh may only launch the built runner"})

    runner = (directory / "runner" / "main.cpp").read_text(encoding="utf-8")
    include = '#include "../../tools/common/runner/runner_impl.h"'
    if runner.count(include) != 1:
        findings.append({"kind": "frozen_runner_include", "value": include})
    if re.search(r"\bint\s+main\s*\(", runner):
        findings.append({"kind": "custom_main", "value": "runner/main.cpp must use the frozen main"})

    cmake = (directory / "CMakeLists.txt").read_text(encoding="utf-8")
    build = (directory / "build.sh").read_text(encoding="utf-8")
    if build != EXPECTED_BUILD:
        findings.append({"kind": "frozen_build_script", "value": "build.sh differs from judge template"})
    if (match := DANGEROUS_CMAKE.search(cmake)) is not None:
        findings.append({"kind": "cmake_side_effect", "value": match.group(0)})
    cmake_without_comments = re.sub(r"#[^\n]*", "", cmake)
    cmake_structure = re.sub(r'"(?:\\.|[^"\\])*"', '""', cmake_without_comments)
    commands = re.findall(r"\b([A-Za-z_]\w*)\s*\(", cmake_structure)
    for command in commands:
        if command.lower() not in {value.lower() for value in ALLOWED_CMAKE_COMMANDS}:
            findings.append({"kind": "cmake_command", "value": command})
    for variable in re.findall(r"\bset\s*\(\s*([^\s)]+)", cmake, re.I):
        if variable not in ALLOWED_CMAKE_SET_VARIABLES:
            findings.append({"kind": "cmake_set_variable", "value": variable})
    ascendc_sets = re.findall(r"\bset\s*\(\s*ASCENDC_CMAKE_DIR\s+([^\s)]+)", cmake, re.I)
    expected_ascendc_sets = {
        '"${ASCEND_CANN_PACKAGE_PATH}/tools/tikcpp/ascendc_kernel_cmake"',
        '"${ASCEND_CANN_PACKAGE_PATH}/compiler/tikcpp/ascendc_kernel_cmake"',
    }
    if set(ascendc_sets) != expected_ascendc_sets or len(ascendc_sets) != 2:
        findings.append({"kind": "cmake_ascendc_path", "value": "unexpected AscendC module path"})
    package_sets = re.findall(
        r"\bset\s*\(\s*ASCEND_CANN_PACKAGE_PATH\s+(.*?)\)", cmake, re.I | re.S
    )
    if len(package_sets) != 1 or not package_sets[0].lstrip().startswith('"$ENV{ASCEND_HOME_PATH}"'):
        findings.append({"kind": "cmake_cann_path", "value": "unexpected CANN package path"})
    includes = re.findall(r"\binclude\s*\((.*?)\)", cmake, re.I | re.S)
    if includes != ['"${ASCENDC_CMAKE_DIR}/ascendc.cmake"']:
        findings.append({"kind": "cmake_include", "value": "only the frozen AscendC module may be included"})
    installs = re.findall(r"\binstall\s*\((.*?)\)", cmake, re.I | re.S)
    if len(installs) != 1 or re.sub(r"\s+", " ", installs[0]).strip() != (
        "TARGETS rsm_solution_runner RUNTIME DESTINATION bin"
    ):
        findings.append({"kind": "cmake_install", "value": "unexpected install rule"})
    executable = re.search(r"add_executable\s*\(\s*rsm_solution_runner(.*?)\)", cmake, re.S)
    if executable is None or "runner/main.cpp" not in executable.group(1):
        findings.append({"kind": "cmake_runner", "value": "rsm_solution_runner must compile runner/main.cpp"})
    for command, allowed_directories in (
        ("ascendc_library", {"op_kernel"}),
        ("add_executable", {"runner", "op_host"}),
    ):
        for body in re.findall(rf"\b{command}\s*\((.*?)\)", cmake, re.I | re.S):
            sources = re.findall(r"(?<![A-Za-z0-9_])([^\s\"']+\.(?:c|cc|cpp|cxx))\b", body)
            for source in sources:
                source_path = PurePosixPath(source)
                if (
                    source_path.is_absolute()
                    or ".." in source_path.parts
                    or not source_path.parts
                    or source_path.parts[0] not in allowed_directories
                ):
                    findings.append({"kind": "cmake_external_source", "value": source})
    for body in re.findall(r"\btarget_link_directories\s*\((.*?)\)", cmake, re.I | re.S):
        normalized = re.sub(r"\s+", " ", body).strip()
        if normalized != 'rsm_solution_runner PRIVATE "${ASCEND_CANN_PACKAGE_PATH}/lib64"':
            findings.append({"kind": "cmake_link_directory", "value": normalized})
    allowed_include_directories = {
        "${CMAKE_CURRENT_SOURCE_DIR}/op_kernel",
        "${CMAKE_CURRENT_SOURCE_DIR}/op_host",
        "${ASCEND_CANN_PACKAGE_PATH}/include",
    }
    for body in re.findall(r"\btarget_include_directories\s*\((.*?)\)", cmake, re.I | re.S):
        tokens = re.findall(r'"([^\"]+)"|([^\s"\']+)', body)
        flattened = [quoted or plain for quoted, plain in tokens]
        for token in flattened:
            if token in {"rsm_solution_runner", "PRIVATE", "PUBLIC", "INTERFACE"}:
                continue
            if token in allowed_include_directories:
                continue
            if token.startswith("${CMAKE_CURRENT_BINARY_DIR}/include/rsm_solution"):
                continue
            findings.append({"kind": "cmake_include_directory", "value": token})
    for body in re.findall(r"\btarget_link_libraries\s*\((.*?)\)", cmake, re.I | re.S):
        tokens = re.findall(r"[^\s\"']+", body)
        for token in tokens:
            if not ALLOWED_LINK_TOKEN.fullmatch(token):
                findings.append({"kind": "cmake_external_library", "value": token})
    return {"schema_version": 1, "passed": not findings, "findings": findings}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output-json", type=Path)
    args = parser.parse_args()
    result = check(args.root.resolve())
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output_json:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    raise SystemExit(0 if result["passed"] else 1)


if __name__ == "__main__":
    main()
