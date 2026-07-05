# 設定檔：軸表/DH/IK 外部化（項目 7）

- 日期：2026-07-05
- 分支：`feature/h-sdo-bg-escalation`
- 類型：feat + test

## 變更摘要

1. **`robot_config.c` 增設定檔覆寫**（`robot_config_apply/load/reset`）：
   key=value 逐鍵覆寫編譯期預設。鍵空間：
   `joint.<i|*>.counts_per_rad|q_min|q_max|vmax|amax|offset_rad`（`*` 萬用）、
   `ik.*`、`q_init.<i>`、`base.left|right.x|y|z`、`dh.left|right.<i>.a|alpha|d|theta`。
   載入採**兩趟制**：先全檔驗證、全過才套用——壞檔不半套用（回 -2，
   訊息帶行號）。`reset()` 還原編譯期預設（預設值改存 `static const`）。
2. **pc_master `--config FILE`**：BUS_UP 之前載入（js/kin 於 init 複製
   設定）；缺檔/壞檔 → 拒絕啟動（exit 2）。
3. **`firmware/pc/dual_arm.cfg.example`**：註解完整的範例（19-bit
   counts_per_rad、PHU20 肩軸降速、肘限位、IK、q_init、肩基座）。

## 動機 / 背景

軸表/DH/IK 佔位值全部硬編在 `robot_config.c`，WP0.4 實機量測值
（0x2025 讀值確認的 counts_per_rad、機構限位、DH、校零偏移）逐機
不同——放檔案免重編譯、每台機器一份，佈建 SOP（C0.3）量完直接填。
解析器在 app 層（平台無關，僅 `robot_config_load` 用 stdio），
F746 之後可從外部儲存餵同一格式。

## 影響範圍

- 修改：`app/robot_config.[ch]`（+apply/load/reset；預設值搬進
  `static const`，行為不變）、`pc/pc_master_main.c`（--config）、
  tests Makefile
- 新增：`pc/dual_arm.cfg.example`、`tests/test_config.c`
- 不給 `--config` 時零行為變化（編譯期預設仍是唯一來源）。

## 驗證方式

- `firmware/tests` **5083 檢查 0 失敗**（+35）：逐鍵 apply（萬用軸/
  越界/未知鍵/段數錯）、檔案載入（註解/空白/行內註解/`=` 兩側空白）、
  壞檔整檔放棄（第 1 鍵不得殘留）、格式錯/非數值、reset 還原。
- E2E：`--config dual_arm.cfg.example` 套用 22 鍵後正常 RUN；
  缺檔與壞鍵檔均 exit 2 拒絕啟動。

## 關聯

- 設計：`harness-agent-loop-engine-plan.md`（純軟體收尾項 7）
- 配套：`provision_joint.sh`（C 分支，佈建量測值的來源）、
  WP0.4（實機量測，值的填入時機）
