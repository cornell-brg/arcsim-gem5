# Pico SDK firmware for the RP2350 board's tests (m_profile_rp2350_sdk_*).
#
# Needs the Pico SDK 2.2.0 (PICO_SDK_PATH) and arm-none-eabi-gcc on PATH:
#
#   cmake -S . -B <build dir> -DCMAKE_BUILD_TYPE=Release
#   cmake --build <build dir>
#
# The build puts each ELF, without debug information, next to its source,
# where the tests find it.

cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2)
set(PICO_PLATFORM rp2350-arm-s)
set(PICO_NO_PICOTOOL 1)
if(NOT PICO_SDK_PATH)
  set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(rp2350_sdk C CXX ASM)
pico_sdk_init()

add_executable(sdk_locks sdk_locks.c)
target_link_libraries(sdk_locks
  pico_runtime pico_sync pico_malloc pico_stdio pico_stdio_semihosting
  hardware_sync)
target_compile_definitions(sdk_locks PRIVATE
  # The RP2350 board has no boot ROM, clocks, resets or timers: skip the
  # start-up steps that need them.
  PICO_RUNTIME_SKIP_INIT_CLOCKS=1
  PICO_RUNTIME_SKIP_INIT_EARLY_RESETS=1
  PICO_RUNTIME_SKIP_INIT_POST_CLOCK_RESETS=1
  PICO_RUNTIME_SKIP_INIT_BOOTROM_RESET=1
  PICO_RUNTIME_SKIP_INIT_PER_CORE_BOOTROM_RESET=1
  PICO_RUNTIME_SKIP_INIT_BOOTROM_LOCKING_ENABLE=1
  PICO_RUNTIME_SKIP_INIT_USB_POWER_DOWN=1
  PICO_TIME_DEFAULT_ALARM_POOL_DISABLED=1)
# Float and double left to the compiler: the board's DCP is not modelled.
pico_set_float_implementation(sdk_locks compiler)
pico_set_double_implementation(sdk_locks compiler)
add_custom_command(TARGET sdk_locks POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} --strip-debug $<TARGET_FILE:sdk_locks>
          ${CMAKE_CURRENT_SOURCE_DIR}/sdk_locks.elf)
