#!/usr/bin/env bash
# L2-12 · 一鍵串起免硬體驗證流程（對照 learn/l2-mid.html 第 12 節）
# 需求：已跑過 ../../common/vcan_setup.sh（需要 sudo 一次）
set -e
cd "$(dirname "$0")/.."

echo "== build =="
for d in 01-can-raw 02-nmt-heartbeat 03-sdo-client 04-pdo-csp 05-bus-budget; do
    make -s -C "$d"
done

echo "== 啟動假從站 node1 =="
python3 ../common/fake_slave.py --node 1 &
SLAVE=$!
trap 'kill $SLAVE 2>/dev/null' EXIT
sleep 0.3

echo; echo "== 05 頻寬預算(純計算) ==";    ./05-bus-budget/demo
echo; echo "== 02 NMT + heartbeat ==";     ./02-nmt-heartbeat/demo
echo; echo "== 03 SDO 組態 ==";            ./03-sdo-client/demo
echo; echo "== 04 PDO 使能+CSP 跟隨 ==";   ./04-pdo-csp/demo
echo; echo "全部通過 — 同一套流程就是專案 A 單軸 bring-up 的預演"
