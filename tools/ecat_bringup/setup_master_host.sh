#!/usr/bin/env bash
# setup_master_host.sh — EtherCAT 主站機一鍵建置（SOEM + IgH）
#
# 重現 2026-07-06/07 在 asus-gx701（Ubuntu 24.04.4, 6.8.1-1052-realtime）
# 上的建置。冪等：可重跑。
#
# 用法：
#   ./setup_master_host.sh [--dir ~/robot_test] [--nic-mac 9c:eb:e8:5f:54:1c] [--clone]
#   預設：從 repo 的 third_party/{SOEM,ethercat} vendored 原始碼建置（離線自足）
#   --clone：改從上游重新 git clone 到 $DIR（更新版本、或無 vendored 源時用）
#   --nic-mac 只影響最後印出的 IgH 載入指令,不動系統
#
# 產出（$DIR 下）：
#   SOEM/build/libsoem.a + samples（slaveinfo 等）
#   ethercat/master/ec_master.ko + devices/ec_generic.ko + tool/ethercat
#
# 版本鎖定（實測可用組合,升級請先在測試機驗證）：
#   SOEM  github.com/OpenEtherCATsociety/SOEM   @ 2f73eaa（2.x, context API）
#   IgH   gitlab.com/etherlab.org/ethercat      @ beb2bf07（stable-1.6 tip,
#         1.6.9-8; 含 igc_6.8 合併 → 支援 6.8 內核;e1000e 原生驅動僅到 6.4,
#         6.8 上用 --enable-generic 的 generic driver）
set -eu

DIR="$HOME/robot_test"
MAC="9c:eb:e8:5f:54:1c"
CLONE=0
# 本腳本在 tools/ecat_bringup/ → vendored 源在 ../../third_party/
VENDOR="$(cd "$(dirname "$0")/../../third_party" 2>/dev/null && pwd || true)"
while [ $# -gt 0 ]; do case $1 in
    --dir) DIR=$2; shift 2 ;;
    --nic-mac) MAC=$2; shift 2 ;;
    --clone) CLONE=1; shift ;;
    *) sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
esac; done

# 取得原始碼到 $DIR/<name>：預設複製 vendored 源,--clone 則從上游取
fetch_src() { # <name> <giturl> <ref>
    local name=$1 url=$2 ref=$3
    if [ "$CLONE" = 0 ] && [ -d "$VENDOR/$name" ]; then
        echo "  用 vendored 源: $VENDOR/$name"
        [ -d "$DIR/$name" ] || cp -a "$VENDOR/$name" "$DIR/$name"
    else
        [ -d "$DIR/$name" ] || git clone "$url" "$DIR/$name"
        git -C "$DIR/$name" fetch -q origin && git -C "$DIR/$name" checkout -q "$ref"
    fi
}

SOEM_REF=2f73eaa
IGH_REF=beb2bf07

echo "== 依賴（需 sudo）=="
sudo apt-get install -y -qq build-essential git cmake autoconf automake \
    libtool pkg-config rt-tests can-utils "linux-headers-$(uname -r)"
[ -d "/lib/modules/$(uname -r)/build" ] || { echo "!! 無內核 headers"; exit 1; }
uname -v | grep -q PREEMPT_RT || echo "⚠ 非 PREEMPT_RT 內核（sudo pro enable realtime-kernel）"

mkdir -p "$DIR"; cd "$DIR"

echo "== SOEM @$SOEM_REF =="
fetch_src SOEM https://github.com/OpenEtherCATsociety/SOEM.git "$SOEM_REF"
cmake -S SOEM -B SOEM/build -DCMAKE_BUILD_TYPE=Release >/dev/null
make -C SOEM/build -j"$(nproc)" >/dev/null
echo "  ✓ $(ls SOEM/build/libsoem.a) + samples"

echo "== IgH @$IGH_REF（stable-1.6, 1.6.9-8）=="
fetch_src ethercat https://gitlab.com/etherlab.org/ethercat.git "$IGH_REF"
cd ethercat
[ -x configure ] || ./bootstrap >/dev/null
# 6.8-rt 實測旗標：generic driver（任何 NIC 可用;原生 igc 亦可 --enable-igc）
./configure --disable-8139too --enable-generic \
            --with-linux-dir="/lib/modules/$(uname -r)/build" >/dev/null
make -j"$(nproc)" >/dev/null
make -j"$(nproc)" modules >/dev/null
cd "$DIR"
echo "  ✓ ethercat/master/ec_master.ko + devices/ec_generic.ko + tool/ethercat"

cat <<EOF

== 完成。常用操作 ==
# IgH 載入（EtherCAT NIC 的 MAC=$MAC;NIC 須 link up,不可有 IP 服務依賴）
sudo insmod $DIR/ethercat/master/ec_master.ko main_devices=$MAC
sudo insmod $DIR/ethercat/devices/ec_generic.ko
sudo $DIR/ethercat/tool/ethercat slaves          # 掃鏈
sudo $DIR/ethercat/tool/ethercat upload -p0 -t uint8 0x2100 0   # CoE SDO 讀
sudo $DIR/ethercat/tool/ethercat debug 1         # FSM 詳細 dmesg（診斷用）

# SOEM 工具（raw socket,與 IgH 互斥——先 rmmod）
sudo rmmod ec_generic ec_master
sudo $DIR/SOEM/build/samples/slaveinfo/slaveinfo <ifname> -map

# 本 repo 的 bring-up 工具編譯方式見 tools/ecat_bringup/README.md
# RT 調校（isolcpus/IRQ 綁核）見 firmware/rt/rt_setup.sh
EOF
