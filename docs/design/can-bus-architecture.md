# 通訊匯流排架構 — CAN / CAN-FD 評估（Dual-Arm 7+7 軸）

- 適用對象：雙臂機器手臂,每臂 **7 軸(7 DoF)**,共 **14 軸**
- 文件層級：需求 / 架構概述
- 狀態：草案(draft)

## 1. 結論摘要

- **STM32F746ZG 不支援 CAN-FD**。它內建 **2 × bxCAN(Classic CAN 2.0B,最高 1 Mbps)**,沒有 FDCAN 周邊。
- 若需要 CAN-FD,建議:
  1. **改用 STM32H743ZI**(2 × FDCAN,封裝/腳位接近 F746ZG,Cortex-M7 480 MHz)— 改動最小;或
  2. STM32G4(最多 3 × FDCAN);或
  3. F746 + 外接 **MCP2518FD**(SPI 介面,每顆一路 CAN-FD)。
- 若維持 Classic CAN:建議 **一臂一路 CAN**(共 2 路),但 1 kHz 等級閉迴路頻寬吃緊。

## 2. STM32F746 vs CAN-FD 能力比較

| 項目         | STM32F746ZG          | STM32H743ZI         | STM32G4              |
| ------------ | -------------------- | ------------------- | ------------------- |
| CAN 周邊     | 2 × bxCAN            | 2 × FDCAN           | 最多 3 × FDCAN      |
| 協定         | Classic CAN 2.0B     | Classic + CAN-FD    | Classic + CAN-FD    |
| 仲裁相速率   | ≤ 1 Mbps             | ≤ 1 Mbps            | ≤ 1 Mbps            |
| 資料相速率   | —(不支援 FD)       | 可達數 Mbps         | 可達數 Mbps         |
| Payload      | 8 bytes              | 64 bytes            | 64 bytes            |
| 核心         | Cortex-M7 @216 MHz   | Cortex-M7 @480 MHz  | Cortex-M4 @170 MHz  |

> 重點:**F7 系列一律是 bxCAN(Classic),無 FDCAN**;CAN-FD 需 H7 / G4 / G0 / L5 等。

## 3. 頻寬粗估(為何 CAN-FD 重要)

假設每軸每控制週期需「1 命令 frame + 1 回授 frame」。

- **Classic CAN @1 Mbps**:含位元填充與框架開銷,實務有效約 5,000–6,000 frame/s。
  - 一臂 7 軸 = 14 frame/週期 → 約 **≤ 400 Hz** 才不塞滿單路匯流排。
  - 14 軸全擠一路 → 28 frame/週期 → 更低,**不建議單路帶雙臂**。
- **CAN-FD(例 1M 仲裁 / 5M 資料,64B payload)**:可把多軸資料打包進單一 frame,有效吞吐遠高於 Classic,**1 kHz+ 控制可行**。

結論:**需要 1 kHz 等級力矩/電流閉迴路 → 用 CAN-FD(H7/G4)**;若 MCU 只做較低頻位置命令下發 + 狀態彙整 → F746 雙路 Classic CAN 勉強可行。

## 4. 匯流排拓樸(建議)

```
方案 A（建議）：一臂一路 CAN/CAN-FD
┌──────────────┐
│  MCU         │
│  CAN/FDCAN1 ─┼──► [L1][L2][L3][L4][L5][L6][L7]   左臂 7 軸
│  CAN/FDCAN2 ─┼──► [R1][R2][R3][R4][R5][R6][R7]   右臂 7 軸
└──────────────┘
優點：兩臂負載隔離、互不干擾、頻寬各自獨立、故障隔離佳。
```

```
方案 B（不建議於高頻）：單路帶 14 軸
CAN ──► [L1..L7][R1..R7]
缺點：頻寬擁擠、單點故障影響雙臂、難達高控制頻率。
```

採 **方案 A**:F746 的 2 路 bxCAN、或 H743 的 2 路 FDCAN,剛好各帶一臂 7 軸。

## 5. 待決議項(Open Questions)

1. 目標控制頻率?(決定 Classic CAN 是否足夠 → 影響選 F746 或 H743)
2. 伺服驅動器是否為 CAN/CAN-FD 介面?協定(CANopen / 自訂)?
3. 力矩/電流閉迴路放在 MCU 端還是關節驅動器端?
4. 是否接受改 MCU 為 STM32H743ZI(以取得原生 CAN-FD)?
5. CAN 終端電阻、匯流排長度、節點數與線束規劃?

## 6. 建議下一步

- 先拍板「目標控制頻率」與「驅動器介面」兩項,即可定 MCU(F746 Classic CAN vs H743 CAN-FD)。
- MCU 拍板後,另開底層設計文件(FDCAN/bxCAN 周邊配置、時序、DMA、ISR、訊息 ID 規劃)。
