"""Select distinct physical cores from the current Linux affinity mask."""
import argparse
import json
import os
from pathlib import Path


def physical_cpus(available):
    selected, seen = [], set()
    for cpu in sorted(available):
        base = Path(f"/sys/devices/system/cpu/cpu{cpu}/topology")
        key = ((base / "physical_package_id").read_text().strip(),
               (base / "core_id").read_text().strip())
        if key not in seen:
            seen.add(key)
            selected.append(cpu)
    return selected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--count", type=int, required=True)
    parser.add_argument("--allow-fewer", action="store_true")
    args = parser.parse_args()
    if not 1 <= args.count <= 32:
        parser.error("count must be between 1 and 32")
    cpus = physical_cpus(os.sched_getaffinity(0))
    if not cpus or (len(cpus) < args.count and not args.allow_fewer):
        parser.error(f"need {args.count} physical cores, only {len(cpus)} available; "
                     "use 'hellohpc test --case sample' on smaller machines")
    cpus = cpus[:args.count]
    payload = {"outputs": {"count": len(cpus), "cpus": cpus,
                            "list": ",".join(map(str, cpus))}}
    Path(os.environ["HELLOHPC_OUTPUT"]).write_text(json.dumps(payload) + "\n")


if __name__ == "__main__":
    main()
