# 06 · 專案 B：Linux RT EtherCAT 1kHz 主站（project-ecat-master.html）

repo EtherCAT 系列分支（`feature/linux-rt-ethercat-master` 等）的實戰內容成頁。

- 架構 SVG：RT PC（SOEM/IgH + 1ms 迴圈）→ 14 站 PHU 菊鏈（DC/CoE/ESM/ESI 註記）。
- A vs B 對照表（頻率/同步/佈線/難點）。
- 里程碑 M1~M6：主站機建置（`setup_master_host.sh`）→ 從站掃描/ESI → PREOP SDO →
  SAFEOP→OP+DC → 單軸 CSP@1kHz（52953088 counts/圈警示）→ 14 軸+抖動報告。
- 實戰紀錄表（git 史實）：SAFEOP 卡關診斷工具（wd_read/phu_state）、0x2100 廠商物件
  根因（gallop_ws 參考主站比對）、EtherCAT 寫入被控制權鎖死 → 定案走 CANopen。
- DoD 特別條款：被廠商權限擋住時，把驗證證據寫成設計文件也算完成 —
  「技術選型輸給生態整合是常態」是本頁核心教訓。
