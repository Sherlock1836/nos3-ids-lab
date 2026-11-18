#!/usr/bin/env bash
set -euo pipefail

exec 9>/tmp/nos3-ids-lab.lock
flock -n 9 || { echo "The NOS3 IDS lab is already running" >&2; exit 1; }

repo=/opt/nos3
sim=$repo/sims/build/bin
flight=$repo/fsw/build/exe/cpu1
for file in "$flight/core-cpu1" "$sim/nos3-single-simulator" /opt/42/42; do
    [[ -x "$file" ]] || { echo "Missing NOS3 build output: $file" >&2; exit 1; }
done

if ! grep -qw sc01-nos-engine-server /etc/hosts; then
    ip=$(getent ahostsv4 nos-engine-server | awk 'NR == 1 {print $1}')
    printf '%s sc01-nos-engine-server fortytwo\n' "$ip" >> /etc/hosts
fi

mkdir -p "$flight/data"/{evs,hk,cam,inst} /opt/42/NOS3InOut
# This headless lab has no telemetry ground station to drain the TO_LAB pipe.
sed -i '/^CFE_APP, to_lab,/d' "$flight/cf/cfe_es_startup.scr"
cp -a "$repo/cfg/build/InOut/." /opt/42/NOS3InOut/
sed -i '/Graphics Front End/s/^TRUE/FALSE/' /opt/42/NOS3InOut/Inp_Sim.txt
# The minimal spacecraft has no radio simulator. Skip its otherwise blocking 42 socket.
sed -i '/Radio IPC/{n;s/^TX /OFF /;}' /opt/42/NOS3InOut/Inp_IPC.txt

pids=()
stop_lab() {
    if (( ${#pids[@]} )); then
        kill "${pids[@]}" 2>/dev/null || true
        wait "${pids[@]}" 2>/dev/null || true
    fi
}
trap stop_lab EXIT
trap 'exit 0' TERM INT

start() {
    local name=$1 directory=$2
    shift 2
    (cd "$directory" && exec "$@" 9>&-) <&8 >"/out/nos3-${name}.log" 2>&1 &
    pids+=("$!")
}

fifo=$(mktemp -d)
mkfifo "$fifo/engine-input"
exec 8<>"$fifo/engine-input"
unlink "$fifo/engine-input"
rmdir "$fifo"
start engine "$sim" /usr/bin/nos_engine_server_standalone -f nos_engine_server_config.json
for ((check=0; check<50; check++)); do
    if (exec 3<>/dev/tcp/nos-engine-server/12001) 2>/dev/null; then
        break
    fi
    sleep 0.1
done
(( check < 50 )) || { echo "NOS Engine did not open port 12001" >&2; exit 1; }

start time "$sim" env TERM=xterm bash -c 'exec ./nos3-single-simulator -f nos3-simulator.xml time >/dev/null'
start dynamics /opt/42 ./42 NOS3InOut
start sample "$sim" ./nos3-single-simulator -f nos3-simulator.xml sample-sim
start flight "$flight" ./core-cpu1 -R PO

wait -n
