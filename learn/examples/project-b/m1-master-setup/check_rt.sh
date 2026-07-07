#!/usr/bin/env bash
# 專案B M1 · 主站機體檢：RT kernel? cyclictest 底噪多少?
# （完整可重現建置腳本見 EtherCAT 分支的 setup_master_host.sh）

echo "== kernel =="
uname -a | grep -q PREEMPT_RT && echo "PREEMPT_RT ✅" \
    || echo "非 RT kernel ⚠️ — sudo apt install linux-image-rt-amd64 (或發行版對應包)"

echo; echo "== cyclictest (10 秒,無則 sudo apt install rt-tests) =="
if command -v cyclictest &>/dev/null; then
    sudo cyclictest -m -Sp90 -i1000 -D10 -q | tail -n +2
    echo "驗收: Max 壓在百 µs 內才有資格談 1kHz 控制"
else
    echo "cyclictest 未安裝"
fi

echo; echo "== 網卡(主站要獨占一張) =="
ip -br link | grep -v "lo\|vcan\|docker\|veth" || true
