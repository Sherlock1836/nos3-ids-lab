#!/usr/bin/env bash
set -euo pipefail

# The Linux OSAL maps the cFS /ram mount to this directory in the lab container.
cp /dev/shm/osal:RAM/ids_audit_log.csv /out/ids_live_audit.csv
cp /dev/shm/osal:RAM/ids_features.csv /out/ids_live_features.csv
wc -l /out/ids_live_{audit,features}.csv
