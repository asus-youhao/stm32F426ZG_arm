# vendor SOEM + IgH 完整原始碼進 repo（third_party/）

- 日期：2026-07-07
- 分支：`feature/linux-rt-ethercat-master`
- 類型：vendor（第三方原始碼納入）+ docs

## 變更摘要

把在 gx701 建立的兩套 EtherCAT 主站堆疊**完整原始碼**收進 repo 的
`third_party/`：
- `third_party/SOEM/`（128 檔，1.1 MB）— commit `2f73eaa`，SOEM 2.x
- `third_party/ethercat/`（2390 檔，83 MB）— commit `beb2bf07`，IgH 1.6.9-8

以 `git archive <ref>` 自釘定 commit 取出——**只含該 commit 追蹤的
原始碼，不含 `.git`、不含編譯產物（.o/.ko/.la）**。

## 動機 / 背景

先前 SOEM/IgH 僅存在於 gx701 的 `~/robot_test/` 與對話紀錄，repo 無
任何主站堆疊來源。使用者要求「完整原始碼複製進 repo」以求離線自足、
可完全重現（不依賴上游是否還在、版本是否漂移）。此前一版做的
`setup_master_host.sh` 只是「從上游重建的腳本」，非原始碼本身。

## 授權與體積（決策記錄）

- 兩套皆 **GPL**（SOEM GPLv2；IgH GPLv2 kernel 模組 + LGPL userspace
  lib），刻意放 `third_party/` 與自有 `firmware/` 分離；產品化前 GPL
  連結邊界須法務確認（設計文件 §11）。LICENSE 檔隨源保留
  （`SOEM/LICENSE.md`、`ethercat/COPYING`+`COPYING.LESSER`）。
- IgH 83 MB 中約 88%（2100 檔）是 `devices/` 各內核版本網卡驅動
  （e1000e/igb/igc/stmmac…）。**現行 `--enable-generic` 建置不編譯
  它們**，保留僅為「完整原始碼」；日後可安全刪除瘦身。
- 現有 repo `.git` 已達 5.8 G，新增 83 MB 佔比 ~1.4%，經使用者確認
  接受。

## 影響範圍

- 新增：`third_party/{SOEM,ethercat}/`、`third_party/README.md`
- 修改：`tools/ecat_bringup/setup_master_host.sh`——**預設改從 vendored
  源建置**（`fetch_src` 複製 `third_party/<name>`），`--clone` 才回上游
  取。離線機（如日後 NUC）clone repo 即可建，無須網路。
- 不影響韌體/主站程式邏輯（純納入外部源 + 建置腳本切換來源）。

## 驗證方式

- `git archive` 產物核對：SOEM 128 檔、IgH 2390 檔，LICENSE 齊全；
  `find` 確認無 `.git`/`.o`/`.ko`/`.la`；`git check-ignore` 確認未被
  `.gitignore` 擋。
- `bash -n setup_master_host.sh` 語法通過；VENDOR 路徑
  （`../../third_party`）解析正確。
- 鎖定 commit 與 gx701 實際 checkout 逐項一致（前一份文件已核對）。

## 關聯

- 前置：`2026-07-07-ecat-master-host-build.md`（建置腳本）、
  `2026-07-06-u24-rt-verification.md`
- 設計：`ethercat-coe-master-plan.md` §6.2/§11（原規劃即預期 SOEM
  vendor tree；§11 GPL 風險）
