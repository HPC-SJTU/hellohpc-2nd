#!/usr/bin/env python3
"""submit.py — package & validate a player Skill submission (proposal §四/§十二.1).

A submission is:
    submission/
    ├── submission.yaml       (operator and model)
    ├── SKILL.md
    └── references/            (optional)
        └── **/*.md             (any number, total budget applies)

Static checks (proposal §12.1 / §4):
  * file types: submission.yaml, SKILL.md and markdown refs; nothing else
  * NO source files/binaries/symlinks/archives/dotfiles
  * exact count under the pinned public scoring tokenizer; total <= 1,200
  * reject invalid UTF-8 and high-entropy payloads
  * flag test-bypass / reference-copying / prompt-injection phrases for review

Produces a validation report (console + optional JSON) and, if valid, a
normalized submission bundle (a .tar.gz of just the allowed files
is NOT created — archives are disallowed on the submit side; instead we copy the
validated tree to <out>/ and write the report there).

Usage:
    python3 submit.py <submission_dir> [--out submission_bundle] [--json report.json]
    python3 submit.py . --repository-layout
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional

from src.runner import submission_surface


# --- limits (proposal §4.2) ------------------------------------------------- #
TOTAL_BYTES_LIMIT = submission_surface.TECHNICAL_TOTAL_BYTES_CAP
SOFT_TOKEN_LIMIT = submission_surface.SOFT_TOKEN_LIMIT
HARD_TOKEN_LIMIT = submission_surface.HARD_TOKEN_LIMIT
# Compatibility name for external consumers; there are no per-file/count limits.
TOTAL_TOKEN_LIMIT = HARD_TOKEN_LIMIT


# suspicious phrases (test bypass / reference copying / prompt injection)
SUSPICIOUS_PHRASES = [
    r"private/", r"reference\s+kernel", r"golden",
    r"bypass", r"skip\s+the\s+check", r"disable\s+(the\s+)?(check|sanitizer)",
    r"cat\s+.*(private|reference)",
    r"ignore\s+(all\s+)?(previous|prior)\s+instructions", r"system\s+prompt",
    r"exfiltrat", r"\.\./\.\.", r"/etc/passwd", r"curl\s+", r"wget\s+",
    r"hardcode", r"memoi[sz]e\s+the\s+(output|answer)", r"precomputed\s+result",
]



@dataclass
class FileReport:
    path: str
    size_bytes: int
    approx_tokens: int
    issues: List[str] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)


@dataclass
class Report:
    submission_dir: str
    files: List[FileReport] = field(default_factory=list)
    total_bytes: int = 0
    total_tokens: int = 0
    errors: List[str] = field(default_factory=list)     # hard rejects
    warnings: List[str] = field(default_factory=list)    # manual-review flags
    valid: bool = False
    operator: Optional[str] = None
    model: Optional[str] = None
    reasoning_effort: str = submission_surface.REASONING_EFFORT
    token_count_complete: bool = False

    soft_token_limit: int = SOFT_TOKEN_LIMIT
    hard_token_limit: int = HARD_TOKEN_LIMIT
    hard_limit_remaining: int = HARD_TOKEN_LIMIT
    length_adjustment: str = "unknown"
    length_multiplier: float = 1.0
    penalized: bool = False
    tokenizer_sha256: str = submission_surface.TOKENIZER_SHA256
    length_rejected: bool = False
    surface_sha256: Optional[str] = None
    surface_snapshot: Optional[submission_surface.SubmissionSurface] = field(
        default=None, repr=False
    )



def shannon_entropy(s: str) -> float:
    if not s:
        return 0.0
    counts = Counter(s)
    n = len(s)
    return -sum((c / n) * math.log2(c / n) for c in counts.values())


def find_high_entropy_tokens(text: str, min_len: int = 24, thr: float = 4.2) -> List[str]:
    """Flag long high-entropy tokens that look like encoded/obfuscated payloads.

    Genuine base64/hex blobs have no separators and a high alnum-run length. We
    skip tokens that are just dictionary-ish words joined by / - _ (common in
    prose containing ordinary slash-separated words) by requiring a long
    UNBROKEN alnum run and a digit+letter mix, which real payloads have but
    word-lists do not."""
    flagged = []
    for tok in re.findall(r"[A-Za-z0-9+/=_\-]{%d,}" % min_len, text):
        # longest run with no separator
        longest_run = max((len(r) for r in re.split(r"[/\-_+=]", tok)), default=0)
        if longest_run < 20:
            continue  # separated word-list, not a blob
        has_digit = any(c.isdigit() for c in tok)
        has_alpha = any(c.isalpha() for c in tok)
        if shannon_entropy(tok) >= thr and has_digit and has_alpha:
            flagged.append(tok[:40] + ("..." if len(tok) > 40 else ""))
    return flagged


# --------------------------------------------------------------------------- #
# per-file validation
# --------------------------------------------------------------------------- #
def check_file(
    item: submission_surface.SurfaceFile, is_skill: bool
) -> FileReport:
    fr = FileReport(
        path=item.path,
        size_bytes=item.source_size_bytes,
        approx_tokens=item.estimated_tokens,
    )
    text = item.text

    if is_skill:
        _check_skill_frontmatter(text, fr)

    for pat in SUSPICIOUS_PHRASES:
        if re.search(pat, text, re.IGNORECASE):
            fr.warnings.append(f"suspicious phrase: /{pat}/")

    ents = find_high_entropy_tokens(text)
    if ents:
        fr.issues.append(f"high-entropy token(s): {', '.join(ents[:3])}")

    return fr


def _check_skill_frontmatter(text: str, report: FileReport) -> None:
    try:
        submission_surface.validate_skill_metadata(text)
    except submission_surface.SubmissionSurfaceError as exc:
        report.issues.append(str(exc))


# --------------------------------------------------------------------------- #
# submission validation
# --------------------------------------------------------------------------- #
def validate(submission_dir: Path, *, repository_layout: bool = False) -> Report:
    supplied = Path(os.path.abspath(os.fspath(submission_dir)))
    rep = Report(submission_dir=str(supplied))
    try:
        surface = submission_surface.snapshot_submission_surface(
            supplied, repository_layout=repository_layout
        )
    except submission_surface.SubmissionLengthError as exc:
        rep.total_tokens = exc.estimated_tokens
        rep.hard_limit_remaining = 0
        rep.length_adjustment = "rejected"
        rep.length_multiplier = 0.0
        rep.length_rejected = True
        rep.errors.append(str(exc))
        return rep
    except submission_surface.SubmissionSurfaceError as exc:
        rep.errors.append(str(exc))
        return rep

    rep.submission_dir = str(surface.root)
    rep.surface_snapshot = surface
    rep.total_bytes = surface.total_bytes
    rep.total_tokens = surface.estimated_tokens
    rep.token_count_complete = True
    rep.hard_limit_remaining = surface.length.hard_limit_remaining
    rep.length_adjustment = surface.length.adjustment
    rep.length_multiplier = surface.length.length_multiplier
    rep.length_rejected = surface.length.rejected
    rep.penalized = surface.length.penalized
    rep.tokenizer_sha256 = surface.tokenizer_sha256
    rep.surface_sha256 = surface.sha256
    rep.files.append(FileReport(
        path="submission.yaml",
        size_bytes=surface.total_bytes - sum(item.source_size_bytes for item in surface.files),
        approx_tokens=0,
    ))

    for item in surface.files:
        fr = check_file(item, item.path == "SKILL.md")
        rep.files.append(fr)
        for issue in fr.issues:
            rep.errors.append(f"{fr.path}: {issue}")
        for warning in fr.warnings:
            rep.warnings.append(f"{fr.path}: {warning}")

    if surface.length.rejected:
        rep.errors.append(
            f"estimated token count {rep.total_tokens} exceeds hard limit "
            f"{HARD_TOKEN_LIMIT}"
        )

    rep.valid = not rep.errors
    if rep.valid:
        rep.operator = surface.operator
        rep.model = surface.model
    return rep


def bundle(submission_dir: Path, out_dir: Path, rep: Report) -> None:
    """Materialize the exact validated snapshot without re-reading its source."""
    if not rep.valid or rep.surface_snapshot is None:
        raise ValueError("cannot bundle an invalid or incomplete submission report")
    submission_surface.materialize_surface(rep.surface_snapshot, Path(out_dir))


def print_report(rep: Report) -> None:
    print(f"\n{'='*66}")
    print(f"SUBMISSION VALIDATION: {rep.submission_dir}")
    print(f"{'='*66}")
    print(f"  operator: {rep.operator or 'invalid'}")
    print(f"  model: {rep.model or 'invalid'}   reasoning_effort: {rep.reasoning_effort}")
    print(f"  files: {len(rep.files)}   total_size: {rep.total_bytes} B")
    token_label = "estimated_tokens" if rep.token_count_complete else "estimated_tokens_at_least"
    print(
        f"  {token_label}: {rep.total_tokens}   "
        f"soft_limit: {rep.soft_token_limit}   hard_limit: {rep.hard_token_limit}"
    )
    print(
        f"  hard_limit_remaining: {rep.hard_limit_remaining}   "
        f"adjustment: {rep.length_adjustment}   "
        f"length_penalty: {'yes' if rep.penalized else 'no'}   "
        f"rejected_by_length: {'yes' if rep.length_rejected else 'no'}"
    )
    print(f"  length_multiplier: {rep.length_multiplier:.6f}")
    print(f"  tokenizer_json_sha256: {rep.tokenizer_sha256}")
    print(
        "  compressed_asset_sha256: "
        f"{submission_surface.TOKENIZER_COMPRESSED_SHA256}"
    )
    for fr in rep.files:
        print(
            f"    - {fr.path:<40} {fr.size_bytes:>6} B  "
            f"{fr.approx_tokens:>5} tok"
        )
    if rep.errors:
        print("\n  ERRORS (reject):")
        for e in rep.errors:
            print(f"    ✗ {e}")
    if rep.warnings:
        print("\n  WARNINGS (manual review):")
        for w in rep.warnings:
            print(f"    ! {w}")
    verdict = "VALID" if rep.valid else "REJECTED"
    manual = "  (flagged for manual review)" if rep.valid and rep.warnings else ""
    print(f"\n  VERDICT: {verdict}{manual}")


def report_to_dict(rep: Report) -> dict:
    return {
        "submission_dir": rep.submission_dir,
        "valid": rep.valid,
        "operator": rep.operator,
        "model": rep.model,
        "reasoning_effort": rep.reasoning_effort,
        "total_bytes": rep.total_bytes,
        "total_tokens_est": rep.total_tokens,
        "token_count_complete": rep.token_count_complete,
        "surface_sha256": rep.surface_sha256,
        "scoring_tokenizer": {
            "engine": submission_surface.SCORING_TOKENIZER_ENGINE,
            "tokenizer_json_sha256": rep.tokenizer_sha256,
            "compressed_asset_sha256": submission_surface.TOKENIZER_COMPRESSED_SHA256,
        },
        "length": {
            "soft_limit": rep.soft_token_limit,
            "hard_limit": rep.hard_token_limit,
            "hard_limit_remaining": rep.hard_limit_remaining,
            "adjustment": rep.length_adjustment,
            "length_multiplier": rep.length_multiplier,
            "penalized": rep.penalized,
            "rejected": rep.length_rejected,
        },
        "errors": rep.errors,
        "warnings": rep.warnings,
        "files": [
            {
                "path": f.path,
                "size_bytes": f.size_bytes,
                "estimated_tokens": f.approx_tokens,
                "approx_tokens": f.approx_tokens,
                "issues": f.issues,
                "warnings": f.warnings,
            }
            for f in rep.files
        ],
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="validate & package a Skill submission")
    ap.add_argument("submission_dir", help="path to submission dir (submission.yaml + SKILL.md + references/)")
    ap.add_argument("--out", default=None, help="write validated bundle to this dir")
    ap.add_argument("--json", default=None, help="write JSON report to this path")
    ap.add_argument(
        "--repository-layout",
        action="store_true",
        help="validate submission.yaml, SKILL.md and references/ while ignoring other problem files",
    )
    args = ap.parse_args()

    rep = validate(Path(args.submission_dir), repository_layout=args.repository_layout)
    print_report(rep)

    if args.json:
        with open(args.json, "w") as f:
            json.dump(report_to_dict(rep), f, indent=2, ensure_ascii=False)
        print(f"\n  wrote JSON report: {args.json}")

    if args.out:
        if rep.valid:
            bundle(Path(args.submission_dir), Path(args.out), rep)
            print(f"  bundled -> {args.out}")
        else:
            print("  not bundled (submission REJECTED; fix errors first)")

    return 0 if rep.valid else 1


if __name__ == "__main__":
    raise SystemExit(main())
