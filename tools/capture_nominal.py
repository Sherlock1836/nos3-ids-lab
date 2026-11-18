"""Capture a paced NOOP session for both approved lab MIDs from inside Docker."""

import argparse
from collections import Counter
import csv
import io
from pathlib import Path
import re
import subprocess
import sys
import time

RAM = Path("/dev/shm/osal:RAM")
FILES = {"audit": RAM / "ids_audit_log.csv", "features": RAM / "ids_features.csv"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("name", help="Unique prefix for the output files in /out")
    parser.add_argument("--count", type=int, default=90)
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--sequence", type=int, default=0)
    args = parser.parse_args()
    if not re.fullmatch(r"[a-zA-Z0-9_-]+", args.name):
        parser.error("name must contain only letters, digits, underscores, or hyphens")
    if not 1 <= args.count <= 500 or not 0 <= args.sequence < 16384:
        parser.error("count must be 1-500 and sequence must be 0-16383")
    destinations = {kind: Path("/out") / f"{args.name}_{kind}.csv" for kind in FILES}
    if any(path.exists() for path in destinations.values()):
        parser.error("choose an unused capture name")

    time.sleep(6)  # Expire the five-second history before any new observations.
    before = {kind: path.read_text(encoding="ascii") for kind, path in FILES.items()}
    subprocess.run([sys.executable, Path(__file__).with_name("sample_noop.py"),
                    "--target", "both", "--count", str(args.count), "--seed", str(args.seed),
                    "--sequence", str(args.sequence)], check=True)

    elapsed = 0
    while elapsed < 5:
        audit = FILES["audit"].read_text(encoding="ascii")
        if audit.count("\n") >= before["audit"].count("\n") + 2 * args.count:
            break
        time.sleep(0.2)
        elapsed += 0.2
    after = {"audit": audit, "features": FILES["features"].read_text(encoding="ascii")}
    captures = {}
    for kind, head in before.items():
        if not head or not after[kind].startswith(head):
            parser.error(f"{kind} log changed during capture. Check for a reset")
        captures[kind] = head.splitlines()[0] + "\n" + after[kind][len(head):]
    counts = Counter(row["message_id"] for row in csv.DictReader(io.StringIO(captures["audit"])))
    expected = {"0x18FA": args.count, "0x18E0": args.count}
    if counts != expected:
        parser.error(f"expected {expected} in the audit but captured {dict(counts)}")
    for kind, destination in destinations.items():
        with destination.open("x", encoding="ascii", newline="\n") as stream:
            stream.write(captures[kind])
        print(destination)


if __name__ == "__main__":
    main()
