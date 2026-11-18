# Nominal validation across live runs

The deployed C99 forest and its threshold stayed fixed. All commands were valid Sample NOOPs on MID `0x18FA`. The second run used a different pacing seed and continued the running simulation after an idle interval.

| Nominal sample | Scored | Flags | Observed rate |
| --- | ---: | ---: | ---: |
| Chronological holdout from the training capture | 48 | 0 | 0% |
| First post-deployment run | 29 | 2 | 6.9% |
| Later post-deployment run | 60 | 2 | 3.3% |

The post-deployment runs produced four false positives across 89 scored commands. One header-only warm-up observation was excluded. The failed checks followed inter-arrival gaps of 199, 300, 201, and 199 ms, with two to four commands in the preceding second. The first observation in the later run passed after roughly 16 minutes of inactivity. The training data held sequence delta, packet size, and function code constant. Every split in the exported forest uses inter-arrival time or rolling command count.

The results show recurring timing-related false positives even within this single command type. They support evaluation with coverage across command cadence and mission modes before selecting a threshold. The idle result is a single observation and does not establish detection behavior across quiet periods.

For each known nominal capture, export both CSVs and run the following from the repository root. The analyzer checks row counts and verifies alignment against the recorded mission elapsed time. It excludes warm-up and header-only failures from the scored denominator and prints the timing features for each false positive.

```powershell
.\.venv\Scripts\python.exe tools/analyze_audit.py out/ids_live_audit.csv --nominal-features out/ids_live_features.csv
```
