cmake_minimum_required(VERSION 3.13)

# The rev A board, boards/gbc_controller.h (standalone: module-header default pins hit the cart bus).
# Must precede pico_sdk_init.
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD gbc_controller CACHE STRING "Board type")

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(gbc_controller C CXX ASM)
pico_sdk_init()

set(EVMU_DIR ${CMAKE_CURRENT_LIST_DIR}/evmu)

add_executable(gbc_controller
    ${CMAKE_CURRENT_LIST_DIR}/main.c
    ${CMAKE_CURRENT_LIST_DIR}/cartserve.c
    ${CMAKE_CURRENT_LIST_DIR}/psram.c
    ${CMAKE_CURRENT_LIST_DIR}/config_map.c
    ${CMAKE_CURRENT_LIST_DIR}/ctrl.c
    ${CMAKE_CURRENT_LIST_DIR}/cable_id.c
    ${CMAKE_CURRENT_LIST_DIR}/pad_driver.c
    ${CMAKE_CURRENT_LIST_DIR}/pad_res.c
    ${CMAKE_CURRENT_LIST_DIR}/pad_catalog.c
    ${CMAKE_CURRENT_LIST_DIR}/pad_mapinfo.c
    ${CMAKE_CURRENT_LIST_DIR}/pad_arena.c
    ${CMAKE_CURRENT_LIST_DIR}/gb_stage.c
    ${CMAKE_CURRENT_LIST_DIR}/romstore.c
    ${CMAKE_CURRENT_LIST_DIR}/slot_page.c
    ${CMAKE_CURRENT_LIST_DIR}/vmu_stage.c
    ${CMAKE_CURRENT_LIST_DIR}/vmu_store_fmt.c
    ${CMAKE_CURRENT_LIST_DIR}/vmu_store.c
    ${CMAKE_CURRENT_LIST_DIR}/vmu_lcd.c
    ${CMAKE_CURRENT_LIST_DIR}/vmu_fb.c
    ${CMAKE_CURRENT_LIST_DIR}/vmu_run.c
    ${CMAKE_CURRENT_LIST_DIR}/padart.c
    # The LC8670 core; evmu/test covers it on the host.
    ${EVMU_DIR}/vmu_cpu.c
    ${EVMU_DIR}/vmu_isa.c
    ${EVMU_DIR}/vmu_mem.c
    ${EVMU_DIR}/vmu_pic.c
    ${EVMU_DIR}/vmu_timers.c
    ${EVMU_DIR}/vmu_lcd.c
    ${EVMU_DIR}/vmu_bios.c
    ${CMAKE_CURRENT_LIST_DIR}/driver_core.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/null.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/jbdev.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/n64_proto.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/gbcart_emu.c
    ${CMAKE_CURRENT_LIST_DIR}/cart_mbc.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/tpak_proto.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/n64.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/mapledev.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/maple_proto.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/dc.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/snesdev.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/snes_proto.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/snes.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/snesmouse.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/snesmouse_proto.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/snes_mouse.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/gendev.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/gen_proto.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/genesis.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/irdev.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/vis_proto.c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/vis.c)
pico_set_binary_type(gbc_controller copy_to_ram)   # so config_save() can erase flash while serving

# run.sh passes ON: stdio on J2's CDC as well as UART1, flashed by picotool. OFF drops TinyUSB and
# run.sh flashes over SWD.
option(USB_CONSOLE "stdio on J2's USB CDC as well as UART1" OFF)

pico_enable_stdio_uart(gbc_controller 1)
if(USB_CONSOLE)
    pico_enable_stdio_usb(gbc_controller 1)
    # The vendor reset interface lets picotool force BOOTSEL; the short stdout timeout keeps a
    # stalled host from blocking core0.
    target_compile_definitions(gbc_controller PRIVATE
        USB_CONSOLE=1
        PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE=1
        PICO_STDIO_USB_STDOUT_TIMEOUT_US=100000
        PICO_STDIO_USB_TASK_INTERVAL_US=200)
else()
    pico_enable_stdio_usb(gbc_controller 0)
    target_compile_definitions(gbc_controller PRIVATE USB_CONSOLE=0)
endif()
pico_generate_pio_header(gbc_controller ${CMAKE_CURRENT_LIST_DIR}/serve.pio)
pico_generate_pio_header(gbc_controller ${CMAKE_CURRENT_LIST_DIR}/drivers/jbdev.pio)
pico_generate_pio_header(gbc_controller ${CMAKE_CURRENT_LIST_DIR}/drivers/mapledev.pio)
pico_generate_pio_header(gbc_controller ${CMAKE_CURRENT_LIST_DIR}/drivers/snesdev.pio)
pico_generate_pio_header(gbc_controller ${CMAKE_CURRENT_LIST_DIR}/drivers/snesmouse.pio)
pico_generate_pio_header(gbc_controller ${CMAKE_CURRENT_LIST_DIR}/drivers/gendev.pio)

if(DEFINED SYSCLK_KHZ)
    target_compile_definitions(gbc_controller PRIVATE SYSCLK_KHZ=${SYSCLK_KHZ})
endif()
if(DEFINED VREG_MV)
    target_compile_definitions(gbc_controller PRIVATE VREG_MV=${VREG_MV})
endif()
# Bench builds with no console attached: force CFG_PROTO_SEL, which only the GB mailbox can write.
if(DEFINED BENCH_PROTO)
    target_compile_definitions(gbc_controller PRIVATE BENCH_PROTO=${BENCH_PROTO})
endif()

# generated/ holds rom.h, emitted by run.sh from rom/ (a gitignored build artifact).
target_include_directories(gbc_controller PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/generated
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/drivers
    ${EVMU_DIR})
target_link_libraries(gbc_controller
    pico_stdlib hardware_pio hardware_dma hardware_vreg hardware_clocks
    hardware_flash  # config_save(): the last flash sector
    hardware_sync   # psram.c + config_save(): save_and_disable_interrupts()
    hardware_adc    # cable_id.c: the CTRL_ID ladder on GP41 / ADC1
    hardware_pwm    # drivers/irdev.c: the IR carrier on GP46
    pico_multicore) # driver_core.c: core1 runs the pad driver
pico_add_extra_outputs(gbc_controller)
