#!/usr/bin/env bash
# 建立虛擬 CAN 介面（免硬體練 CANopen）。需要 sudo。
# vcan0 = 左臂、vcan1 = 右臂（對應 firmware 的 CO_BUS_LEFT/RIGHT）
set -e
for dev in vcan0 vcan1; do
    if ! ip link show "$dev" &>/dev/null; then
        sudo ip link add dev "$dev" type vcan
    fi
    sudo ip link set up "$dev"
    echo "$dev up"
done
echo "ok — 觀察流量: candump vcan0"
