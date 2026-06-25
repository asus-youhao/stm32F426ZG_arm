cmake_minimum_required(VERSION 3.16)

# ──────────────────────────────────────────────────────────────────────────
# STM32F746ZG 雙臂控制韌體
#
# 兩種建置：
#  1) HOST（預設）：用系統 gcc 建立模擬器 phu_sim_demo（可在 PC 驗證控制邏輯）
#       cmake -S . -B build && cmake --build build && ./build/phu_sim_demo
#  2) TARGET（交叉）：建立可燒錄的 firmware.elf/.bin（需 arm-none-eabi + CubeMX HAL）
#       cmake -S . -B build-tgt -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake
#       cmake --build build-tgt
#     ※ TARGET 需先放入 CubeMX 產生的 Core/ 與 Drivers/（見 target/README.md）
# ──────────────────────────────────────────────────────────────────────────
project(stm32f746_dualarm C ASM)
set(CMAKE_C_STANDARD 11)

# 共用：可移植的控制/通訊模組（不含硬體 bxCAN）
set(PORTABLE_SRC
    canopen/co_sdo.c canopen/co_nmt.c canopen/co_pdo.c canopen/cia402.c
    control/linalg.c control/trajectory.c control/joint_space.c
    control/kinematics.c control/ik.c control/task_space.c control/dual_arm_ctrl.c
    safety/safety.c
    host/host_if.c
    app/dual_arm.c app/robot_config.c app/app_main.c
)
set(PORTABLE_INC canopen app control safety host)

if(NOT CMAKE_CROSSCOMPILING)
    # ── HOST：模擬器（用 sim/ 的 HAL 替身與虛擬 bus）────────────────────────
    message(STATUS "Building HOST simulator (phu_sim_demo)")
    add_executable(phu_sim_demo
        ${PORTABLE_SRC}
        sim/hal_shim.c sim/co_bxcan_sim.c sim/phu_sim.c sim/sim_main.c
    )
    target_include_directories(phu_sim_demo PRIVATE sim ${PORTABLE_INC})
    target_link_libraries(phu_sim_demo m)
else()
    # ── TARGET：可燒錄韌體 ────────────────────────────────────────────────
    message(STATUS "Building TARGET firmware for STM32F746ZG")
    set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker/STM32F746ZGTx_FLASH.ld)

    # CubeMX 產生的程式（使用者放入,見 target/README.md）：
    #   Core/Src/*.c（main.c、stm32f7xx_it.c、system_*.c、msp、hal conf 等）
    #   Core/Startup/startup_stm32f746xx.s
    #   Drivers/STM32F7xx_HAL_Driver/Src/*.c、CMSIS
    file(GLOB_RECURSE CUBE_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/Core/*.s
        ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32F7xx_HAL_Driver/Src/*.c
    )

    add_executable(firmware.elf
        ${PORTABLE_SRC}
        canopen/co_bxcan.c           # 真實 bxCAN 硬體層（需 HAL）
        ${CUBE_SRC}
    )
    target_compile_definitions(firmware.elf PRIVATE STM32F746xx USE_HAL_DRIVER)
    target_include_directories(firmware.elf PRIVATE
        ${PORTABLE_INC}
        Core/Inc
        Drivers/STM32F7xx_HAL_Driver/Inc
        Drivers/CMSIS/Device/ST/STM32F7xx/Include
        Drivers/CMSIS/Include
    )
    target_link_options(firmware.elf PRIVATE
        -T${LINKER_SCRIPT}
        -Wl,-Map=${CMAKE_BINARY_DIR}/firmware.map,--cref
    )

    # 產生 .bin / .hex 與大小報告
    add_custom_command(TARGET firmware.elf POST_BUILD
        COMMAND ${CMAKE_SIZE} firmware.elf
        COMMAND ${CMAKE_OBJCOPY} -O binary firmware.elf firmware.bin
        COMMAND ${CMAKE_OBJCOPY} -O ihex   firmware.elf firmware.hex
        COMMENT "Generating firmware.bin / firmware.hex + size")
endif()
