#!/usr/bin/env bash
# 起假從站 → 跑 demo → 2 秒後 kill 從站(模擬拔線) → 應觸發安全停止
set -e
cd "$(dirname "$0")"
make -s
python3 ../../common/fake_slave.py --node 1 &
SLAVE=$!
( sleep 2; kill $SLAVE 2>/dev/null; echo ">>> 從站已被拔線 <<<" ) &
KILLER=$!
trap 'kill $SLAVE $KILLER 2>/dev/null' EXIT
sleep 0.3
./demo
