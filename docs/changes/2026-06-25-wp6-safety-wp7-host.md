# WP6 安全 + WP7 上位機介面

- 日期：2026-06-25
- 分支：`claude/keen-ptolemy-ri2f8y`

## 變更摘要

- **WP6**：新增 `firmware/safety/safety.[ch]`（系統狀態機、驅動故障/通訊看門狗/急停、安全控制字）。
  - `dual_arm` 加 `dual_arm_set_safe_stop()`;`app_main` 整合 safety 並提供 `app_set_estop()/app_sys_state()`。
- **WP7**：新增 `firmware/host/host_if.[ch]`（命令/遙測協定,含 TLM_MOTOR 扭矩/電流遙測,傳輸無關）。
- 新增 `docs/design/wp6-safety-wp7-host.md`。

## 動機 / 背景

使用者要求把 WP6、WP7 做完。WP7 遙測預留扭矩/電流欄位,銜接 Python 假硬體的馬達出力/電流回報。

## 設計重點

- 安全：故障/逾時/急停 → 安全控制字（quick stop / disable voltage）覆寫 L1 下發。
- 協定：簡潔二進位框架（SYNC/TYPE/LEN/PAYLOAD/XOR）,命令與遙測雙向。

## 影響範圍

- 新增 `firmware/safety/`、`firmware/host/`;修改 `dual_arm.[ch]`、`app_main.c`。

## 驗證方式

- C 模擬器仍可編譯執行（safety 接入不影響正常流程,allow=true 時照常運動）。
- 協定編解碼可離線單元測試。

## 關聯

- `wp6-safety-wp7-host.md`、`dual-arm-control-plan.md`
