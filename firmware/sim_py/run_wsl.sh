#!/usr/bin/env bash
# run_wsl.sh — 在 WSL 裡跑 bringup_decode（slcan / USB 序列埠）
#
# WSL 不會自動看到 Windows 的 USB。第一次請先在「Windows PowerShell（系統管理員）」執行：
#     winget install usbipd            # 若尚未安裝 usbipd-win
#     usbipd list                      # 找到 CANable 的 BUSID（例如 3-4）
#     usbipd bind   --busid <BUSID>    # 首次需綁定一次
#     usbipd attach --wsl --busid <BUSID>
# 掛好後回到 WSL 執行本腳本即可。拔插或重開機後要重新 attach。
#
# 用法：
#     ./run_wsl.sh                     # 自動抓 /dev/ttyACM* 或 /dev/ttyUSB*
#     ./run_wsl.sh /dev/ttyACM0 --node 1
set -euo pipefail
cd "$(dirname "$0")"

PY=$(command -v python3 || command -v python)
if [ -z "$PY" ]; then echo "找不到 python3，請先安裝。" >&2; exit 1; fi

# 相依套件（python-can / pyserial）
if ! "$PY" -c "import can, serial" 2>/dev/null; then
  echo ">>> 安裝相依套件 python-can pyserial ..."
  "$PY" -m pip install --user python-can pyserial
fi

# 找裝置：第一個參數若是 /dev/... 就當作通道，否則自動偵測
CHANNEL=""
ARGS=()
for a in "$@"; do
  if [[ "$a" == /dev/* && -z "$CHANNEL" ]]; then CHANNEL="$a"; else ARGS+=("$a"); fi
done
if [ -z "$CHANNEL" ]; then
  CHANNEL=$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -1 || true)
fi

if [ -z "$CHANNEL" ]; then
  cat >&2 <<'EOF'
找不到 /dev/ttyACM* 或 /dev/ttyUSB* — USB 還沒掛進 WSL。
請在 Windows PowerShell（系統管理員）執行：
    usbipd list
    usbipd bind   --busid <BUSID>
    usbipd attach --wsl --busid <BUSID>
再回來重跑本腳本。
EOF
  exit 2
fi

echo ">>> 使用通道 $CHANNEL"
exec "$PY" bringup_decode.py --channel "$CHANNEL" "${ARGS[@]}"
