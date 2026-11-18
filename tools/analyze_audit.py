"""Summarize an IDS audit CSV and optionally plot the inspection timeline."""

import argparse
from collections import Counter
import csv
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("audit", type=Path)
    parser.add_argument("--plot", type=Path)
    args = parser.parse_args()
    with args.audit.open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["timestamp", "message_id", "status", "note"]:
            parser.error("expected timestamp,message_id,status,note")
        rows = list(reader)
    if not rows:
        print("The audit contains no inspections.")
        return
    if any(row["status"] not in {"PASS", "FAIL"} for row in rows):
        parser.error("every status must be PASS or FAIL")

    counts = Counter((row["message_id"], row["status"]) for row in rows)
    print("MID       PASS    FAIL    FAIL %")
    mids = sorted({row["message_id"] for row in rows})
    positions = {mid: index for index, mid in enumerate(mids)}
    for mid in mids:
        passed, failed = counts[mid, "PASS"], counts[mid, "FAIL"]
        print(f"{mid:8} {passed:5}   {failed:5}   {failed / (passed + failed):7.1%}")

    if args.plot:
        from matplotlib import pyplot as plt

        fig, axis = plt.subplots(figsize=(9, max(2.5, len(mids) * 0.7)), layout="constrained")
        for status, color, marker in [("PASS", "#296a77", "|"), ("FAIL", "#a13a21", "x")]:
            selected = [row for row in rows if row["status"] == status]
            axis.scatter(
                [float(row["timestamp"]) for row in selected],
                [positions[row["message_id"]] for row in selected],
                color=color, marker=marker, label=status, s=42, alpha=0.8,
            )
        axis.set(yticks=range(len(mids)), yticklabels=mids, ylim=(-0.5, len(mids) - 0.5),
                 xlabel="Mission elapsed time (seconds)", ylabel="Message ID",
                 title="IDS audit timeline")
        axis.grid(axis="x", alpha=0.22)
        axis.legend(loc="upper right")
        fig.savefig(args.plot, dpi=160)
        plt.close(fig)


if __name__ == "__main__":
    main()
