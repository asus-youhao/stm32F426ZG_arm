#!/usr/bin/env bash
# 一鍵 demo：vcan ×2 + 14 顆假 PHU 從站 + PC 主站全棧
#
# 用法：
#   ./run_demo.sh        # 自動跑完整 demo（bring-up→使能→關節移動→FK→急停→恢復）
#   ./run_demo.sh -i     # 互動模式（自己下 j/e/p/q 命令）
#
# vcan0/vcan1 不存在時自動改用 user namespace（unshare -rn，免 sudo）；
# 已用 setup_vcan.sh 建好系統級 vcan 則直接沿用（可另開 candump 觀察）。
set -e
cd "$(dirname "$0")"

MODE="${1:-demo}"

# ---- 1) 確保 vcan0/vcan1 存在；不存在且無權限 → 進 user namespace 重跑自己 ----
if ! ip link show vcan0 &>/dev/null || ! ip link show vcan1 &>/dev/null; then
    if [ -z "$DEMO_IN_NS" ]; then
        echo "▶ vcan0/vcan1 不存在 → 進 user namespace（免 sudo；與外部隔離）"
        exec unshare -rn env DEMO_IN_NS=1 "$0" "$@"
    fi
    for ifc in vcan0 vcan1; do
        ip link show "$ifc" &>/dev/null || ip link add dev "$ifc" type vcan
        ip link set up "$ifc"
    done
fi
echo "▶ vcan0(左臂) / vcan1(右臂) 就緒"

# ---- 2) 相依檢查 + 編譯 ----
python3 -c "import can" 2>/dev/null || {
    echo "缺 python-can：請先 pip install python-can"; exit 2; }
make -s
echo "▶ pc_master 編譯完成"

# ---- 3) 起 14 顆假從站（每 bus 7 顆，關節表同 CLAUDE.md）----
SLAVE_LOG="$(mktemp /tmp/phu_slaves.XXXX.log)"
./run_slaves.sh vcan0 vcan1 >"$SLAVE_LOG" 2>&1 &
SLAVES_PID=$!
trap 'kill -- -$SLAVES_PID 2>/dev/null || kill $SLAVES_PID 2>/dev/null; wait 2>/dev/null' EXIT
sleep 1.5
kill -0 $SLAVES_PID 2>/dev/null || {
    echo "從站起不來，log："; cat "$SLAVE_LOG"; exit 1; }
echo "▶ 假從站已上線（log: $SLAVE_LOG）"
echo

# ---- 4) 跑主站 ----
if [ "$MODE" = "-i" ]; then
    echo "▶ 互動模式：j <idx> <rad> / e <0|1> / p / q"
    ./pc_master --bringup 1
else
    echo "▶ Demo 腳本：bring-up → 使能14軸 → J0/J3 移動 → FK → 急停 → 恢復（約15秒）"
    echo "──────────────────────────────────────────"
    {
        sleep 2.5
        echo "j 0 0.4"     # 左肩 J1 → 0.4 rad
        sleep 2
        echo "j 3 -0.6"    # 左肘 J4 → -0.6 rad
        sleep 2
        echo "p"           # 印雙臂末端 FK
        sleep 1
        echo "e 1"         # 急停 → ESTOP
        sleep 1.5
        echo "e 0"         # 解除 → 恢復 RUNNING
        sleep 1.5
        echo "p"
        sleep 1
    } | ./pc_master --bringup 1 --seconds 14
    echo "──────────────────────────────────────────"
    echo "▶ Demo 結束。互動模式：./run_demo.sh -i"
fi
