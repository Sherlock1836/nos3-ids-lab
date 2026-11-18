"""Summarize an IDS audit CSV and optionally plot the inspection timeline."""

import argparse
from collections import Counter
import csv
from pathlib import Path


def summarize_nominal(parser, audit, feature_path):
    """Associate features with a same-session audit for known nominal traffic."""
    with feature_path.open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        columns = ["seq_delta", "delta_t_ms", "packet_size", "function_code", "packets_last_second"]
        if reader.fieldnames == ["timestamp", "message_id", *columns, "model_score", "decision"]:
            summarize_profiles(parser, audit, list(reader))
            return
        if reader.fieldnames != columns:
            parser.error(f"expected feature header {','.join(columns)}")
        features = list(reader)

    if len({row["message_id"] for row in audit}) != 1:
        parser.error("nominal feature analysis requires a single MID")
    # An empty note is a scored pass. Warm-up and header errors have separate notes.
    scored = [row for row in audit if not row["note"] or row["note"].startswith("Isolation Forest score ")]
    if len(scored) != len(features):
        parser.error("audit and feature counts disagree. Export both from the same capture")

    prior_ms = None
    packets = iter(features)
    failures = []
    for row in audit:
        try:
            current_ms = round(float(row["timestamp"]) * 1000)
            note = row["note"]
            if note.startswith("Warm-up"):
                prior_ms = current_ms
                continue
            if note and not note.startswith("Isolation Forest score "):
                continue
            values = next(packets)
            gap = int(values["delta_t_ms"])
            count = int(values["packets_last_second"])
            if prior_ms is not None and current_ms - prior_ms != gap:
                parser.error("audit timestamps and feature gaps disagree. Check capture order")
            prior_ms = current_ms
        except (TypeError, ValueError, OverflowError):
            parser.error("nominal captures require numeric timestamps, gaps, and window counts")
        if row["status"] == "FAIL":
            failures.append((row["timestamp"], gap, count))

    print(f"Nominal model flags: {len(failures)}/{len(scored)} scored")
    print(f"Other observations excluded from model rate: {len(audit) - len(scored)}")
    for timestamp, gap, count in failures:
        print(f"  MET {timestamp} s | gap {gap} ms | {count} packets in the last second")


def summarize_profiles(parser, audit, features):
    expected = []
    for row in audit:
        note = row["note"]
        if not note or note.startswith(("Receive rate ", "Timing outlier ", "Command contract passed.")):
            expected.append((row["timestamp"], row["message_id"], row["status"]))
    statuses = {"PASS": "PASS", "RATE": "FAIL", "OUTLIER": "FAIL", "UNCALIBRATED": "PASS"}
    actual = [(row["timestamp"], row["message_id"], statuses.get(row["decision"])) for row in features]
    if expected != actual:
        parser.error("audit and feature decisions disagree. Export both from the same capture")

    counts = Counter((row["message_id"], row["decision"]) for row in features)
    for mid in sorted({row["message_id"] for row in audit}):
        flagged = counts[mid, "RATE"] + counts[mid, "OUTLIER"]
        print(f"{mid} nominal timing flags: {flagged}/{counts[mid, 'PASS'] + flagged} scored")
        if counts[mid, "UNCALIBRATED"]:
            print(f"  {counts[mid, 'UNCALIBRATED']} uncalibrated observations")
        for row in features:
            if row["message_id"] == mid and row["decision"] in {"RATE", "OUTLIER"}:
                print(f"  MET {row['timestamp']} s | {row['decision']} | "
                      f"gap {row['delta_t_ms']} ms | {row['packets_last_second']} packets in the last second")
    print(f"Warm-up and command-contract failures excluded from timing rate: {len(audit) - len(features)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("audit", type=Path)
    parser.add_argument("--plot", type=Path)
    parser.add_argument("--nominal-features", type=Path, help="Feature CSV from the same known nominal capture")
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

    if args.nominal_features:
        summarize_nominal(parser, rows, args.nominal_features)

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
