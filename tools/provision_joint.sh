#!/usr/bin/env bash
# provision_joint.sh — EYOU PHU 關節 CANopen 佈建 SOP（WP-C0.3）
#
# 依 docs/design/linux-canopen-master-plan.md §4：
#   1) 0x2100=2 切控制權到 CANopen（出廠 1=EtherCAT）
#   2) 0x26A0=<new-id> 設 node-id（出廠全是 1 → 一次只接一顆新關節！）
#   3) 0x2130=1 存檔（退路 0x1010:01="save"）,約 3 s,保存中嚴禁斷電
#   4) 重上電後驗證：heartbeat、0x1008 名稱、0x2025 解析度、0x26A2/A3 減速比
#   5) 記入佈建清冊 CSV（貼標籤由人工完成）
#
# 需求：can-utils（cansend/candump）;介面先起好：
#   sudo ip link set can0 type can bitrate 1000000 && sudo ip link set up can0
#
# 用法：
#   ./provision_joint.sh <can-if> <new-node-id 1..7> [選項]
#     --current-id N   目前 node-id（預設：自動偵測 heartbeat,失敗退 1）
#     --label TEXT     清冊標籤（如 L_J4_Elbow）
#     --ledger FILE    清冊 CSV（預設 <script_dir>/provision_ledger.csv）
#     --skip-save      不下存檔命令（乾跑排練用）
#     --dry-run        只印出會送的 frame,不真的送
set -u

# ---------- 參數 ----------
[ $# -ge 2 ] || { sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
IF=$1; NEW_ID=$2; shift 2
CUR_ID=""; LABEL="-"; SKIP_SAVE=0; DRY=0
LEDGER="$(cd "$(dirname "$0")" && pwd)/provision_ledger.csv"
while [ $# -gt 0 ]; do
    case $1 in
        --current-id) CUR_ID=$2; shift 2 ;;
        --label)      LABEL=$2; shift 2 ;;
        --ledger)     LEDGER=$2; shift 2 ;;
        --skip-save)  SKIP_SAVE=1; shift ;;
        --dry-run)    DRY=1; shift ;;
        *) echo "未知選項: $1"; exit 2 ;;
    esac
done
[ "$NEW_ID" -ge 1 ] && [ "$NEW_ID" -le 7 ] || { echo "new-node-id 需 1..7"; exit 2; }

command -v cansend >/dev/null && command -v candump >/dev/null \
    || { echo "需要 can-utils（sudo apt install can-utils）"; exit 1; }
if [ "$DRY" != 1 ]; then
    ip link show "$IF" >/dev/null 2>&1 \
        || { echo "介面 $IF 不存在。先: sudo ip link set $IF type can bitrate 1000000 && sudo ip link set up $IF"; exit 1; }
fi

# ---------- SDO 工具（expedited,little-endian）----------
# 送請求並等 0x580+node 回應;stdout 印回應 8 個 hex byte（空白分隔）,失敗回非 0
sdo_xfer() { # <node> <8 hex bytes...>
    local node=$1; shift
    local req_id resp_id data
    req_id=$(printf '%03X' $((0x600 + node)))
    resp_id=$(printf '%03X' $((0x580 + node)))
    data=$(printf '%s' "$*" | tr -d ' ')
    if [ "$DRY" = 1 ]; then
        echo "DRY cansend $IF ${req_id}#${data}" >&2
        echo "60 00 00 00 00 00 00 00"; return 0
    fi
    local out
    ( candump -n 1 -T 1200 "$IF,${resp_id}:7FF" & sleep 0.05
      cansend "$IF" "${req_id}#${data}"; wait ) > /tmp/.sdo.$$ 2>/dev/null
    out=$(awk 'NF>=4 {for(i=4;i<=NF;i++) printf "%s ",$i; print ""}' /tmp/.sdo.$$ | tail -1)
    rm -f /tmp/.sdo.$$
    [ -n "$out" ] || return 1
    echo "$out"
}

