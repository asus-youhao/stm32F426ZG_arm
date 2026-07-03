# PC 端 CANopen 主站（SocketCAN）

不接 Nucleo 板也能測**同一套韌體**：把 `firmware/` 的 L0–L4 C 堆疊直接編成
Linux 執行檔，底層 bxCAN 換成 SocketCAN（`co_bxcan_socketcan.c`），
對端跑 `sim_py/can_slave.py` 模擬 14 顆 EYOU PHU CiA402 從站。

拓樸（對應 F746 的 CAN1/CAN2 雙通道）：

- `vcan0` = 左臂 bus（node 1..7：PHU20×2、PHU17×2、PHU14×3）
- `vcan1` = 右臂 bus（node 1..7，同上）

被抽換/新增的檔案只有三個，其餘 `canopen/ control/ safety/ app/ test/`
與板端共用同一份源碼：

| 檔案 | 取代 | 作用 |
| ---- | ---- | ---- |
| `co_bxcan_socketcan.c` | `canopen/co_bxcan.c` | SocketCAN 收發（非阻塞;TX 滿→CO_ERR_TX 對齊 mailbox 語意） |
| `hal_linux.c` | `sim/hal_shim.c` | 真實牆鐘 HAL_GetTick/Delay（SDO 逾時、看門狗需要） |
| `pc_master_main.c` | `board/main.c` | clock_nanosleep 500Hz 迴圈 + stdin 互動命令 |

## 使用

```bash
# 1) 建 vcan0/vcan1（需 sudo，開機後需重跑）
./setup_vcan.sh

# 2) 起 14 顆假從站（需 pip install python-can）
./run_slaves.sh

# 3) 編譯並跑主站（另一個終端）
make
./pc_master --bringup 1        # 先跑 WP2 單軸 bring-up 再進全棧
./pc_master                    # 直接進全棧
./pc_master --right none       # 單臂（只有 vcan0）
./pc_master --seconds 10       # 跑 10 秒自動結束（CI 用）
```

互動命令（stdin）：`j <idx> <rad>` 關節點到點、`e <0|1>` 急停、
`p` 印雙臂末端位姿、`q` 離開。

## 無 sudo 的替代方案（user namespace）

`unshare -rn` 建的 network namespace 內可直接建 vcan（kernel 自動載入模組），
適合無 root 的機器與 CI：

```bash
unshare -rn bash -c '
  ip link add dev vcan0 type vcan && ip link set up vcan0
  ip link add dev vcan1 type vcan && ip link set up vcan1
  (cd ../sim_py && python3 can_slave.py --interface socketcan --channel vcan0 \
      --nodes 1:PHU20,2:PHU20,3:PHU17,4:PHU17,5:PHU14,6:PHU14,7:PHU14 &) 
  (cd ../sim_py && python3 can_slave.py --interface socketcan --channel vcan1 \
      --nodes 1:PHU20,2:PHU20,3:PHU17,4:PHU17,5:PHU14,6:PHU14,7:PHU14 &)
  sleep 1
  ./pc_master --bringup 1 --seconds 10
'
```

> 注意：namespace 內的 vcan 與外部隔離，`candump` 等工具也要在同一個
> namespace 內跑（`nsenter` 或同一個 `unshare` shell）。

## 限制

- 非 RT 核心下 tick 遲到可到數 ms（實測 max ~2.5ms @500Hz）;要更緊可用
  `chrt -f 50 ./pc_master`（需權限）或 PREEMPT_RT。
- 這條路測的是**協定與控制邏輯**，不是 bxCAN 硬體時序;燒板前仍建議跑一次
  `firmware/Makefile` 的板端 bring-up。
- 真實 CANable/真馬達的情境見 `sim_py/README.md` 與 P5 變更文件。
