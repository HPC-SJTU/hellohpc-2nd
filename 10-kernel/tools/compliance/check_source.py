#!/usr/bin/env python3
"""Reject machine-confirmed source-policy violations.

This is deliberately a supporting control, not the primary anti-specialization
mechanism.  Runtime neighbor probes cover transformations that no source regex can
reliably classify.  The scanner nevertheless constant-folds ordinary C/C++ integer
expressions so hex, shifts, products, and comparison order do not evade exact-shape
matching.  Findings carry the file and line of the matched source so contestants
can act on them; IO names that collide with ordinary identifiers are only rejected
in call position.
"""

from __future__ import annotations

import argparse
import ast
import json
import operator
import re
from dataclasses import dataclass
from pathlib import Path


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"}
REJECT_KINDS = {
    "prohibited_high_level_api",
    "prohibited_contestant_host_io",
    "persistent_output_sentinel",
    "input_tensor_write",
    "case_id",
    "exact_shape_match",
}
TOKEN = re.compile(
    r"0[xX][0-9a-fA-F]+(?:[uUlL]+)?|\d+(?:[uUlL]+)?|"
    r"[A-Za-z_]\w*|==|&&|\|\||<<|>>|->|<=|>=|!=|"
    r"[(){}\[\],;.+\-*/%&|^~<>]"
)
SHAPE_NAMES = {"n": "N", "d": "D", "s": "S"}
PROHIBITED_HIGH_LEVEL_APIS = re.compile(
    r"\b(?:aclnn\w*|atb::\w+|torch_npu|mindspore|"
    r"AscendC::(?:SoftMax\w*|Softmax\w*|LayerNorm\w*|RmsNorm\w*|LogSumExp\w*))\b",
    re.IGNORECASE,
)
# Host IO types and ACL runtime entry points are matched by name in any
# context; only these spellings can declare streams or reach the runtime.
PROHIBITED_CONTESTANT_HOST_IO_TYPES = re.compile(
    r"\b(?:aclrt(?:Memcpy\w*|Malloc\w*|Free|Memset\w*|CreateEvent|EventElapsedTime)|"
    r"std::(?:i|o|f)stream)\b"
)
# Collision-prone C library names are only rejected in call or member-call
# position: "int read = 0;" is a variable, not file IO.  std::remove and
# std::rename additionally get an argument-shape check below, because
# std::remove has both a one-argument <cstdio> file-deletion form and the
# three-argument value algorithm, while std::rename is always the file
# operation.  The runtime neighbor probes cover the residual IO variants no
# name scan can classify.
PROHIBITED_CONTESTANT_HOST_IO_CALLS = re.compile(
    r"\b(fopen|popen|system|fork|exec[lvpe]*|dlopen|openat|open|creat|writev|"
    r"write|pwrite|read|pread|mmap|munmap|syscall|socket|connect|chmod|unlink|"
    r"remove|rename)\s*\("
)
PERSISTENT_OUTPUT_SENTINEL = re.compile(
    r"(?:isnan|isfinite)\s*\([^;{}]{0,160}(?:mean|rstd|logsumexp|lse)|"
    r"(?:mean|rstd|logsumexp|lse)[^;{}]{0,120}GetValue\s*\([^)]*\)[^;{}]{0,80}(?:==|!=)",
    re.IGNORECASE,
)
INPUT_GLOBAL_TENSOR = re.compile(
    r"\b([A-Za-z_]\w*)\s*\.\s*SetGlobalBuffer\s*\(\s*"
    r"reinterpret_cast\s*<\s*__gm__\s+[^>]*\*\s*>\s*\(\s*"
    r"(?:score|x|offsets)\s*\)",
    re.S,
)
BOUNDARIES = {"&&", "||", ";", "{", "}", ","}
ALLOWED_BINOPS = {
    ast.Add: operator.add,
    ast.Sub: operator.sub,
    ast.Mult: operator.mul,
    ast.FloorDiv: operator.floordiv,
    ast.Div: operator.floordiv,
    ast.Mod: operator.mod,
    ast.LShift: operator.lshift,
    ast.RShift: operator.rshift,
    ast.BitOr: operator.or_,
    ast.BitAnd: operator.and_,
    ast.BitXor: operator.xor,
}
ALLOWED_UNARYOPS = {ast.UAdd: operator.pos, ast.USub: operator.neg, ast.Invert: operator.invert}