# 讀 <node> <idx16> <sub> → 全域 RD_SIZE(1/2/4) RD_VAL(十進位)
sdo_read() {
    local node=$1 idx=$2 sub=$3 lo hi resp cs
    lo=$(printf '%02X' $((idx & 0xFF))); hi=$(printf '%02X' $((idx >> 8)))
    resp=$(sdo_xfer "$node" "40 $lo $hi $(printf '%02X' "$sub") 00 00 00 00") || return 1
    set -- $resp; cs=$((16#$1))
    [ $((cs & 0x80)) -eq 0 ] || return 2                     # 0x80 = abort
    case $cs in
        79) RD_SIZE=1 ;;  # 0x4F
        75) RD_SIZE=2 ;;  # 0x4B
        71) RD_SIZE=4 ;;  # 0x47(3B) 少見,當 4
        67) RD_SIZE=4 ;;  # 0x43
        *)  RD_SIZE=4 ;;  # segmented 等：不支援細讀,回 4B 原樣
    esac
    RD_VAL=$(( 16#$5 | (16#$6 << 8) | (16#$7 << 16) | (16#$8 << 24) ))
    case $RD_SIZE in 1) RD_VAL=$((RD_VAL & 0xFF));; 2) RD_VAL=$((RD_VAL & 0xFFFF));; esac
    return 0
}

# 寫 <node> <idx16> <sub> <val> <size 1|2|4>;驗回應 0x60
sdo_write() {
    local node=$1 idx=$2 sub=$3 val=$4 size=$5 cmd lo hi d resp
    case $size in 1) cmd=2F;; 2) cmd=2B;; 4) cmd=23;; *) return 2;; esac
    lo=$(printf '%02X' $((idx & 0xFF))); hi=$(printf '%02X' $((idx >> 8)))
    d=$(printf '%02X %02X %02X %02X' $((val & 0xFF)) $(((val >> 8) & 0xFF)) \
        $(((val >> 16) & 0xFF)) $(((val >> 24) & 0xFF)))
    resp=$(sdo_xfer "$node" "$cmd $lo $hi $(printf '%02X' "$sub") $d") || return 1
    set -- $resp
    [ "$1" = "60" ]                                          # 0x60 = write OK
}

# 寫入前探測物件大小（讀回應的 cs 決定 1/2/4B）,探不到用預設
probe_size() { # <node> <idx> <sub> <default>
    if sdo_read "$1" "$2" "$3"; then echo "$RD_SIZE"; else echo "$4"; fi
}

step() { echo; echo "== $* =="; }

