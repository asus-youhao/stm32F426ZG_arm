# 假硬體模擬 — 主機(PC)編譯
# 用法： cd firmware/sim && make && ./phu_sim_demo

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -ffast-math
# include 路徑：sim 優先（提供 stm32f7xx_hal.h 替身）
INC     = -I. -I../canopen -I../app -I../control -I../safety

# 注意：用 co_bxcan_sim.c 取代 co_bxcan.c（不編 HAL 版）
SRC = \
  sim_main.c \
  hal_shim.c \
  co_bxcan_sim.c \
  phu_sim.c \
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

TARGET = phu_sim_demo

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(INC) $(SRC) -o $(TARGET) -lm

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean
