#!/usr/bin/env bash
# 建立虛擬 CAN 介面：vcan0=左臂、vcan1=右臂（需 sudo,開機後需重跑）
set -e

for ifc in vcan0 vcan1; do
    if ip link show "$ifc" &>/dev/null; then
        echo "$ifc 已存在"
    else
        sudo modprobe vcan
        sudo ip link add dev "$ifc" type vcan
        echo "$ifc 已建立"
    fi
    sudo ip link set up "$ifc"
done

ip -brief link show type vcan
echo "OK。可用 'candump vcan0'（can-utils）觀察左臂 bus。"
