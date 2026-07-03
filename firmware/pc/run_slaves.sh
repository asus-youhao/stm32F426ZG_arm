#!/usr/bin/env bash
# 啟動 14 顆假 EYOU PHU CiA402 從站：vcan0=左臂 node1..7、vcan1=右臂 node1..7
# 用法：./run_slaves.sh [左介面] [右介面]   （預設 vcan0 vcan1;Ctrl-C 一起收掉）
set -e
cd "$(dirname "$0")/../sim_py"

LEFT="${1:-vcan0}"
RIGHT="${2:-vcan1}"
# 關節表（CLAUDE.md）：J1/J2=PHU20, J3/J4=PHU17, J5-J7=PHU14
NODES="1:PHU20,2:PHU20,3:PHU17,4:PHU17,5:PHU14,6:PHU14,7:PHU14"

python3 can_slave.py --interface socketcan --channel "$LEFT"  --nodes "$NODES" &
PID_L=$!
python3 can_slave.py --interface socketcan --channel "$RIGHT" --nodes "$NODES" &
PID_R=$!

trap 'kill $PID_L $PID_R 2>/dev/null; wait' INT TERM
echo "左臂從站 pid=$PID_L @$LEFT,右臂從站 pid=$PID_R @$RIGHT（Ctrl-C 結束）"
wait
