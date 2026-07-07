# ------------------------------------------------------------------
# Nucleo-F746ZG CANopen bring-up firmware — bare Makefile + arm-none-eabi-gcc
#   build:  make            (產出 build/bringup_f746.elf/.hex/.bin)
#   flash:  make flash      (OpenOCD, Nucleo 內建 ST-Link)
#           make flash-cube (STM32CubeProgrammer CLI)
#   clean:  make clean
#
# HAL 來源用本機 STM32Cube_FW_F7 包；換機器可在命令列覆寫：
#   make CUBE_FW_F7=/path/to/STM32Cube_FW_F7_V1.17.4
# ------------------------------------------------------------------

TARGET     = bringup_f746
BUILD      = build

CUBE_FW_F7 ?= C:/Users/u/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4
HAL_SRC    = $(CUBE_FW_F7)/Drivers/STM32F7xx_HAL_Driver/Src
HAL_INC    = $(CUBE_FW_F7)/Drivers/STM32F7xx_HAL_Driver/Inc
CMSIS_DEV  = $(CUBE_FW_F7)/Drivers/CMSIS/Device/ST/STM32F7xx
CMSIS_CORE = $(CUBE_FW_F7)/Drivers/CMSIS/Include

# ---- toolchain ----
PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
AS      = $(PREFIX)gcc -x assembler-with-cpp
CP      = $(PREFIX)objcopy
SZ      = $(PREFIX)size

# ---- MCU / FPU ----
CPU = -mcpu=cortex-m7
FPU = -mfpu=fpv5-sp-d16
FLOAT = -mfloat-abi=hard
MCU = $(CPU) -mthumb $(FPU) $(FLOAT)

# ---- defines ----
C_DEFS = -DUSE_HAL_DRIVER -DSTM32F746xx

# ---- includes ----
C_INCLUDES = \
  -Iboard \
  -Icanopen \
  -Iapp \
  -Iengine \
  -Icontrol \
  -Isafety \
  -Ihost \
  -I. \
  -I$(HAL_INC) \
  -I$(CMSIS_DEV)/Include \
  -I$(CMSIS_CORE)

# ---- application + canopen sources（bring-up + L1–L4 全棧）----
C_SOURCES = \
  board/main.c \
  board/stm32f7xx_it.c \
  board/system_stm32f7xx.c \
  canopen/co_bxcan.c \
  canopen/co_nmt.c \
  canopen/co_sdo.c \
  canopen/co_pdo.c \
  canopen/co_emcy.c \
  canopen/cia402.c \
  app/app_main.c \
  app/dual_arm.c \
  app/bus_canopen.c \
  app/sdo_bg.c \
  engine/spsc_ring.c \
  engine/eng_log.c \
  app/robot_config.c \
  control/linalg.c \
  control/trajectory.c \
  control/joint_space.c \
  control/kinematics.c \
  control/ik.c \
  control/task_space.c \
  control/dual_arm_ctrl.c \
  safety/safety.c \
  host/host_if.c \
  test/bringup.c

# ---- HAL sources（bring-up + app 用到的模組）----
C_SOURCES += \
  $(HAL_SRC)/stm32f7xx_hal.c \
  $(HAL_SRC)/stm32f7xx_hal_cortex.c \
  $(HAL_SRC)/stm32f7xx_hal_rcc.c \
  $(HAL_SRC)/stm32f7xx_hal_rcc_ex.c \
  $(HAL_SRC)/stm32f7xx_hal_gpio.c \
  $(HAL_SRC)/stm32f7xx_hal_pwr.c \
  $(HAL_SRC)/stm32f7xx_hal_pwr_ex.c \
  $(HAL_SRC)/stm32f7xx_hal_can.c \
  $(HAL_SRC)/stm32f7xx_hal_uart.c \
  $(HAL_SRC)/stm32f7xx_hal_tim.c \
  $(HAL_SRC)/stm32f7xx_hal_tim_ex.c

ASM_SOURCES = board/startup_stm32f746xx.s
LDSCRIPT    = board/STM32F746ZGTx_FLASH.ld

# ---- flags ----
OPT = -Og -g3
CFLAGS  = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections
ASFLAGS = $(MCU) $(OPT) -Wall -fdata-sections -ffunction-sections
LDFLAGS = $(MCU) -specs=nano.specs -specs=nosys.specs -T$(LDSCRIPT) \
          -Wl,-Map=$(BUILD)/$(TARGET).map,--cref -Wl,--gc-sections -lc -lm

# ---- objects ----
OBJECTS  = $(addprefix $(BUILD)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

# ---- rules ----
all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD)/%.o: %.s | $(BUILD)
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD)/%.hex: $(BUILD)/%.elf
	$(CP) -O ihex $< $@

