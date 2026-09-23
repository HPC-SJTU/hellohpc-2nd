#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.reference.reference import write_inputs
from tools.correctness.generate_case import generate_case


def generate_suite(manifest: Path, output_root: Path) -> None:
    cases = json.loads(manifest.read_text(encoding="utf-8"))["cases"]
    for spec in cases:
        score, x, offsets, metadata = generate_case(spec)
        write_inputs(output_root / spec["case_id"], score, x, offsets, metadata)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    args = parser.parse_args()
    generate_suite(args.manifest, args.output_root)


if __name__ == "__main__":
    main()
