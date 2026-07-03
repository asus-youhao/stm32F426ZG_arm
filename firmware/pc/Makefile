# PC 端 CANopen 主站 — Linux SocketCAN（vcan0=左臂, vcan1=右臂）
# 用法：
#   ./setup_vcan.sh            # 一次性建 vcan0/vcan1（需 sudo）
#   ./run_slaves.sh            # 啟動 14 顆假 PHU 從站（python-can）
#   make && ./pc_master        # 主站全棧 @500Hz

CC      ?= gcc
# -D_GNU_SOURCE：clock_nanosleep / SocketCAN / strncpy 等 POSIX+GNU 宣告
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE
# include：pc 優先,再 sim（stm32f7xx_hal.h 替身）,其餘為韌體本體
INC     = -I. -I../sim -I../canopen -I../app -I../control -I../safety -I../test

# 與 firmware/Makefile（板端）同一套韌體源碼;僅底層/進入點不同：
#   co_bxcan.c → co_bxcan_socketcan.c;board/main.c → pc_master_main.c
SRC = \
  pc_master_main.c \
  co_bxcan_socketcan.c \
  hal_linux.c \
  ../test/bringup.c \
  ../canopen/co_sdo.c \
  ../canopen/co_nmt.c \
  ../canopen/co_pdo.c \
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
  ../app/app_main.c

TARGET = pc_master

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(INC) $(SRC) -o $(TARGET) -lm

clean:
	rm -f $(TARGET)

.PHONY: all clean