@dataclass(frozen=True)
class Comparison:
    token_index: int
    dimension: str
    value: int


def _strip_comments_and_literals(text: str) -> str:
    # Blank comments and literals with same-length whitespace instead of
    # deleting them, so offsets in the stripped text still address original
    # source lines for finding diagnostics.
    pattern = re.compile(r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'", re.S)
    return pattern.sub(lambda match: re.sub(r"[^\n]", " ", match.group(0)), text)


def _line(code: str, offset: int) -> int:
    return code.count("\n", 0, offset) + 1


def _call_argument_span(code: str, open_paren_offset: int) -> str | None:
    """Return the text of a call's argument list, or None if unbalanced."""
    depth = 0
    for index in range(open_paren_offset, min(len(code), open_paren_offset + 4000)):
        character = code[index]
        if character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
            if depth == 0:
                return code[open_paren_offset + 1:index]
    return None


def _is_single_argument_call(arguments: str) -> bool:
    """True when the argument list has no top-level comma."""
    depth = 0
    for character in arguments:
        if character in "([":
            depth += 1
        elif character in ")]":
            depth -= 1
        elif character == "," and depth == 0:
            return False
    return True


def _constant(node: ast.AST, constants: dict[str, int]) -> int:
    if isinstance(node, ast.Expression):
        return _constant(node.body, constants)
    if isinstance(node, ast.Constant) and isinstance(node.value, int):
        return int(node.value)
    if isinstance(node, ast.Name) and node.id in constants:
        return constants[node.id]
    if isinstance(node, ast.BinOp) and type(node.op) in ALLOWED_BINOPS:
        return int(ALLOWED_BINOPS[type(node.op)](
            _constant(node.left, constants), _constant(node.right, constants)
        ))
    if isinstance(node, ast.UnaryOp) and type(node.op) in ALLOWED_UNARYOPS:
        return int(ALLOWED_UNARYOPS[type(node.op)](_constant(node.operand, constants)))
    raise ValueError("not an integer constant expression")


def _eval_integer(tokens: list[str], constants: dict[str, int] | None = None) -> int | None:
    if not tokens:
        return None
    expression = "".join(re.sub(r"(?<=[0-9a-fA-F])[uUlL]+$", "", token) for token in tokens)
    expression = expression.replace("/", "//")
    try:
        return _constant(ast.parse(expression, mode="eval"), constants or {})
    except (SyntaxError, ValueError, ZeroDivisionError, OverflowError):
        return None


def _shape_dimension(tokens: list[str]) -> str | None:
    identifiers = [token.lower() for token in tokens if re.fullmatch(r"[A-Za-z_]\w*", token)]
    if not identifiers:
        return None
    final = identifiers[-1]
    return SHAPE_NAMES.get(final)


def _side(tokens: list[str], equality: int, direction: int) -> list[str]:
    values: list[str] = []
    depth = 0
    index = equality + direction
    while 0 <= index < len(tokens):
        token = tokens[index]
        if direction < 0:
            if token in (")", "]"):
                depth += 1
            elif token in ("(", "["):
                if depth == 0:
                    break
                depth -= 1
        else:
            if token in ("(", "["):
                depth += 1
            elif token in (")", "]"):
                if depth == 0:
                    break
                depth -= 1
        if depth == 0 and token in BOUNDARIES:
            break
        values.append(token)
        index += direction
    if direction < 0:
        values.reverse()
    return values


def _integer_constants(code: str) -> dict[str, int]:
    definitions: list[tuple[str, list[str]]] = []
    for match in re.finditer(r"(?m)^\s*#\s*define\s+([A-Za-z_]\w*)\s+([^\n]+)$", code):
        definitions.append((match.group(1), TOKEN.findall(match.group(2))))
    declaration = re.compile(
        r"\b(?:constexpr|const)\s+(?:unsigned\s+|signed\s+)?"
        r"(?:auto|int|long|size_t|u?int(?:8|16|32|64)_t)\s+"
        r"([A-Za-z_]\w*)\s*=\s*([^;]+);"
    )
    for match in declaration.finditer(code):
        definitions.append((match.group(1), TOKEN.findall(match.group(2))))

    constants: dict[str, int] = {}
    # Resolve short dependency chains without interpreting arbitrary C++.
    for _ in range(len(definitions) + 1):
        changed = False
        for name, expression in definitions:
            if name in constants:
                continue
            value = _eval_integer(expression, constants)
            if value is not None:
                constants[name] = value
                changed = True
        if not changed:
            break
    return constants


def _comparisons(code: str) -> tuple[list[str], list[Comparison], list[int]]:
    token_matches = list(TOKEN.finditer(code))
    tokens = [match.group(0) for match in token_matches]
    token_offsets = [match.start() for match in token_matches]
    constants = _integer_constants(code)
    result: list[Comparison] = []
    for index, token in enumerate(tokens):
        if token != "==":
            continue
        lhs, rhs = _side(tokens, index, -1), _side(tokens, index, 1)
        lhs_dimension, rhs_dimension = _shape_dimension(lhs), _shape_dimension(rhs)
        if lhs_dimension and (value := _eval_integer(rhs, constants)) is not None:
            result.append(Comparison(index, lhs_dimension, value))
        elif rhs_dimension and (value := _eval_integer(lhs, constants)) is not None:
            result.append(Comparison(index, rhs_dimension, value))
    return tokens, result, token_offsets


def _conjunction_bounds(tokens: list[str], index: int) -> tuple[int, int]:
    left = index
    while left > 0 and tokens[left - 1] not in {"||", ";", "{", "}"}:
        left -= 1
    right = index
    while right + 1 < len(tokens) and tokens[right + 1] not in {"||", ";", "{", "}"}:
        right += 1
    return left, right


def _initializer_bodies(code: str):
    for match in re.finditer(r"=\s*\{", code):
        begin = code.find("{", match.start())
        depth = 0
        for index in range(begin, len(code)):
            if code[index] == "{":
                depth += 1
            elif code[index] == "}":
                depth -= 1
                if depth == 0:
                    yield code[begin + 1:index], begin
                    break


def scan_source(
    path: Path,
    case_ids: set[str],
    shapes: set[tuple[int, int, int]],
    allow_judge_host_io: bool = False,
) -> list[dict]:
    text = path.read_text(encoding="utf-8", errors="replace")
    code = _strip_comments_and_literals(text)
    findings: list[dict] = []
    for case_id in sorted(case_ids):
        offset = code.find(case_id)
        if offset >= 0:
            findings.append(
                {"file": str(path), "line": _line(code, offset), "kind": "case_id", "value": case_id}
            )
    for literal in re.finditer(r'"(?:\\.|[^"\\])*"', text, re.S):
        if len(literal.group(0)) >= 512:
            findings.append(
                {
                    "file": str(path),
                    "line": _line(text, literal.start()),
                    "kind": "large_string_blob",
                    "count": len(literal.group(0)),
                }
            )
    tokens, comparisons, token_offsets = _comparisons(code)
    conjunctions = {
        _conjunction_bounds(tokens, comparison.token_index)
        for comparison in comparisons
    }
    matched_shapes: dict[tuple[int, int, int], int] = {}
    for left, right in conjunctions:
        values: dict[str, set[int]] = {"S": set(), "N": set(), "D": set()}
        first_offset: int | None = None
        for comparison in comparisons:
            if left <= comparison.token_index <= right:
                values[comparison.dimension].add(comparison.value)
                if first_offset is None:
                    first_offset = token_offsets[comparison.token_index]
        for shape in shapes:
            expected = {"S": shape[0], "N": shape[1], "D": shape[2]}
            if all(expected[dimension] in values[dimension] for dimension in expected):
                matched_shapes.setdefault(shape, first_offset or 0)
    for (s, n, d), offset in sorted(matched_shapes.items()):
        findings.append(
            {
                "file": str(path),
                "line": _line(code, offset),
                "kind": "exact_shape_match",
                "value": {"S": s, "N": n, "D": d},
            }
        )
    for match in PROHIBITED_HIGH_LEVEL_APIS.finditer(code):
        findings.append(
            {
                "file": str(path),
                "line": _line(code, match.start()),
                "kind": "prohibited_high_level_api",
                "value": match.group(0),
            }
        )
    if not allow_judge_host_io:
        for pattern in (PROHIBITED_CONTESTANT_HOST_IO_TYPES, PROHIBITED_CONTESTANT_HOST_IO_CALLS):
            for match in pattern.finditer(code):
                name = match.group(1) if match.lastindex else None
                if name == "remove":
                    arguments = _call_argument_span(code, match.end() - 1)
                    # std::remove(value_first, value_last, value) is the
                    # ordinary algorithm; the one-argument form is <cstdio>
                    # file deletion.
                    if arguments is not None and not _is_single_argument_call(arguments):
                        continue
                findings.append(
                    {
                        "file": str(path),
                        "line": _line(code, match.start()),
                        "kind": "prohibited_contestant_host_io",
                        "value": match.group(0).strip(),
                    }
                )
        if (sentinel := PERSISTENT_OUTPUT_SENTINEL.search(code)) is not None:
            findings.append(
                {
                    "file": str(path),
                    "line": _line(code, sentinel.start()),
                    "kind": "persistent_output_sentinel",
                    "value": "output state gates computation",
                }
            )
        for tensor in set(INPUT_GLOBAL_TENSOR.findall(code)):
            escaped = re.escape(tensor)
            write_patterns = (
                rf"\b(?:AscendC::)?DataCopy(?:Pad)?\s*\(\s*{escaped}\s*\[",
                rf"\b{escaped}\s*\.\s*(?:SetValue|SetAtomic\w*)\s*\(",
                rf"\b{escaped}\s*\[[^]]+\]\s*=",
            )
            for pattern in write_patterns:
                if (match := re.search(pattern, code, re.S)) is not None:
                    findings.append(
                        {
                            "file": str(path),
                            "line": _line(code, match.start()),
                            "kind": "input_tensor_write",
                            "value": tensor,
                        }
                    )
                    break
    # Large literal initializers remain informational diagnostics. They do not affect
    # the binary machine verdict because generic lookup tables can be legitimate.
    for body, body_offset in _initializer_bodies(code):
        literals = re.findall(r"(?<![A-Za-z_])(?:0[xX][0-9a-fA-F]+|\d+(?:\.\d*)?)(?![A-Za-z_])", body)
        if len(literals) >= 256:
            findings.append(
                {
                    "file": str(path),
                    "line": _line(code, body_offset),
                    "kind": "large_literal_blob",
                    "count": len(literals),
                }
            )
    for finding in findings:
        kind = finding["kind"]
        finding["severity"] = (
            "reject" if kind in REJECT_KINDS else "info"
        )
    return findings


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--source-dir", type=Path, action="append", required=True)
    parser.add_argument("--manifest", type=Path, action="append", required=True)
    parser.add_argument("--output-json", type=Path)
    args = parser.parse_args()
    case_ids: set[str] = set()
    shapes: set[tuple[int, int, int]] = set()
    for manifest in args.manifest:
        for case in json.loads(manifest.read_text(encoding="utf-8"))["cases"]:
            case_ids.add(case["case_id"])
            shapes.add((int(case["S"]), int(case["N"]), int(case["D"])))
    findings = []
    for source_dir in args.source_dir:
        allow_judge_host_io = source_dir.resolve() == (args.root.resolve() / "tools" / "common")
        for path in source_dir.rglob("*"):
            if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES:
                findings.extend(scan_source(path, case_ids, shapes, allow_judge_host_io))
    counts = {
        severity: sum(finding["severity"] == severity for finding in findings)
        for severity in ("reject", "info")
    }
    result = {
        "schema_version": 3,
        "passed": counts["reject"] == 0,
        "counts": counts,
        "findings": findings,
    }
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output_json:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    raise SystemExit(0 if result["passed"] else 1)


if __name__ == "__main__":
    main()
