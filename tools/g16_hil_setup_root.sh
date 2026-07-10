#!/usr/bin/env bash
# g16_hil_setup_root.sh — G16 HIL 環境一次性 root 設定（跑一次即可）
#
#   sudo tools/g16_hil_setup_root.sh
#
# 做三件事：
#  1) udev 規則補 ST-Link/V2.1（0483:374b,Nucleo-F746ZG 板載）與 V3 系列
#     —— 既有 49-stlink.rules 只涵蓋 V2(3748),probe-rs 開不了 V2.1。
#  2) 建 ~/.local/bin/python3-rawnet(python3 副本)並授 cap_net_raw
#     —— ecat_slave.py 假從站需要 AF_PACKET raw socket,之後免 sudo。
#  3) SOEM slaveinfo setcap(若已編譯)—— PC 端掃站也免 sudo。
set -eu

[ "$(id -u)" = 0 ] || { echo "請用 sudo 執行"; exit 1; }
REAL_USER="${SUDO_USER:-asus}"
REAL_HOME="$(getent passwd "$REAL_USER" | cut -d: -f6)"

# 1) udev：ST-Link V2.1 / V3
cat > /etc/udev/rules.d/49-stlink-v21-v3.rules <<'EOF'
# ST-Link/V2.1 (Nucleo/Discovery 板載)
SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374b", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="3752", MODE="0666", GROUP="plugdev"
# ST-Link/V3
SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374d", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374e", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374f", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="3753", MODE="0666", GROUP="plugdev"
SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="3754", MODE="0666", GROUP="plugdev"
EOF
udevadm control --reload-rules
udevadm trigger --subsystem-match=usb --attr-match=idVendor=0483
echo "[1/3] udev 規則已裝並重載"

# 2) python3 raw-socket 副本
PYBIN="$(readlink -f /usr/bin/python3)"
DEST="$REAL_HOME/.local/bin/python3-rawnet"
install -o "$REAL_USER" -g "$REAL_USER" -m 0755 "$PYBIN" "$DEST"
setcap cap_net_raw,cap_net_admin+eip "$DEST"
echo "[2/3] $DEST → $(getcap "$DEST")"

# 3) SOEM slaveinfo（存在才做）
SLAVEINFO="$REAL_HOME/SOEM/build/test/linux/slaveinfo/slaveinfo"
if [ -x "$SLAVEINFO" ]; then
    setcap cap_net_raw,cap_net_admin+eip "$SLAVEINFO"
    echo "[3/3] slaveinfo → $(getcap "$SLAVEINFO")"
else
    echo "[3/3] 略過（$SLAVEINFO 不存在）"
fi

echo "完成。之後假從站免 sudo：python3-rawnet firmware/sim_py/ecat_slave.py --iface <nic> ..."
