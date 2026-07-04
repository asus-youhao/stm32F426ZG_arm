# 單元測試（主機編譯）：cd firmware/tests && make && ./unit_tests
CC     ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
INC    = -I. -I../sim -I../canopen -I../app -I../control -I../safety -I../host -I../engine

SRC = \
  test_main.c test_trajectory.c test_kinematics.c test_ik.c \
  test_cia402.c test_hostif.c test_canopen.c test_safety.c \
  test_engine.c \
  ../engine/loop_engine.c ../engine/spsc_ring.c \
  ../sim/hal_shim.c ../sim/co_bxcan_sim.c ../sim/phu_sim.c \
  ../canopen/co_sdo.c ../canopen/co_nmt.c ../canopen/co_pdo.c ../canopen/cia402.c \
  ../control/linalg.c ../control/trajectory.c ../control/joint_space.c \
  ../control/kinematics.c ../control/ik.c ../control/task_space.c ../control/dual_arm_ctrl.c \
  ../safety/safety.c ../host/host_if.c \
  ../app/dual_arm.c ../app/robot_config.c

TARGET = unit_tests

all: $(TARGET)
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(INC) $(SRC) -o $(TARGET) -lm
run: $(TARGET)
	./$(TARGET)
clean:
	rm -f $(TARGET)
.PHONY: all run clean
