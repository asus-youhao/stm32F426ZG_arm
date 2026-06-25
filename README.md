# stm32F426ZG_arm

STM32F426ZG（ARM Cortex-M4F）韌體開發專案。

## 開發規範

- 分支策略：採用 **Git Flow**（`main` / `develop` / `feature/*` / `release/*` / `hotfix/*`），詳見 [CLAUDE.md](./CLAUDE.md)。
- 文件規範：**所有改動都要在 [`docs/`](./docs) 撰寫對應的 markdown 文件**。

## 快速開始

```bash
# 從 develop 切出功能分支
git checkout develop
git checkout -b feature/<功能名稱>
```

## 文件

所有文件集中於 [`docs/`](./docs)，變更紀錄位於 [`docs/changes/`](./docs/changes)。
