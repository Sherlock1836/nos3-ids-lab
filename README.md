# NOS3 command intrusion detector

This branch runs a passive cFS IDS in NASA NOS3's headless DeepSpace scenario. It monitors the Sample (`0x18FA`) and CI_LAB (`0x18E0`) command MIDs on the Software Bus. The Docker lab runs cFS, NOS Engine, the NOS time driver, 42 dynamics, and the Sample simulator. The lab sends documented NOOPs through CI_LAB on container loopback and publishes no host ports.

## What the detector evaluates

- **Command contract.** The cFS app checks the header, function code, declared size, and cFS checksum. The contracts use the destination apps' command definitions. Configure the subscriptions and tables in `ids_app/fsw/src/ids_app.c` for a different mission. [cFE Application Developer’s Guide](https://github.com/nasa/cFE/blob/dev/docs/cFE%20Application%20Developers%20Guide.md)
- **Temporal behavior.** Each MID has an independent one-second window and a 12-tree Isolation Forest exported to C99. A high score triggers an alert when a short gap and elevated recent command count corroborate it. A separate rate ceiling catches sustained high receiver counts. Five seconds of idleness or a clock regression causes warm-up. Timing is sampled at IDS receipt and includes receiver scheduling and queueing.
- **Operator evidence.** The audit keeps the original four fields `timestamp,message_id,status,note`. A separate feature log includes MID, timestamp, raw features, score, and timing decision. A warm-up note identifies commands that passed the contract while the model gathered history.

The app observes Software Bus copies and has no command authority. The checksum detects corruption and provides no sender authentication. Coverage is limited to the configured MIDs. The NOOP profiles need retraining on representative command mixes and mission modes. The C99 inference allocates no dynamic memory. Logs stop at 8,192 rows with an EVS event. Validate processor cost, queue loss, and alert rates on the target platform.

See [improved detector validation](docs/improved-detector.md) for this branch's results and [nominal validation of the first prototype](docs/nominal-validation.md) for the earlier experiment.

## Run the lab

After completing Local setup, launch the simulator or resume a previously built container.

```powershell
docker start nos3-ids-lab
docker exec -d nos3-ids-lab bash -lc 'bash /source/tools/start_lab.sh >/out/nos3-supervisor.log 2>&1'
Select-String 'IDS watching MID|OPERATIONAL' out/nos3-flight.log
```

Once cFS reaches the operational state, capture a paced NOOP run and inspect its audit. Use an unused capture name and the next sequence number when continuing an earlier batch.

```powershell
docker exec nos3-ids-lab python3 /source/tools/capture_nominal.py smoke --count 5 --seed 42
.\.venv\Scripts\python.exe tools/analyze_audit.py out/smoke_audit.csv --nominal-features out/smoke_features.csv --plot out/smoke_timeline.png
```

The app writes `/ram/ids_audit_log.csv` and `/ram/ids_features.csv`. To save all current rows to the Windows workspace, run `docker exec nos3-ids-lab bash /source/tools/export_audit.sh`. Export before restarting or stopping the container because the live files are in Linux shared memory. Stop the lab with `docker stop nos3-ids-lab`.

## Retrain and verify

Collect separate fit and calibration captures from known nominal traffic. Each needs at least 64 usable rows per MID. The shipped profiles let you use the detector before collecting new baselines. Send no other commands while a scripted capture is active.

```powershell
docker exec nos3-ids-lab python3 /source/tools/capture_nominal.py fit --count 112 --seed 491
docker exec nos3-ids-lab python3 /source/tools/capture_nominal.py calibration --count 76 --sequence 112 --seed 885
.\.venv\Scripts\python.exe tools/train_profiles.py out/fit_features.csv out/calibration_features.csv ids_app/fsw/src --reference out/profile_reference.csv
docker restart -t 1 nos3-ids-lab
docker exec nos3-ids-lab bash /source/tools/build_lab.sh
docker exec -d nos3-ids-lab bash -lc 'bash /source/tools/start_lab.sh >/out/nos3-supervisor.log 2>&1'
```

After cFS reaches the operational state, capture a fresh validation session and compare the compiled scorer, scikit-learn reference, and live audit. The comparison uses the earlier model in `ids_model.h` as a baseline on Sample observations. The two deployed models come from `ids_profiles.h` and its included headers.

```powershell
docker exec nos3-ids-lab python3 /source/tools/capture_nominal.py validation --count 100 --seed 1092
.\.venv\Scripts\python.exe tools/check_profiles.py out/profile_reference.csv out/validation_features.csv
.\.venv\Scripts\python.exe tools/analyze_audit.py out/validation_audit.csv --nominal-features out/validation_features.csv
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
