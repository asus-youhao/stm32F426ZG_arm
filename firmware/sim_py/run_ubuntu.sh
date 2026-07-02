#!/usr/bin/env bash
# run_ubuntu.sh — 在原生 Ubuntu 跑 bringup_decode（slcan / USB 序列埠）
#
# 前置：把 CANable 之類的 USB-CAN 轉接器插上，它會出現為 /dev/ttyACM0 或 /dev/ttyUSB0。
# 若開埠出現 Permission denied，把自己加進 dialout 群組並重登入：
#     sudo usermod -aG dialout "$USER"
#
# 用法：
#     ./run_ubuntu.sh                  # 自動抓 /dev/ttyACM* 或 /dev/ttyUSB*
#     ./run_ubuntu.sh /dev/ttyUSB0 --node 1
set -euo pipefail
cd "$(dirname "$0")"

PY=$(command -v python3 || command -v python)
if [ -z "$PY" ]; then echo "找不到 python3，請先安裝。" >&2; exit 1; fi

if ! "$PY" -c "import can, serial" 2>/dev/null; then
  echo ">>> 安裝相依套件 python-can pyserial ..."
  "$PY" -m pip install --user python-can pyserial
fi

CHANNEL=""
ARGS=()
for a in "$@"; do
  if [[ "$a" == /dev/* && -z "$CHANNEL" ]]; then CHANNEL="$a"; else ARGS+=("$a"); fi
done
if [ -z "$CHANNEL" ]; then
  CHANNEL=$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -1 || true)
fi

if [ -z "$CHANNEL" ]; then
  echo "找不到 /dev/ttyACM* 或 /dev/ttyUSB* — 確認 USB-CAN 轉接器已插上（dmesg | tail 可查）。" >&2
  exit 2
fi

if [ ! -w "$CHANNEL" ]; then
  echo "警告：$CHANNEL 沒有寫入權限，可能出現 Permission denied。" >&2
  echo "      執行 'sudo usermod -aG dialout $USER' 後重新登入。" >&2
fi

echo ">>> 使用通道 $CHANNEL"
exec "$PY" bringup_decode.py --channel "$CHANNEL" "${ARGS[@]}"
