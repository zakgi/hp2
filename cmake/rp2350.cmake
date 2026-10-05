# rp2350 post-project setup. The root CMakeLists has called project() with the pico-sdk
# cross-toolchain plumbed in by rp2350_toolchain.cmake. Owns: pico_sdk_init(), the embedded flag
# pass, and the hp2_rp2350 firmware target: sources, include paths, link libraries, the PIO
# programs, USB-CDC stdio, the partition table and the extra outputs.

# pico-sdk has assembly sources (boot stage 2, vector table, startup).
enable_language(ASM)
# C11 for the SDK's own C sources; C++23 comes from the root.
set(CMAKE_C_STANDARD 11)
# No clang-scan-deps in the Arm toolchains; no modules either.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

pico_sdk_init()

include(embedded_flags)
hp2_apply_embedded_flags()

set(HP2_BOARD_DIR ${CMAKE_SOURCE_DIR}/src/boards/${HP2_BOARD})
set(HP2_RP2350_DIR ${CMAKE_SOURCE_DIR}/src/target/rp2350)
set(HP2_PYTHON
    "${CMAKE_SOURCE_DIR}/.venv/bin/python"
    CACHE FILEPATH "Python with the hp2lib package (uv sync --extra dev)")

add_executable(hp2_rp2350)

# --- Sources ------------------------------------------------------------------------------------
set(HP2_RP2350_OWN_SOURCES ${HP2_BOARD_DIR}/main.cpp ${HP2_BOARD_DIR}/board.cpp ${HP2_CORE_SOURCES})
target_sources(hp2_rp2350 PRIVATE ${HP2_RP2350_OWN_SOURCES})

# Warnings on our own translation units only: a target-wide option would also reach the SDK's
# INTERFACE-attached C sources.
set_source_files_properties(
  ${HP2_RP2350_OWN_SOURCES}
  TARGET_DIRECTORY hp2_rp2350
  PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic")

# Board headers shadow target-wide ones: the board directory comes first.
target_include_directories(hp2_rp2350 PRIVATE ${HP2_BOARD_DIR} ${CMAKE_SOURCE_DIR}/src ${HP2_RP2350_DIR})

target_link_libraries(
  hp2_rp2350
  PRIVATE pico_stdlib
          pico_multicore
          pico_rand
          pico_sha256
          hardware_dma
          hardware_pio
          hardware_pwm
          hardware_spi
          hardware_vreg)

# --- PIO programs ---------------------------------------------------------------------------------
# pioasm turns each .pio into a header the drivers include by name.
pico_generate_pio_header(hp2_rp2350 ${HP2_RP2350_DIR}/display/palette_lut.pio)

pico_enable_stdio_usb(hp2_rp2350 1)
pico_enable_stdio_uart(hp2_rp2350 0)

# No %f in pico's printf: double formatting is software on this core.
target_compile_definitions(hp2_rp2350 PRIVATE PICO_PRINTF_SUPPORT_FLOAT=0 PICO_PRINTF_SUPPORT_EXPONENTIAL=0)

# --- Memory ---------------------------------------------------------------------------------------
# PICO_COPY_TO_RAM, pico-sdk's own cache variable, is on in the presets: XIP execution is slow on
# the RP2350 and the whole image fits in RAM.
if(PICO_COPY_TO_RAM)
  pico_set_binary_type(hp2_rp2350 copy_to_ram)
endif()

target_link_options(hp2_rp2350 PRIVATE "LINKER:--print-memory-usage" "LINKER:--gc-sections" "-Wl,--demangle")

# Each core's stack in its own 4 KB scratch bank: SCRATCH_Y core 0, SCRATCH_X core 1.
target_compile_definitions(hp2_rp2350 PRIVATE PICO_STACK_SIZE=0x1000 PICO_CORE1_STACK_SIZE=0x1000)

# --- Partition table -------------------------------------------------------------------------------
# Embedded in the firmware's block loop at post-link, so one UF2 carries the image and the table.
# The same JSON drives scripts/pack_assets.py, so the asset UF2 and the firmware agree on every
# offset by construction.
set(HP2_PARTITIONS ${HP2_BOARD_DIR}/partitions.json)
pico_embed_pt_in_binary(hp2_rp2350 ${HP2_PARTITIONS})

# scripts/gen_partition_header.py turns the JSON into flash_partitions.hpp: one constexpr
# FlashRegion (XIP address, size) per partition, checked in next to it.
set(HP2_PARTITION_HEADER ${HP2_BOARD_DIR}/flash_partitions.hpp)
add_custom_command(
  OUTPUT ${HP2_PARTITION_HEADER}
  COMMAND ${HP2_PYTHON} ${CMAKE_SOURCE_DIR}/scripts/gen_partition_header.py --input ${HP2_PARTITIONS} --output
          ${HP2_PARTITION_HEADER}
  DEPENDS ${HP2_PARTITIONS} ${CMAKE_SOURCE_DIR}/scripts/gen_partition_header.py
          ${CMAKE_SOURCE_DIR}/scripts/hp2lib/partition_table.py
  COMMENT "Generating flash_partitions.hpp from ${HP2_PARTITIONS}"
  VERBATIM)
target_sources(hp2_rp2350 PRIVATE ${HP2_PARTITION_HEADER})

pico_add_extra_outputs(hp2_rp2350)

message(STATUS "")
message(STATUS "Highway Patrol II RP2350 build")
message(STATUS "  HP2_BOARD            : ${HP2_BOARD}")
message(STATUS "  PICO_BOARD           : ${PICO_BOARD}")
message(STATUS "  PICO_SDK_PATH        : ${PICO_SDK_PATH}")
message(STATUS "")
