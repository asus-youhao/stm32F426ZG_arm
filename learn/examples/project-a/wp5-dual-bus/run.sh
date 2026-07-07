#!/usr/bin/env bash
# 起 14 個假從站(vcan0 左臂 7 + vcan1 右臂 7)後跑雙 bus demo
set -e
cd "$(dirname "$0")"
make -s
PIDS=()
for n in 1 2 3 4 5 6 7; do
    python3 ../../common/fake_slave.py --iface vcan0 --node $n & PIDS+=($!)
    python3 ../../common/fake_slave.py --iface vcan1 --node $n & PIDS+=($!)
done
trap 'kill "${PIDS[@]}" 2>/dev/null' EXIT
sleep 0.5
./demo
