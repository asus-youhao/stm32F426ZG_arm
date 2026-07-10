#!/usr/bin/env bash
# ecat_g16.sh — G16 開發機 EtherCAT 主站控制選單(IgH / SOEM / pysoem)
#
# 定位:G16 = Ubuntu 22.04 generic kernel(非 PREEMPT_RT)+ USB 網卡,
#        僅做「功能驗證 / SIL / 掃站 / 抓 SDO-PDO」;真實時控制請回 gx701 24.04+RT。
#
# 網卡:預設綁 enx00e04c6809b6(Realtek 00:e0:4c,EtherCAT 那張)。
#       enx8e986ba97ba4 是手機 USB 熱點上網,勿用。可用環境變數覆寫:
#         ECAT_NIC=enxXXXX ./ecat_g16.sh
#
# 需求:IgH(/usr/bin/ethercat 已裝)、SOEM(~/SOEM 已編)、pysoem(pip --user 已裝)。
#       IgH 啟動/停止、SOEM/pysoem 開 raw socket 皆需 root,本腳本會自動 sudo。
#
# 用法:./ecat_g16.sh          # 進互動選單
#        ./ecat_g16.sh <n>     # 直接執行第 n 項後離開(可搭配腳本/CI)
set -u

# ---------- 可調參數 ----------
NIC="${ECAT_NIC:-enx00e04c6809b6}"                 # EtherCAT 網卡介面名
NIC_MAC="$(cat /sys/class/net/$NIC/address 2>/dev/null | tr a-z A-Z)"
IGH_CONF=/etc/sysconfig/ethercat
SOEM_DIR="${SOEM_DIR:-$HOME/SOEM}"
SLAVEINFO="$SOEM_DIR/build/test/linux/slaveinfo/slaveinfo"

# ---------- 小工具 ----------
c_hdr() { printf '\n\033[1;36m== %s ==\033[0m\n' "$1"; }
c_ok()  { printf '\033[1;32m%s\033[0m\n' "$1"; }
c_warn(){ printf '\033[1;33m%s\033[0m\n' "$1"; }
c_err() { printf '\033[1;31m%s\033[0m\n' "$1"; }
pause() { read -rp $'\n按 Enter 返回選單…' _; }

need_nic() {
    if [ ! -e "/sys/class/net/$NIC" ]; then
        c_err "找不到網卡 $NIC。現有介面:"; ls /sys/class/net/ | grep -vE '^(lo|vcan)'; return 1
    fi
    return 0
}

# ---------- 動作 ----------
act_env() {
    c_hdr "環境狀態"
    echo "Kernel : $(uname -r)  ($(grep -q PREEMPT_RT /boot/config-$(uname -r) 2>/dev/null && echo RT || echo 'generic/非RT'))"
    echo "NIC    : $NIC  MAC=$NIC_MAC"
    if need_nic; then
        echo -n "  carrier: "; cat /sys/class/net/$NIC/carrier 2>/dev/null
        echo    "  operstate: $(cat /sys/class/net/$NIC/operstate 2>/dev/null)"
    fi
    echo -n "IgH    : "; ethercat version 2>/dev/null | head -1 || c_err "未安裝"
    echo -n "  service: "; systemctl is-active ethercat 2>/dev/null || true
    echo -n "SOEM   : "; [ -x "$SLAVEINFO" ] && echo "$SLAVEINFO" || c_err "未編譯($SLAVEINFO)"
    echo -n "pysoem : "; python3 -c "import pysoem;print(pysoem.__version__)" 2>/dev/null || c_err "未安裝"
}

act_igh_start() {
    c_hdr "IgH:設定並啟動 master(綁 $NIC / $NIC_MAC)"
    need_nic || return
    [ -n "$NIC_MAC" ] || { c_err "抓不到 MAC"; return; }
    c_warn "IgH 與 SOEM/pysoem 不能同時抓同一張網卡;啟 IgH 前請確認沒在跑 SOEM。"
    sudo bash -c "
        cp -n '$IGH_CONF' '$IGH_CONF.bak.\$(date +%s)' 2>/dev/null || true
        sed -i 's|^MASTER0_DEVICE=.*|MASTER0_DEVICE=\"$NIC_MAC\"|' '$IGH_CONF'
        sed -i 's|^DEVICE_MODULES=.*|DEVICE_MODULES=\"generic\"|'   '$IGH_CONF'
        grep -E '^MASTER0_DEVICE|^DEVICE_MODULES' '$IGH_CONF'
        nmcli dev set '$NIC' managed no 2>/dev/null || true
        systemctl restart ethercat || /etc/init.d/ethercat restart
    " && sleep 1 && ethercat master 2>/dev/null && c_ok "IgH master 已啟動" || c_err "啟動失敗"
}

