#!/usr/bin/env bash
# run_sil_c.sh — SIL-C 一鍵重現：veth + SOEM 建置 + ecat_slave.py + sil_csp_test
# 用法（WSL/Linux, root）：./run_sil_c.sh [axes]      預設 2 軸；14 = 雙臂全配
set -e
AXES=${1:-2}
REPO=$(cd "$(dirname "$0")/../.." && pwd)
SOEM_SRC=$REPO/third_party/SOEM
BUILD=${SOEM_BUILD:-$HOME/soem_build}

[ "$(id -u)" = 0 ] || { echo "需要 root（raw socket / veth）"; exit 2; }

# 1. SOEM（需 cmake ≥3.28；WSL 22.04 可 pip3 install cmake）
if [ ! -f "$BUILD/libsoem.a" ]; then
    cmake -B "$BUILD" -S "$SOEM_SRC" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD" -j"$(nproc)"
fi

# 2. 驗收程式
gcc -O2 -o "$BUILD/sil_csp_test" "$REPO/tools/ecat_bringup/sil_csp_test.c" \
    -I"$SOEM_SRC/include" -I"$BUILD/include" -L"$BUILD" -lsoem -lpthread -lrt

# 3. veth pair（主站 ecm0 ↔ 從站 ecs0）
ip link del ecm0 2>/dev/null || true
ip link add ecm0 type veth peer name ecs0
ip link set ecm0 up && ip link set ecs0 up

# 4. 假從站（背景）+ 主站驗收
pkill -f '[e]cat_slave.py' 2>/dev/null || true
python3 -u "$REPO/firmware/sim_py/ecat_slave.py" --iface ecs0 --axes "$AXES" --wd-ms 0 &
SLAVE=$!
sleep 1.5
rc=0
"$BUILD/sil_csp_test" ecm0 "$AXES" || rc=$?
kill $SLAVE 2>/dev/null || true
exit $rc
