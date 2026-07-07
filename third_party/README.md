# third_party — vendored EtherCAT 主站原始碼

本目錄放**完整 vendor 進 repo** 的第三方 EtherCAT 主站堆疊原始碼
（`git archive` 自釘定 commit 取出，不含 `.git`、不含編譯產物）。
目的：主站環境離線自足、可完全重現，不依賴上游是否還在。

> ⚠️ **授權隔離**：這兩套都是 **GPL** 授權，刻意放在 `third_party/`
> 與本專案自有程式碼（`firmware/`）分開。本 repo 自有韌體透過
> `firmware/ecat/ec_master.h` 門面與主站互動；**產品化前 GPL 連結
> 邊界須經法務確認**（見 `docs/design/ethercat-coe-master-plan.md` §11）。

## 內容

| 目錄 | 上游 | 釘定 commit | 版本 | 授權 |
| --- | --- | --- | --- | --- |
| `SOEM/` | github.com/OpenEtherCATsociety/SOEM | `2f73eaa` | 2.x（context API `ecx_*`） | GPLv2（rt-labs 另售商業授權） |
| `ethercat/` | gitlab.com/etherlab.org/ethercat | `beb2bf07` | 1.6.9-8（stable-1.6，含 igc_6.8） | GPLv2（kernel 模組）+ LGPL（userspace lib） |

實測平台：Ubuntu 24.04.4 + `6.8.1-1052-realtime`（asus-gx701，2026-07-06/07）。

## 建置

```bash
tools/ecat_bringup/setup_master_host.sh          # 預設從本目錄 vendored 源建置
tools/ecat_bringup/setup_master_host.sh --clone  # 改從上游重新 clone（更新版本用）
```

IgH 在 6.8 內核用 **generic driver**（`--enable-generic`）——原生
e1000e/igc 驅動只到 6.4。因此 `ethercat/devices/` 下各內核版本的
網卡驅動（e1000e/igb/igc/stmmac…，佔本樹約 88% 體積）**目前建置
不編譯**，保留僅為「完整原始碼」；日後確定不用可安全刪除瘦身。

## 相關

- 建置/操作：`tools/ecat_bringup/`（含 bring-up/診斷工具）
- RT 調校：`firmware/rt/rt_setup.sh`
- 設計：`docs/design/{ethercat-coe-master-plan,linux-rt-ethercat-master-plan,linux-igh-ethercat-master-plan}.md`
