# 視覺化橋：3D viewer 接 pc_master telemetry ring（項目 4）

- 日期：2026-07-05
- 分支：`feature/viz-pc-bridge`（stacked 於 `feature/h-sdo-bg-escalation`——
  依賴 telemetry ring（H2）與 `--bus ethercat`（H5））
- 類型：feat + test

## 變更摘要

1. **`app_tele_t` 擴充**：遙測快照從只帶 J0 改為全 14 軸
   （`sw/pos/tgt[APP_TELE_NJ]`）——3D 視覺化的前置。
2. **`firmware/pc/viz_bridge.[ch]`**：pc_master 側 UDP 橋（非 RT，
   socket 只活在主執行緒）。資料面：telemetry 快照 → JSON（q/qt 用
   `js_counts_to_rad` 轉 rad）→ 發給最近來訊的對端；命令面：對端送與
   stdin 相同的文字命令，走同一個 `handle_cmd` → cmd ring → RT。
   新旗標 `--viz PORT`。
3. **`ws_server.py --bridge PORT`**：第四種模式——不跑物理，鏡射
   pc_master 遙測進 `sim.M`（與 monitor 模式同策略，3D/前端零改動），
   UI 命令（jog/estop/enable/preset）轉成 pc_master 文字命令回送；
   preset 展開成逐軸命令。每秒送 "hello" 訂閱保活。
4. **§5.2 階梯接線 + `--miss-pct`**：排查中發現非 RT 開發機啟動爆發期
   會瞬間超 5% miss 視窗，而 pc_master 沒設 `degrade_dt_us`——升級階梯
   跳過降頻直達 SAFE_STOP（監督照設計動作，是接線缺項）。補上
   `degrade_dt_us = 2×dt`（超標先降頻一次）與 `--miss-pct N` 旋鈕
   （開發機放寬用；真機用預設 5%）。

## 動機 / 背景

搭配 `--bus ethercat`（sim 後端），整條「瀏覽器 UI → WS → UDP →
cmd ring → RT loop engine → telemetry ring → UDP → WS → 3D」在無任何
硬體下可跑——資料源是真 C 主站與真控制堆疊，不是 Python 假馬達。
既有 3D viewer / web monitor 全部直接重用。

## 影響範圍

- 修改：`app/app_io_agents.[ch]`（tele 全軸化）、`pc/pc_master_main.c`
  （--viz/--miss-pct/降頻接線）、`pc/Makefile`、`sim_py/ws_server.py`
  （bridge 模式）、`pc/README.md`（demo 用法）
- 新增：`pc/viz_bridge.[ch]`
- RT 路徑零觸碰（UDP 只在主執行緒）；未給 `--viz` 行為不變。
- 板端不受影響（viz_bridge 僅 pc 目標；app_tele_t 變大 ~130B/筆）。

## 驗證方式

- `firmware/tests` 5022 檢查 0 失敗（tele 結構改動迴歸）。
- **E2E L1（UDP ↔ 真 pc_master）**：9/9——訂閱即收遙測、14 軸欄位齊、
  全軸 OP_ENABLED、`j 3 0.4` 後 q[3] 收斂 0.4、他軸不動、
  `e 1`/`e 0` 急停復歸來回。
- **E2E L2（WS 客戶端 ↔ ws_server --bridge ↔ pc_master）**：8/8——
  source=pc_master、雙臂 jog、estop/enable 經 UI 命令路。
- **E2E L3（瀏覽器）**：playwright 開 `viewer3d.html`——右上角顯示
  「源：pc_master:udp:9601」，按「姿態：示範彎曲」雙臂即擺位
  （UI→bridge→pc_master→RT→遙測→3D 全鏈路截圖存證）。

## 關聯

- 設計：`harness-agent-loop-engine-plan.md` §5.3（telemetry ring 消費端）
- 前置：`4c2a65a`（trace ring）、H2/H5
- 待辦：真機 CANopen 500 Hz 時同一條 `--viz` 即為現場儀表
