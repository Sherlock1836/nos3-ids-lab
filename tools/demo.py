"""Build and verify the portable C99 IDS using offline observation fixtures."""

import csv
from io import StringIO
from pathlib import Path
from random import Random
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "ids_app/fsw/src"
OUT = ROOT / "out"


def run(*args, input=None):
    return subprocess.run(args, input=input, text=True, check=True, stdout=subprocess.PIPE).stdout


def main():
    OUT.mkdir(exist_ok=True)
    rng = Random(42)
    time = 0
    observations = []
    for i in range(1250):
        time += rng.randint(225, 950)
        observations.append(f"{time} {(16300 + i) % 16384} 8 0\n")
    replay = ROOT / "tools/ids_replay.c"
    compile_c = ["gcc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(OUT), "-I", str(SOURCE)]
    features_exe = OUT / "ids_features.exe"
    detector_exe = OUT / "ids_replay.exe"
    run(*compile_c, "-DIDS_FEATURES_ONLY", replay, SOURCE / "ids_features.c", "-o", features_exe)
    edge_cases = "0 16383 8 0\n200 0 8 0\n400 16383 8 0\n1400 0 8 0\n100 0 8 0\n200 1 8 0\n"
    edge_rows = list(csv.reader(StringIO(run(features_exe, input=edge_cases))))[1:]
    if [tuple(row[:2] + row[4:]) for row in edge_rows] != [
        ("1", "200", "2"), ("-1", "200", "3"), ("1", "1000", "1"), ("1", "100", "2")
    ]:
        raise RuntimeError("feature wrap, window expiry, or clock-reset regression")

    nominal = OUT / "synthetic_nominal_features.csv"
    reference = OUT / "model_reference.csv"
    nominal.write_text(run(features_exe, input="".join(observations[:1000])), encoding="ascii")
    print(run(sys.executable, ROOT / "tools/train.py", nominal, OUT / "ids_model.h", "--reference", reference), end="")
    run(*compile_c, replay, SOURCE / "ids_features.c", "-lm", "-o", detector_exe)

    # Compare the compiled scorer with scikit-learn on every held-out observation.
    checker = OUT / "check_model.c"
    checker.write_text(
        '#include <stdio.h>\n#include <ids_model.h>\n'
        'int main(void) {\n'
        '    float x[5];\n'
        '    while (scanf("%f,%f,%f,%f,%f", &x[0], &x[1], &x[2], &x[3], &x[4]) == 5)\n'
        '        printf("%.17g\\n", IDS_ModelScore(x));\n'
        '    return 0;\n'
        '}\n',
        encoding="ascii",
    )
    model_exe = OUT / "check_model.exe"
    run(*compile_c, checker, "-lm", "-o", model_exe)
    with reference.open(newline="") as stream:
        expected = list(csv.reader(stream))
    calculated = run(model_exe, input="".join(",".join(row[:5]) + "\n" for row in expected))
    error = max(abs(float(c) - float(r[-1])) for c, r in zip(calculated.splitlines(), expected, strict=True))
    if error > 1e-12:
        raise RuntimeError(f"C99 score mismatch: maximum error {error:g}")

    # Off-nominal timing fixture following a separate nominal session.
    time += 15000
    observations = observations[1000:] + [f"{time + i} {1250 + i - 84} 8 0\n" for i in range(8)]
    audit = OUT / "ids_demo_audit.csv"
    results = run(detector_exe, input="".join(observations))
    audit.write_text(results, encoding="ascii")
    rows = list(csv.DictReader(StringIO(results)))
    nominal_flags = sum(row["status"] == "FAIL" for row in rows[:-8])
    fixture_flags = sum(row["status"] == "FAIL" for row in rows[-8:])
    print(f"C99/Python score parity: {len(expected)} rows, max error {error:.2g}")
    print(f"Offline replay: {nominal_flags}/250 nominal and {fixture_flags}/8 timing fixture rows flagged")
    if not nominal_flags < 25 or not fixture_flags or len(rows) != 258:
        raise RuntimeError("unexpected offline replay result")
    print(audit)


if __name__ == "__main__":
    main()