$(BUILD)/%.bin: $(BUILD)/%.elf
	$(CP) -O binary -S $< $@

$(BUILD):
	mkdir $@

# ---- flash（預設用 STM32CubeProgrammer CLI，自動偵測 Nucleo 內建 ST-Link，免 cfg）----
flash: $(BUILD)/$(TARGET).hex
	STM32_Programmer_CLI -c port=SWD mode=UR -w $(BUILD)/$(TARGET).hex -rst

# 連線測試（不燒錄，只確認 ST-Link/目標）
probe:
	STM32_Programmer_CLI -c port=SWD mode=UR

clean:
	rm -rf $(BUILD) $(ECAT_BUILD)

# =================================================================
# WP-SE3：EtherCAT 探測韌體（獨立 target,不動 bring-up 主韌體）
#   make ecat-probe   → build-ecat/ecat_probe_f746.elf/.hex
#   make flash-ecat   → 燒錄
# =================================================================
SOEM_DIR     = ../third_party/SOEM
ECAT_TARGET  = ecat_probe_f746
ECAT_BUILD   = build-ecat
ECAT_INC     = -Iecat/f7 -Iecat -I$(SOEM_DIR)/include -I$(SOEM_DIR)/osal $(C_INCLUDES)
ECAT_SOURCES = \
  board/ecat_probe_main.c \
  board/system_stm32f7xx.c \
  ecat/f7/osal.c \
  ecat/f7/oshw.c \
  ecat/f7/nicdrv.c \
  ecat/f7/tick_f7.c \
  ecat/ec_dc_pll.c \
  $(SOEM_DIR)/src/ec_base.c \
  $(SOEM_DIR)/src/ec_main.c \
  $(SOEM_DIR)/src/ec_coe.c \
  $(SOEM_DIR)/src/ec_config.c \
  $(SOEM_DIR)/src/ec_dc.c \
  $(SOEM_DIR)/src/ec_print.c \
  $(SOEM_DIR)/src/ec_eoe.c \
  $(SOEM_DIR)/src/ec_foe.c \
  $(SOEM_DIR)/src/ec_soe.c \
  $(HAL_SRC)/stm32f7xx_hal.c \
  $(HAL_SRC)/stm32f7xx_hal_cortex.c \
  $(HAL_SRC)/stm32f7xx_hal_rcc.c \
  $(HAL_SRC)/stm32f7xx_hal_rcc_ex.c \
  $(HAL_SRC)/stm32f7xx_hal_gpio.c \
  $(HAL_SRC)/stm32f7xx_hal_pwr.c \
  $(HAL_SRC)/stm32f7xx_hal_pwr_ex.c \
  $(HAL_SRC)/stm32f7xx_hal_eth.c \
  $(HAL_SRC)/stm32f7xx_hal_uart.c

ECAT_OBJECTS  = $(addprefix $(ECAT_BUILD)/,$(notdir $(ECAT_SOURCES:.c=.o)))
ECAT_OBJECTS += $(ECAT_BUILD)/startup_stm32f746xx.o
vpath %.c $(sort $(dir $(ECAT_SOURCES)))

ecat-probe: $(ECAT_BUILD)/$(ECAT_TARGET).elf $(ECAT_BUILD)/$(ECAT_TARGET).hex

$(ECAT_BUILD)/%.o: %.c | $(ECAT_BUILD)
	$(CC) -c $(MCU) $(C_DEFS) $(ECAT_INC) $(OPT) -Wall -fdata-sections -ffunction-sections $< -o $@

$(ECAT_BUILD)/startup_stm32f746xx.o: board/startup_stm32f746xx.s | $(ECAT_BUILD)
	$(AS) -c $(ASFLAGS) $< -o $@

$(ECAT_BUILD)/$(ECAT_TARGET).elf: $(ECAT_OBJECTS)
	$(CC) $(ECAT_OBJECTS) $(MCU) -specs=nano.specs -specs=nosys.specs -T$(LDSCRIPT) \
	  -Wl,-Map=$(ECAT_BUILD)/$(ECAT_TARGET).map,--cref -Wl,--gc-sections -lc -lm -o $@
	$(SZ) $@

$(ECAT_BUILD)/$(ECAT_TARGET).hex: $(ECAT_BUILD)/$(ECAT_TARGET).elf
	$(CP) -O ihex $< $@

$(ECAT_BUILD):
	mkdir -p $@

flash-ecat: $(ECAT_BUILD)/$(ECAT_TARGET).hex
	STM32_Programmer_CLI -c port=SWD mode=UR -w $(ECAT_BUILD)/$(ECAT_TARGET).hex -rst

.PHONY: all clean flash probe ecat-probe flash-ecat
