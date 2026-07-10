# PC 端 CANopen 主站 — Linux SocketCAN（vcan0=左臂, vcan1=右臂）
# 用法：
#   ./setup_vcan.sh            # 一次性建 vcan0/vcan1（需 sudo）
#   ./run_slaves.sh            # 啟動 14 顆假 PHU 從站（python-can）
#   make && ./pc_master        # 主站全棧 @500Hz

CC      ?= gcc
# -D_GNU_SOURCE：clock_nanosleep / SocketCAN / strncpy 等 POSIX+GNU 宣告
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE
# include：pc 優先,再 sim（stm32f7xx_hal.h 替身）,其餘為韌體本體
INC     = -I. -I../sim -I../canopen -I../app -I../control -I../safety -I../test -I../engine -I../ecat

# 與 firmware/Makefile（板端）同一套韌體源碼;僅底層/進入點不同：
#   co_bxcan.c → co_bxcan_socketcan.c;board/main.c → pc_master_main.c
SRC = \
  pc_master_main.c \
  rt_selfcheck.c \
  co_bxcan_socketcan.c \
  hal_linux.c \
  ../test/bringup.c \
  ../canopen/co_sdo.c \
  ../canopen/co_nmt.c \
  ../canopen/co_pdo.c \
  ../canopen/co_emcy.c \
  ../canopen/cia402.c \
  ../control/linalg.c \
  ../control/trajectory.c \
  ../control/joint_space.c \
  ../control/kinematics.c \
  ../control/ik.c \
  ../control/task_space.c \
  ../control/dual_arm_ctrl.c \
  ../safety/safety.c \
  ../app/dual_arm.c \
  ../app/robot_config.c \
  ../app/app_main.c \
  ../app/app_agents.c \
  ../app/app_io_agents.c \
  ../app/bus_canopen.c \
  ../app/bus_ecat.c \
  ../app/sdo_bg.c \
  ../ecat/ec_master_sim.c \
  ../ecat/ec_dc_pll.c \
  ../sim/phu_sim.c \
  ../engine/loop_engine.c \
  ../engine/spsc_ring.c \
  ../engine/harness.c \
  ../engine/eng_log.c \
  ../engine/eng_trace.c

TARGET = pc_master

all: $(TARGET)

# 預設 = sim 後端（免硬體 SIL）
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(INC) $(SRC) -o $(TARGET) -lm -lpthread

# ── 真硬體後端（gx701 等有 IgH/SOEM 的機器）──
# 用出廠 PDO + DC(assign_activate 0x300) 配方（見 ../ecat/ec_config.h）。
# 換掉 ec_master_sim.c → ec_master_{igh,soem}.c,鏈接對應庫。
# 路徑用變數,請依機器覆寫:
#   make pc_master_igh  IGH_DIR=$HOME/robot_test/ethercat
#   make pc_master_soem SOEM_DIR=$HOME/robot_test/SOEM
# 執行:sudo LD_LIBRARY_PATH=$IGH_DIR/lib/.libs ./pc_master_igh --bus ethercat --rate 100
# SOEM 需 EC_IFNAME=<nic>（或程式呼叫 ec_soem_set_ifname）。
SRC_NOSIM = $(filter-out ../ecat/ec_master_sim.c,$(SRC))
IGH_DIR  ?= $(HOME)/robot_test/ethercat
SOEM_DIR ?= $(HOME)/robot_test/SOEM

pc_master_igh: $(SRC_NOSIM) ../ecat/ec_master_igh.c
	$(CC) $(CFLAGS) $(INC) -I$(IGH_DIR)/include $^ -o $@ \
	  -L$(IGH_DIR)/lib/.libs -lethercat -lm -lpthread -lrt

pc_master_soem: $(SRC_NOSIM) ../ecat/ec_master_soem.c
	$(CC) $(CFLAGS) $(INC) -I$(SOEM_DIR)/include -I$(SOEM_DIR)/build/include \
	  -I$(SOEM_DIR)/osal -I$(SOEM_DIR)/osal/linux -I$(SOEM_DIR)/oshw/linux \
	  $^ -o $@ -L$(SOEM_DIR)/build -lsoem -lm -lpthread -lrt

clean:
	rm -f $(TARGET) pc_master_igh pc_master_soem

.PHONY: all clean
