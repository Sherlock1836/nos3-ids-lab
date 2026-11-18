"""Check the compiled profiles against Python scores and live NOS3 decisions."""

import argparse
from collections import Counter
import csv
from pathlib import Path
import subprocess
import tempfile

from train import COLUMNS

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "ids_app/fsw/src"


def run_checks(executable, rows):
    stdin = "".join(",".join(row) + "\n" for row in rows)
    result = subprocess.run([executable], input=stdin, text=True, check=True, capture_output=True)
    return list(csv.reader(result.stdout.splitlines()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("live", type=Path)
    args = parser.parse_args()
    with args.reference.open(newline="", encoding="ascii") as stream:
        reference = list(csv.reader(stream))
    with args.live.open(newline="", encoding="utf-8-sig") as stream:
        live = list(csv.DictReader(stream))
    if not reference or {row["message_id"] for row in live} != {"0x18FA", "0x18E0"}:
        parser.error("expected calibration scores and live rows for both MIDs")

    with tempfile.TemporaryDirectory() as work:
        executable = Path(work) / "check-profiles.exe"
        subprocess.run(["gcc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-I", SOURCE,
                        ROOT / "tools/check_profiles.c", SOURCE / "ids_detector.c", SOURCE / "ids_features.c",
                        "-lm", "-o", executable], check=True)
        predictions = run_checks(executable, [row[:6] for row in reference])
        error = max(abs(float(c[0]) - float(p[-1])) for c, p in zip(predictions, reference, strict=True))
        if error > 1e-12:
            raise RuntimeError(f"C99/Python score mismatch {error:g}")
        predictions = run_checks(executable, [[row["message_id"], *[row[col] for col in COLUMNS]] for row in live])

    counts = Counter()
    for row, (score, decision, legacy_flag) in zip(live, predictions, strict=True):
        if row["decision"] != decision or abs(float(score) - float(row["model_score"])) > 5.000001e-7:
            raise RuntimeError(f"C99 replay did not match NOS3 at {row['timestamp']} on {row['message_id']}")
        mid = row["message_id"]
        counts[mid, "scored"] += 1
        counts[mid, "flags"] += decision != "PASS"
        counts[mid, "legacy flags"] += int(legacy_flag)
    print("Offline contract, independent history, clock, idle, and rate fixtures passed")
    print(f"C99/Python parity on {len(reference)} calibration scores, max error {error:.2g}")
    print(f"C99 replay matched {len(live)} live decisions and logged scores")
    for mid in ["0x18FA", "0x18E0"]:
        print(f"{mid}: {counts[mid, 'flags']}/{counts[mid, 'scored']} nominal timing flags")
    print(f"First prototype on the same scored Sample observations: {counts['0x18FA', 'legacy flags']} flags")


if __name__ == "__main__":
    main()
