# 2026-07-10 EtherCAT 匯流排分析 UI（Wireshark 式）——ecat_sniff.py + 互動 viewer

## 變更摘要

免額外硬體,把「F746 板端主站 ↔ PC 假 PHU 從站」的實際線上流量做成
Wireshark 風格的互動 UI：

1. **`tools/ecat_sniff.py`（新）**：AF_PACKET（ETH_P_ALL,本機送出幀只派送
   給 ALL taps）抓 0x88A4 → 解碼 EtherCAT frame/datagram（cmd/暫存器名/wkc）、
   AL 狀態機、SII、CoE mailbox SDO、FMMU 過程資料（factory 33B/29B 佈局,
   CiA402 cw/sw 語意）→ JSON。方向由 `PACKET_OUTGOING` 判定（主站幀 vs 回幀）。
   取樣：非週期幀全留;LRW 只留內容有變化者（8821 幀 → 1150 幀）。
2. **`tools/ecat_bus_viewer_tpl.html`（新）**：單檔互動 viewer 模板
   （`/*__DATA__*/null` 注入 JSON）——工具列（文字過濾＋方向/協定 chips）、
   SVG 時間軸（主站/回幀密度＋軸0 位置曲線,點擊跳轉）、封包列表、
   解碼樹＋hex dump（乙太頭高亮）、鍵盤導覽、亮/暗雙主題（token 化,
   `data-theme` 覆寫）、統計列。無外部資源（Artifact CSP 相容）。
3. **CAN 對照檢視（SavvyCAN 式,同日補）**：第二個 tab 把 EtherCAT 事件映射成
   CANopen 幀列（時間/COB-ID/名稱/Node/Len/Data/解碼）——CoE SDO 的 8B payload
   原生就是 CANopen SDO 幀（0x600/0x580+node）;LRW 每軸輸出/輸入 ≙
   RPDO1(0x200+n,cw+tgt)/TPDO1(0x180+n,sw+pos);AL 狀態機 ≈ NMT(0x000)/
   Heartbeat(0x700+n);err≠0 → EMCY(0x080+n)。點列跳回封包解碼。
4. **`tools/ecat_live.py`（新）：real-time 模式**——本機 SSE server
   （`python3-rawnet tools/ecat_live.py --iface <nic>` → http://localhost:8792）,
   同一套 viewer UI 即時串流（變化幀＋每秒心跳樣本,上限 6000 幀）。
   Artifact 版因 CSP 擋 WebSocket/fetch 只能是靜態快照,即時看本機開 live。
5. 首個 capture 已發佈為 Artifact（板子重置全流程：掃鏈→SII→FMMU/SM/DC→
   SAFEOP→CoE SDO→OP→CiA402 使能→CSP 位置爬坡）。

## 重跑指令（G16）

```bash
# 抓 18s,中途重置板子取得完整啟動序列
(python3-rawnet tools/ecat_sniff.py --iface enx00e04c6809b6 --seconds 18 \
   --out /tmp/cap.json & sleep 1.5; st-flash reset; wait)
# 注入模板產出單檔 HTML
python3 - <<'EOF'
tpl=open('tools/ecat_bus_viewer_tpl.html').read()
open('/tmp/ecat_bus_viewer.html','w').write(
    tpl.replace('/*__DATA__*/null', open('/tmp/cap.json').read(), 1))
EOF
```

## 驗證方式

- Playwright 實測（localhost 服務）：亮/暗主題渲染、SDO 快速過濾（174 幀,
  修掉誤把 mailbox 輪詢全算進來的分類）、解碼樹/hex 窗格、時間軸皆正常;
  console 無錯誤（僅本地 favicon 404）。
- CAN 對照抽查：RPDO1 `0F 00 42 03 00 00`=cw 0x000F+tgt 834、SDO 請求
  `40 00 1C 00…`=標準 upload;884 列（TPDO1 708/SDO 116/RPDO1 48/NMT+HB 12）。
- live 實測：串流中重置板子,啟動爆發即時進 UI（38k+ 幀抓取,1310 顯示）。
- 抓包內容抽查：BRD/APWR 站址、SII 讀、FMMU/SM/DC 設定、AL INIT→PREOP→
  SAFEOP→OP、SDO 讀 0x1C00/0x1000、LRW cw 0x06→0x07→0x0F、pos 爬坡全數可見。

## 影響範圍

- 新增：`tools/ecat_sniff.py`、`tools/ecat_bus_viewer_tpl.html`、`tools/ecat_live.py`、本文件
- 純工具/文件,韌體與建置零影響

## 關聯

- 資料源：`2026-07-10-se5-se6-harness-board.md` 的 ecat-app 韌體 + `ecat_slave.py --factory-pdo`
- Branch：`feature/stm32-ethercat-master`