# ---------- 0) 偵測目前 node-id ----------
step "0) 偵測目前節點（heartbeat 0x701..0x77F,聽 3 秒）"
if [ -z "$CUR_ID" ]; then
    if [ "$DRY" = 1 ]; then CUR_ID=1
    else
        hb=$(candump -n 1 -T 3000 "$IF,700:780" 2>/dev/null | awk '{print $2}' | tail -1)
        if [ -n "${hb:-}" ]; then CUR_ID=$((16#$hb - 0x700))
        else
            echo "  沒聽到 heartbeat。可能：關節仍在 EtherCAT 模式（0x2100=1,出廠值）"
            echo "  → 先用 EYouServoStudio(UART) 或 EtherCAT SDO 把 0x2100 設 2,或用 --current-id 指定後重試"
            CUR_ID=1
            echo "  仍嘗試以出廠預設 node-id=1 溝通..."
        fi
    fi
fi
echo "  目前 node-id = $CUR_ID → 目標 node-id = $NEW_ID"
if [ "$CUR_ID" != "$NEW_ID" ] && candump -n 1 -T 800 "$IF,$(printf '%03X' $((0x700+NEW_ID))):7FF" 2>/dev/null | grep -q .; then
    echo "  ！匯流排上已有 node $NEW_ID 的 heartbeat — node-id 撞車,一次只接一顆新關節。中止。"
    exit 1
fi

# ---------- 1) 切控制權 0x2100=2 ----------
step "1) 0x2100=2 切控制權到 CANopen"
SZ=$(probe_size "$CUR_ID" 0x2100 0 2)
if sdo_read "$CUR_ID" 0x2100 0 && [ "$RD_VAL" = 2 ]; then
    echo "  已是 CANopen 模式（0x2100=2）,跳過"
elif sdo_write "$CUR_ID" 0x2100 0 2 "$SZ"; then
    echo "  OK（size=${SZ}B）"
else
    echo "  ！SDO 無回應/被拒。若關節在 EtherCAT 模式,CAN 口可能不收 SDO —"
    echo "    改用 EYouServoStudio(UART) 或 EtherCAT 寫 0x2100=2 後重跑本腳本。"
    exit 1
fi

# ---------- 2) 設 node-id 0x26A0 ----------
step "2) 0x26A0=$NEW_ID 設 node-id"
if [ "$CUR_ID" = "$NEW_ID" ]; then
    echo "  node-id 已是 $NEW_ID,跳過"
else
    SZ=$(probe_size "$CUR_ID" 0x26A0 0 1)
    sdo_write "$CUR_ID" 0x26A0 0 "$NEW_ID" "$SZ" \
        && echo "  OK（size=${SZ}B;重上電後生效）" \
        || { echo "  ！寫入失敗"; exit 1; }
fi

# ---------- 3) 存檔 ----------
step "3) 存檔（約 3 秒,嚴禁斷電！）"
if [ "$SKIP_SAVE" = 1 ]; then
    echo "  --skip-save,略過"
else
    SZ=$(probe_size "$CUR_ID" 0x2130 0 1)
    if sdo_write "$CUR_ID" 0x2130 0 1 "$SZ"; then
        echo "  0x2130=1 已下發,等待 4 秒..."
    else
        echo "  0x2130 失敗 → 退路 0x1010:01=\"save\"(0x65766173)"
        sdo_write "$CUR_ID" 0x1010 1 $((0x65766173)) 4 \
            && echo "  0x1010:01 已下發,等待 4 秒..." \
            || { echo "  ！兩種存檔都失敗"; exit 1; }
    fi
    [ "$DRY" = 1 ] || sleep 4
fi

# ---------- 4) 重上電 + 驗證 ----------
step "4) 請將關節斷電再上電（node-id 重上電才生效）"
if [ "$DRY" = 1 ]; then
    echo "  DRY：略過等待"
else
    echo -n "  等待 node $NEW_ID heartbeat（0x$(printf '%03X' $((0x700+NEW_ID)))）"
    ok=0
    for _ in $(seq 1 60); do
        if candump -n 1 -T 1000 "$IF,$(printf '%03X' $((0x700+NEW_ID))):7FF" 2>/dev/null | grep -q .; then
            ok=1; break
        fi
        echo -n "."
    done
    echo
    [ "$ok" = 1 ] || { echo "  ！60 秒沒等到 heartbeat,檢查供電/接線/終端電阻"; exit 1; }
    echo "  heartbeat OK"
fi

step "驗證讀回（記入清冊）"
NAME="?"; RESO="?"; GEAR_N="?"; GEAR_D="?"; BAUD="?"
if sdo_read "$NEW_ID" 0x1008 0; then
    NAME=$(printf '%b' "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x' \
        $((RD_VAL & 0xFF)) $(((RD_VAL >> 8) & 0xFF)) \
        $(((RD_VAL >> 16) & 0xFF)) $(((RD_VAL >> 24) & 0xFF)))" | tr -cd '[:print:]')
fi
sdo_read "$NEW_ID" 0x2025 0 && RESO=$RD_VAL     # 編碼器解析度（19-bit=524288 待實證）
sdo_read "$NEW_ID" 0x26A2 0 && GEAR_N=$RD_VAL   # 減速比分子
sdo_read "$NEW_ID" 0x26A3 0 && GEAR_D=$RD_VAL   # 減速比分母
sdo_read "$NEW_ID" 0x26A1 0 && BAUD=$RD_VAL
echo "  名稱=$NAME  解析度(0x2025)=$RESO  減速比=$GEAR_N/$GEAR_D  波特率=$BAUD"
[ "$RESO" = "?" ] || [ "$RESO" = 524288 ] || \
    echo "  ！解析度 ≠ 524288（19-bit 假設）→ 依實測更新 robot_config 校正表"

# ---------- 5) 清冊 ----------
step "5) 佈建清冊 → $LEDGER"
[ -f "$LEDGER" ] || echo "date,interface,node_id,label,name,resolution_0x2025,gear_0x26A2,gear_0x26A3,baud_0x26A1" > "$LEDGER"
echo "$(date +%F_%T),$IF,$NEW_ID,$LABEL,$NAME,$RESO,$GEAR_N,$GEAR_D,$BAUD" >> "$LEDGER"
tail -1 "$LEDGER" | sed 's/^/  /'
echo
echo "完成。請在關節貼上標籤：${LABEL}（node ${NEW_ID}）。"
