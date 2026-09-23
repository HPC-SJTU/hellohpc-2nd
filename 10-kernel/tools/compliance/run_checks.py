#!/usr/bin/env python3
"""Run the same machine-verifiable compliance checks the judge runs.

These are the mechanism-only halves of the official gates: source policy
scanning and submission layout checking against the public manifest.  They
use no hidden data, so contestants can reproduce every compliance verdict
locally before packing a submission.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.compliance.check_source import scan_source
from tools.compliance.check_submission_layout import check as check_layout

ROOT = Path(__file__).resolve().parents[2]

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, action="append",
                        default=[ROOT / "tools/data/public_cases.json"])
    parser.add_argument("--output-json", type=Path)
    args = parser.parse_args()
    manifests = [path if path.is_absolute() else ROOT / path for path in args.manifest]

    case_ids: set[str] = set()
    shapes: set[tuple[int, int, int]] = set()
    for manifest in manifests:
        for case in json.loads(manifest.read_text(encoding="utf-8"))["cases"]:
            case_ids.add(case["case_id"])
            shapes.add((int(case["S"]), int(case["N"]), int(case["D"])))

    source_findings: list[dict] = []
    for path in sorted((ROOT / "solution").rglob("*")):
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES:
            source_findings.extend(scan_source(path, case_ids, shapes))
    layout_findings = check_layout(ROOT)["findings"]
    # Mirror the judge verdict: only reject-severity findings fail the check.
    # Informational diagnostics (e.g. large literal lookup tables) are shown
    # but never fail a submission.
    reject_findings = [
        finding for finding in source_findings if finding.get("severity") == "reject"
    ]
    info_findings = [
        finding for finding in source_findings if finding.get("severity") != "reject"
    ]
    passed = not reject_findings and not layout_findings
    result = {
        "schema_version": 1,
        "passed": passed,
        "reject_findings": reject_findings,
        "info_findings": info_findings,
        "layout_findings": layout_findings,
    }
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output_json:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    if not passed:
        print("compliance checks FAILED; see findings above", file=sys.stderr)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
