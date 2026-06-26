# 上位機 Host UI（假硬體）

純前端單檔 `host_ui.html`，內建假 PHU 馬達模型（與 `sim_py/phu_motor.py` 同樣的扭矩/電流模型），
扮演「上位機」介面：可看雙臂動畫、每顆馬達力/電流、CANopen frame log，並下命令（使能/模式/急停/關節 jog）。

## 開啟

直接用瀏覽器開 `host_ui.html`（無需伺服器、無外部相依）。

## 內容

- 雙臂視覺化（段色依扭矩;綠→紅 = 低→高負載）
- 14 軸馬達遙測表：力(N·m)、電流(A)、CiA402 狀態
- CANopen frame log：RPDO(下發 cw/pos) / TPDO(回授 sw)
- 命令：Enable、Demo 揮手、模式切換、E-STOP、左臂關節滑桿
- 系統狀態、CAN1/CAN2 流量、500 Hz 控制

## 定位

此 UI 為**示意/教學用上位機**（前端模擬），與真機部署無關；真機上位機可改走
WP7 的 `host_if` 二進位協定（CMD_*/TLM_*）對接 MCU。

## can_monitor.html — 即時 CAN 監控台

另開 `can_monitor.html`：即時顯示**所有 COB-ID(CAN ID)**的 TX/RX、類型、data、解碼值、次數、age，
以及各 node 的 CiA402 狀態與力/電流。用於「即時看見所有 cmd RX/TX 與 CAN ID 數值/狀態」。
