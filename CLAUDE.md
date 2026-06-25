# CLAUDE.md

本檔案提供 Claude Code（claude.com/claude-code）在此儲存庫工作時的指引。

## 專案簡介

本專案為 **STM32F426ZG** ARM Cortex-M4 微控制器的韌體開發專案。

- MCU：STM32F426ZG（ARM Cortex-M4F，180 MHz，1 MB Flash，256 KB SRAM）
- 架構：ARM 32-bit
- 開發語言：C / C++（嵌入式韌體）

> 隨著專案演進，請持續更新本節（工具鏈、HAL 版本、目錄結構等）。

## 目錄結構

```
.
├── CLAUDE.md          # 本檔案：Claude Code 工作指引
├── README.md          # 專案說明
└── docs/              # 所有文件（每次改動都要在此記錄）
    ├── README.md      # 文件索引
    └── changes/       # 變更紀錄，每筆改動一份 markdown
```

## Git Flow 分支策略

本專案採用 **Git Flow** 分支模型。

### 長期分支

| 分支       | 用途                                       |
| ---------- | ------------------------------------------ |
| `main`     | 正式版本，僅存放可釋出的穩定程式碼         |
| `develop`  | 主開發分支，整合所有已完成的功能           |

### 短期分支（從對應來源分支切出，完成後合併回去）

| 前綴         | 來源       | 合併目標            | 用途             |
| ------------ | ---------- | ------------------- | ---------------- |
| `feature/*`  | `develop`  | `develop`           | 新功能開發       |
| `release/*`  | `develop`  | `main` + `develop`  | 發版前整理       |
| `hotfix/*`   | `main`     | `main` + `develop`  | 線上緊急修復     |

### 慣用流程

```bash
# 開新功能
git checkout develop
git checkout -b feature/<功能名稱>
# ...開發、commit...
git checkout develop
git merge --no-ff feature/<功能名稱>

# 發版
git checkout -b release/<版本號> develop
# ...修正、bump 版本...
git checkout main && git merge --no-ff release/<版本號>
git tag -a v<版本號>
git checkout develop && git merge --no-ff release/<版本號>

# 緊急修復
git checkout -b hotfix/<問題> main
# ...修正...
git checkout main && git merge --no-ff hotfix/<問題>
git checkout develop && git merge --no-ff hotfix/<問題>
```

### Commit 訊息規範

採用 Conventional Commits：

```
<type>(<scope>): <簡述>

<內文，說明動機與細節>
```

常用 `type`：`feat`、`fix`、`docs`、`refactor`、`test`、`chore`、`build`。

## 文件撰寫規範（重要）

> **所有程式或設定的改動，都必須撰寫對應的文件（markdown）。**

每次改動請於 `docs/changes/` 新增一份文件，命名規則：

```
docs/changes/YYYY-MM-DD-<簡短描述>.md
```

每份變更文件至少包含：

1. **變更摘要**：做了什麼
2. **動機 / 背景**：為什麼要改
3. **影響範圍**：影響的檔案、模組、行為
4. **驗證方式**：如何測試確認（編譯、燒錄、量測等）
5. **關聯**：相關 commit / branch / issue

完成後請更新 `docs/README.md` 的文件索引。

## 對 Claude Code 的工作要求

1. 開發前確認目前所在分支符合 Git Flow（功能開發應在 `feature/*`）。
2. 每完成一項改動，**務必**在 `docs/changes/` 補上變更文件。
3. Commit 訊息遵循 Conventional Commits。
4. 除非使用者明確要求，否則不要建立 Pull Request。
5. 變更若影響硬體行為（腳位、時脈、周邊設定），需在文件中特別標註。
