# NOS3 command anomaly monitor

This workspace runs a passive cFS app in NASA NOS3's headless DeepSpace scenario. The app observes the Sample command MID `0x18FA` on the Software Bus. The lab runs cFS, NOS Engine, the NOS time driver, 42 dynamics, and the Sample simulator in the local `nos3-ids-lab` Docker container. Sample commands enter via CI_LAB on container loopback. The headless launcher disables the unused TO_LAB and radio IPC endpoints. It publishes no host ports.

## Review of the transcript

- **The basic design is appropriate for a prototype.** Passive Software Bus observation, offline training, compiled inference, and an operator audit trail make a reasonable first milestone. A Software Bus subscription sees copies of selected messages. Subsystem apps can execute independently of when the IDS scores them. Link authentication, command authorization, prevention, and recovery require separate controls. See the [cFE Application Developer’s Guide](https://github.com/nasa/cFE/blob/dev/docs/cFE%20Application%20Developers%20Guide.md).
- **The supplied feature code would teach the model artifacts.** The “window rate” grows for the lifetime of the process, the first observation gets invented timing values, and the sequence wrap correction covers only one direction. The implementation uses a capped one-second rolling count, signed 14-bit sequence deltas, and a warm-up record before scoring. Time is cFS mission elapsed time at IDS receipt. It reflects scheduling and queueing inside the sim.
- **The original scoring logic and threshold need correction.** The initial stub is additive hand-written rules. With a threshold of 0.75, neither individual rule can flag a packet, and the sequence rule misses a zero delta. The trained model uses 16 isolation trees, full-precision split thresholds, and a threshold calibrated on a time-ordered nominal split. A failure note reports the score and threshold. The score is an outlier measure and cannot identify intent or establish an attack's cause. See [scikit-learn IsolationForest](https://scikit-learn.org/stable/modules/generated/sklearn.ensemble.IsolationForest.html).
- **The transcript is missing a usable route from capture to validation.** The four-column audit CSV is for inspection. The separate feature CSV contains the actual inputs required for training. The trainer uses 60% of rows to fit, 20% to calibrate, and 20% to report a nominal holdout result. Capture curated nominal sessions in time order before training. A baseline of NOOP commands can exercise timing and queue behavior. Mission validation needs representative command types, resets, batching, and link conditions. A feature held constant in training cannot produce a tree split.
- **The flight budget is still a mission requirement.** The C99 inference has no dynamic allocation and each log stops at 8,192 rows with an EVS event. Each boot starts fresh CSVs. Synchronous I/O and finite Software Bus queues can affect coverage during bursts. Docker Desktop denied the simulator's real-time scheduling request, and cFS ran with standard scheduling. Measure real-time cost and detection performance against the mission configuration and processor.

## Run it on this machine

Open PowerShell in this workspace with Docker Desktop running. Check the current session or start it after a stop.

```powershell
docker ps --filter name=nos3-ids-lab
docker start nos3-ids-lab
docker exec -d nos3-ids-lab bash -lc 'bash /source/tools/start_lab.sh >/out/nos3-supervisor.log 2>&1'
Select-String 'IDS watching MID' out/nos3-flight.log
```

Send a few normal Sample NOOPs inside the container. Use the next sequence number if sending several batches within the same session. Wait for the IDS startup event before sending.

```powershell
docker exec nos3-ids-lab python3 /source/tools/sample_noop.py --count 5 --sequence 0
docker exec nos3-ids-lab bash /source/tools/export_audit.sh
.\.venv\Scripts\python.exe tools/analyze_audit.py out/ids_live_audit.csv --plot out/ids_live_timeline.png
```

The app writes `/ram/ids_audit_log.csv` and `/ram/ids_features.csv`. The export command copies snapshots into `out` on Windows. Export before restarting or stopping the container because the live files are in Linux shared memory. The audit columns are `timestamp,message_id,status,note`. Initial warm-up has an explicit header-only note. Stop the lab with `docker stop nos3-ids-lab`.

## Verified locally

- **Live baseline.** 241 valid Sample NOOPs traversed CI_LAB and generated 241 audit rows. The feature log contained 239 usable rows after two warm-up observations, one at startup and one after mission elapsed time moved backward. The deployed forest used 143 rows to fit, 48 to calibrate, and 48 to evaluate. The 48 holdout scores matched the compiled C99 model. No holdout rows exceeded the threshold.
- **Live deployment.** After rebuilding and restarting, a fresh 30-NOOP session produced 28 passes and two false alerts. The [live audit](out/ids_live_audit.csv), [timeline](out/ids_live_timeline.png), and [nominal features](out/nos3_nominal_noop_features.csv) are in the output folder. The next sequence number in the running session is 30.
- **Offline replay.** The separate synthetic model flagged one of 250 nominal observations and six of eight timing fixtures. C99 and Python scores agreed to within `1.2e-16` on 200 held-out rows. These small runs establish the capture, training, deployment, and logging path. The alert rate on representative mission traffic remains unknown.

## Offline validation and retraining

```powershell
.\.venv\Scripts\python.exe tools/demo.py
.\.venv\Scripts\python.exe tools/analyze_audit.py out/ids_demo_audit.csv --plot out/ids_demo_timeline.png
```

This generates synthetic nominal observations and off-nominal timing fixtures, exercises the same C feature extractor, and compares compiled scores with scikit-learn. It reads and writes local files. The fixture metrics measure behavior on this demonstration dataset.

For a new model, run a fresh nominal capture, export the CSVs, and save the training session under a distinct filename. The trainer requires at least 200 finite feature rows. It accepts the live feature CSV from known nominal sessions, regardless of the current model's audit flags.

```powershell
.\.venv\Scripts\python.exe tools/train.py out/ids_live_features.csv ids_app/fsw/src/ids_model.h
docker restart -t 1 nos3-ids-lab
docker exec nos3-ids-lab bash /source/tools/build_lab.sh
docker exec -d nos3-ids-lab bash -lc 'bash /source/tools/start_lab.sh >/out/nos3-supervisor.log 2>&1'
```

## Local setup

This repository contains the IDS application, model, and lab scripts. The setup script checks out upstream NOS3 at revision `1dadc685effc26730603c36c7864afce3c546406`, initializes its submodules, and patches its build and startup configuration. The build script copies the checkout and IDS app to the Linux filesystem in `ivvitc/nos3-64:20261021`, then builds the headless DeepSpace configuration. See [NOS3 Getting Started](https://github.com/nasa/nos3/blob/main/docs/wiki/NOS3_Getting_Started.md).

To set up the lab from a fresh checkout, run the following in PowerShell with Git, Python, and Docker Desktop installed. The offline replay also requires GCC on PATH.

```powershell
./tools/setup_nos3.ps1
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
New-Item -ItemType Directory -Force out | Out-Null
$root = (Get-Location).Path
docker run -d --name nos3-ids-lab --hostname nos-engine-server --mount "type=bind,source=$root,target=/source,readonly" --mount "type=bind,source=$root\out,target=/out" --sysctl fs.mqueue.msg_max=10000 --ulimit rtprio=99 --cap-add SYS_NICE ivvitc/nos3-64:20261021 sleep infinity
docker exec nos3-ids-lab bash /source/tools/build_lab.sh
```
