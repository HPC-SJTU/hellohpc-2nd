#!/usr/bin/env python3
"""Publish the sum of the final elapsed marker from each completed input."""

import json
import math
import os
from pathlib import Path
import re


MARKER = re.compile(
    r"FULL_ANALYZE_STEP\t[0-9]+\tEVENT\t[0-9]+\tPLAYER\t[0-3]"
    r"\tELAPSED_SECONDS\t([^\t]+)\tSTEP_SECONDS\t[^\t]+"
)


def elapsed(path):
    last = None
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            match = MARKER.fullmatch(line.rstrip("\n"))
            if match:
                last = float(match.group(1))
    if last is None or not math.isfinite(last) or last <= 0:
        raise ValueError(f"{path}: no positive finite final elapsed marker")
    return last


def main():
    samples = [elapsed(Path(f"game{game}.err")) for game in (1, 2)]
    total = math.fsum(samples)
    if not math.isfinite(total):
        raise ValueError("total elapsed time must be finite")
    outputs = {
        "elapsed": total,
        "samples": samples,
        "message": "Public self-test only. The official evaluation is authoritative.",
    }
    Path(os.environ["HELLOHPC_OUTPUT"]).write_text(
        json.dumps({"outputs": outputs}, allow_nan=False) + "\n", encoding="utf-8"
    )


if __name__ == "__main__":
    main()
