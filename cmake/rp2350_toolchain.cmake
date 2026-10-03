# rp2350 cross-toolchain bring-up. Included from the root CMakeLists BEFORE project(): CMake reads
# CMAKE_TOOLCHAIN_FILE exactly once, at the first project() call. pico-sdk's order is rigid:
#   1. set PICO_PLATFORM and PICO_BOARD in the cache
#   2. include(pico_sdk_init.cmake), which plumbs CMAKE_TOOLCHAIN_FILE
#   3. project(... LANGUAGES C CXX)
#   4. pico_sdk_init()
# Steps 1 and 2 happen here, 3 in the root CMakeLists, 4 in rp2350.cmake.

# HP2_BOARD picks src/boards/<name>/, which supplies pico_board.cmake, partitions.json, main.cpp,
# board.cpp, board_constants.hpp and board_definitions.hpp. Adding a board: create the directory,
# append the name here, add a configure preset.
set(HP2_SUPPORTED_BOARDS adafruit_feather_rp2350)
set_property(CACHE HP2_BOARD PROPERTY STRINGS ${HP2_SUPPORTED_BOARDS})

list(FIND HP2_SUPPORTED_BOARDS "${HP2_BOARD}" _board_index)
if(_board_index EQUAL -1)
  message(FATAL_ERROR "HP2_BOARD='${HP2_BOARD}' is not recognised.\n" "Supported boards: ${HP2_SUPPORTED_BOARDS}")
endif()

# PICO_BOARD selects the SDK board header; each board names its own.
include(${CMAKE_SOURCE_DIR}/src/boards/${HP2_BOARD}/pico_board.cmake)
set(PICO_BOARD
    "${HP2_PICO_BOARD}"
    CACHE STRING "" FORCE)
set(PICO_PLATFORM
    "rp2350"
    CACHE STRING "" FORCE)

# PICO_COMPILER unset: pico-sdk defaults to arm-none-eabi-gcc from PATH.
include(pico_sdk_import)
