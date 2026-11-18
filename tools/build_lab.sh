#!/usr/bin/env bash
set -euo pipefail

# Use a Linux filesystem for CMake, submodules, and simulator scripts.
if [[ ! -f /opt/nos3/.ids-source-copied ]]; then
    mkdir -p /opt/nos3
    cp -a /source/nos3/. /opt/nos3/
    touch /opt/nos3/.ids-source-copied
fi
cd /opt/nos3
mkdir -p fsw/apps/ids_app
cp -a /source/ids_app/. fsw/apps/ids_app/
cp /source/nos3/cfg/nos3_defs/{targets.cmake,cpu1_cfe_es_startup.scr} cfg/nos3_defs/
sed -i 's|<scenario>STF1</scenario>|<scenario>DeepSpace</scenario>|' cfg/nos3-mission.xml
make config SC1_CFG=spacecraft/sc-minimal-config.xml
make -j4 build-fsw
make -j4 build-sim
if [[ ! -x /opt/42/42 ]]; then
    if [[ ! -d /opt/42/.git ]]; then
        git clone --depth 1 --branch dev_20260403 https://github.com/nasa-itc/42.git /opt/42
    fi
    make -C /opt/42 -j4
fi
