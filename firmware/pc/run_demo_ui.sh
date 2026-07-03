#!/usr/bin/env bash
# 一鍵 UI demo：C 韌體主站 ⇄ 真實 vcan bus ⇄ 假 PHU 從站，瀏覽器看 3D 動畫 + CAN 資料流
#
#   pc_master(主站,C)──RPDO/SDO──► vcan0/vcan1 ◄──TPDO──can_slave.py×14(從站,物理)
#                                      ▲
#                            ws_server --monitor（旁聽鏡射）
#                                      ▼
#            瀏覽器 viewer3d.html（3D 動畫）/ can_monitor_ws.html(資料流)
#
# 需要系統級 vcan（瀏覽器在 namespace 外,連不進 unshare 的隔離網路）：
#   先跑一次  sudo ./setup_vcan.sh
set -e
cd "$(dirname "$0")"

if ! ip link show vcan0 &>/dev/null || ! ip link show vcan1 &>/dev/null; then
    echo "缺 vcan0/vcan1。UI demo 需要系統級 vcan（瀏覽器要能連 HTTP/WS）："
    echo "    sudo ./setup_vcan.sh"
    echo "（純終端 demo 不需要 sudo：./run_demo.sh）"
    exit 2
fi

python3 -c "import can" 2>/dev/null || { echo "缺 python-can：pip install python-can"; exit 2; }
make -s
echo "▶ pc_master 編譯完成"

# ---- 從站（物理模擬）+ 監聽伺服器（3D/資料流後端）----
SLAVE_LOG="$(mktemp /tmp/phu_ui_slaves.XXXX.log)"
WS_LOG="$(mktemp /tmp/phu_ui_ws.XXXX.log)"
./run_slaves.sh vcan0 vcan1 >"$SLAVE_LOG" 2>&1 &
SLAVES_PID=$!
( cd ../sim_py && exec python3 -u ws_server.py 8765 8090 \
      --monitor --interface socketcan --channel vcan0 --channel2 vcan1 ) >"$WS_LOG" 2>&1 &
WS_PID=$!
trap 'kill $SLAVES_PID $WS_PID 2>/dev/null; wait 2>/dev/null' EXIT
sleep 1.5
kill -0 $SLAVES_PID 2>/dev/null || { echo "從站起不來："; cat "$SLAVE_LOG"; exit 1; }
kill -0 $WS_PID     2>/dev/null || { echo "ws_server 起不來："; cat "$WS_LOG"; exit 1; }

HTTP_PORT=$(grep -oP "viewer3d\.html" -m1 "$WS_LOG" >/dev/null && \
            grep -oP "http://localhost:\K[0-9]+" -m1 "$WS_LOG" || echo 8090)
echo "▶ 從站×14 + 監聽伺服器已上線（log: $SLAVE_LOG / $WS_LOG）"
echo
echo "┌──────────────────────────────────────────────────────────┐"
echo "│  瀏覽器開：                                                │"
echo "│    3D 動畫   http://localhost:${HTTP_PORT}/ui/viewer3d.html        │"
echo "│    CAN 資料流 http://localhost:${HTTP_PORT}/ui/can_monitor_ws.html  │"
echo "└──────────────────────────────────────────────────────────┘"
echo
echo "▶ pc_master 主站啟動,連續示範動作（Ctrl-C 結束一切）"

# ---- 連續動作腳本：雙臂揮手循環,3D 會一直動 ----
feeder() {
    sleep 3
    while :; do
        echo "j 0 0.5";  echo "j 7 0.5"          # 雙肩抬起
        echo "j 3 -0.9"; echo "j 10 -0.9"        # 雙肘彎曲
        sleep 4
        echo "j 5 0.5";  echo "j 12 -0.5"        # 腕擺動
        sleep 2
        echo "j 5 -0.5"; echo "j 12 0.5"
        sleep 2
        echo "j 0 -0.1"; echo "j 7 -0.1"         # 放下
        echo "j 3 -0.2"; echo "j 10 -0.2"
        echo "j 5 0.0";  echo "j 12 0.0"
        sleep 4
    done
}
feeder | ./pc_master
