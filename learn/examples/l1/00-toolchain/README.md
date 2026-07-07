# L1-2 · 開發環境（無程式碼，指令練習）

對照教學：`learn/l1-basic.html` 第 2 節。

```bash
# 1) 交叉工具鏈 + 燒錄工具
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi gdb-multiarch \
                 openocd stlink-tools

# 2) 驗證
arm-none-eabi-gcc --version
st-info --probe          # 接上 Nucleo 板後應列出 ST-Link

# 3) 本 repo 的兩種建置（回到 repo 根目錄）
cd firmware/tests && make          # PC 上跑單元測試
cd ../.. && cmake -S firmware -B build && cmake --build build
./build/phu_sim_demo               # PC 上跑 HOST 模擬
```

之後每個 L1 範例都有兩種身分：`make run` 在 PC 模擬（用 `common/hal_stub.h`），
或把 `main.c` 丟進 CubeMX 專案燒真板（拿掉 `-DHOST_SIM`）。
