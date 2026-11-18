# Improved command IDS in NOS3

The transcript asks for a lightweight cFS detector that inspects CCSDS commands on the Software Bus, learns a baseline in scikit-learn, runs inference in C99, and produces a readable audit. This branch implements those requirements for two configured command streams in the NOS3 simulation. The policies and profiles are configurable at build time.

| Requirement | Implementation |
| --- | --- |
| Observe CCSDS commands | Subscribe to the Sample and CI_LAB command MIDs. Decode through the cFE Message API. Check known function codes, declared size, and checksum. |
| Lightweight learned detection | Train two 12-tree Isolation Forests from nominal captures. Export to static C99 and score independent timing histories. Apply a corroborating timing rule and a separate rate ceiling. |
| Explain findings to operators | Record timestamp, MID, PASS/FAIL, and a short reason. Record the input features and score separately. Report failures through cFE Event Services with per-MID rate limiting. |
| Train and inspect | Collect distinct fit, calibration, and validation captures. Rebuild the C99 profiles, verify score parity, and plot the audit. See the [runbook](../README.md). |

## Calibration and validation

The fit capture supplied 110 scored rows per MID and the separate calibration capture supplied 75 per MID. The calibration score cutoffs were 0.684685 for Sample and 0.674180 for CI_LAB. Both profiles require a gap below 179 ms and at least three commands in the preceding second to corroborate a model alert. The independent rate rule flags counts above four in that window. These values describe the lab workload.

An initial development run using a fifth-percentile gap guard still flagged three normal commands per MID. The revised guard uses the fastest gap observed in the fit and calibration captures, with two simulator ticks of tolerance. The final test used a different pacing seed in a fresh cFS process. Those validation rows were excluded from both fitting and calibration.

| Live validation | Commands audited | Timing observations scored | False alerts |
| --- | ---: | ---: | ---: |
| Sample `0x18FA` | 100 | 99 | 0 |
| CI_LAB `0x18E0` | 100 | 99 | 0 |

One command per MID passed the contract during warm-up. The first prototype flagged one of the same 99 scored Sample observations offline. The standalone C detector matched all 198 live timing decisions, and its scores agreed with scikit-learn on all 150 calibration rows to within `5.6e-17`. The NOS3 Linux module occupied 38,117 bytes across text, data, and BSS.

Offline checks covered supported and unsupported command metadata, exact size mismatches, isolated stream histories, sequence wrap, clock regression, idle warm-up, and timing anomalies. Both shipped models flagged an abstract observation with a 170 ms gap and three recent commands. The rate guard flagged five recent commands even when the forest score was below its cutoff. These checks exercised the detector on in-memory feature rows.

## Where the evidence stops

Both live streams used valid NOOPs. The two models learned timing and recent counts because sequence deltas, command sizes, and codes were essentially constant during fitting. This validation supports simulator integration and a limited false-alert comparison. Mission use requires representative nominal captures, calibrated mode-specific profiles, distinct fault and anomaly evaluation, and performance and queue-loss measurements on the target processor. The monitor operates after routing, so link authentication, sender identity, and policy enforcement belong at separate control points.