act_igh_slaves() { c_hdr "IgH:掃站(ethercat slaves)"; ethercat slaves 2>&1 || c_err "掃不到——確認馬達上電、線接 $NIC、且接 IN 埠"; }
act_igh_master() { c_hdr "IgH:master 狀態"; ethercat master 2>&1 || c_err "master 未啟動"; }
act_igh_pdo() {
    c_hdr "IgH:PDO / SDO 檢視"
    echo "--- pdos ---"; ethercat pdos 2>&1 | head -40
    echo "--- 站0 SDO 字典(前 30 行) ---"; ethercat sdos -p0 2>&1 | head -30
}
act_igh_stop() { c_hdr "IgH:停止 master"; sudo systemctl stop ethercat 2>/dev/null || sudo /etc/init.d/ethercat stop; c_ok "已停止(網卡釋放,可換 SOEM)"; }

act_soem_scan() {
    c_hdr "SOEM:slaveinfo 掃站($NIC)"
    [ -x "$SLAVEINFO" ] || { c_err "找不到 $SLAVEINFO,先編 SOEM"; return; }
    need_nic || return
    if systemctl is-active --quiet ethercat 2>/dev/null; then
        c_warn "IgH master 正在跑,會搶網卡。建議先選 [5] 停 IgH。"; fi
    if getcap "$SLAVEINFO" 2>/dev/null | grep -q cap_net_raw; then
        "$SLAVEINFO" "$NIC"
    else
        sudo "$SLAVEINFO" "$NIC"
    fi
}
act_soem_setcap() {
    c_hdr "SOEM:給 slaveinfo raw-socket cap(之後免 sudo)"
    [ -x "$SLAVEINFO" ] || { c_err "找不到 $SLAVEINFO"; return; }
    sudo setcap cap_net_raw,cap_net_admin+eip "$SLAVEINFO" && c_ok "已授權:$(getcap "$SLAVEINFO")"
}

act_pysoem() {
    c_hdr "pysoem:config_init 掃站($NIC)"
    need_nic || return
    if systemctl is-active --quiet ethercat 2>/dev/null; then
        c_warn "IgH master 正在跑,會搶網卡。建議先選 [5] 停 IgH。"; fi
    sudo python3 -c "
import pysoem
m = pysoem.Master(); m.open('$NIC')
n = m.config_init()
print('slaves found:', n)
for i, s in enumerate(m.slaves):
    print(f'  [{i}] {s.name}  man=0x{s.man:08X} id=0x{s.id:08X} rev=0x{s.rev:08X}')
m.close()
" || c_err "掃描失敗——確認馬達上電、線接 $NIC"
}

# ---------- 選單 ----------
menu() {
    cat <<EOF

\033[1;37m┈ G16 EtherCAT 控制選單 ┈\033[0m  (NIC=$NIC)
  --- IgH(kernel master) ---
   1) 設定並啟動 IgH master(綁網卡)
   2) 掃站  ethercat slaves
   3) master 狀態
   4) PDO / SDO 檢視
   5) 停止 IgH master(釋放網卡)
  --- SOEM(userspace) ---
   6) SOEM slaveinfo 掃站
   7) SOEM setcap(免 sudo 授權)
   8) pysoem 掃站測試
  --- 其他 ---
   9) 環境 / 網卡狀態
   0) 離開
EOF
}

run_action() {
    case "$1" in
        1) act_igh_start ;;
        2) act_igh_slaves ;;
        3) act_igh_master ;;
        4) act_igh_pdo ;;
        5) act_igh_stop ;;
        6) act_soem_scan ;;
        7) act_soem_setcap ;;
        8) act_pysoem ;;
        9) act_env ;;
        *) return 1 ;;
    esac
}

# 直接帶參數模式:./ecat_g16.sh 2
if [ $# -ge 1 ]; then run_action "$1"; exit $?; fi

# 互動模式
while true; do
    printf '%b' "$(menu)"
    read -rp $'\n選項數字 > ' choice
    case "$choice" in
        0|q|Q) echo "bye"; exit 0 ;;
        [1-9]) run_action "$choice"; pause ;;
        *) c_err "無效選項" ;;
    esac
done
