cmake_minimum_required(VERSION 3.28)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# rp2350 cross-builds are selected by HP2_BOARD in the cache (each rp2350 preset sets it); otherwise
# this is the SFML host build. The toolchain bring-up has to run before project(): CMake reads
# CMAKE_TOOLCHAIN_FILE at the first project().
if(DEFINED CACHE{HP2_BOARD})
  set(HP2_RP2350 TRUE)
  include(rp2350_toolchain)
endif()

project(
  hp2
  VERSION 0.0.1
  LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(hp2_sources)

if(HP2_RP2350)
  include(rp2350)
  return()
endif()

option(HP2_BUILD_TESTS "Build the GoogleTest suite" ON)
set(HP2_DISK_IMAGE
    "${CMAKE_CURRENT_LIST_DIR}/assets/hp2.adf"
    CACHE FILEPATH "Game disk image (ADF) used by asset-dependent tests")

set(COMMON_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/src)

include(FetchContent)

set(ADFLIB_ENABLE_TESTS
    OFF
    CACHE BOOL "Enable tests" FORCE)
set(ADFLIB_ENABLE_NATIVE_DEV
    OFF
    CACHE BOOL "Enable native devices" FORCE)
FetchContent_Declare(
  adflib
  GIT_REPOSITORY https://github.com/zakgi/ADFlib
  GIT_TAG 284eb9f3178120cafe4b7a7e7fb69d09791d2f82
  EXCLUDE_FROM_ALL SYSTEM)
FetchContent_MakeAvailable(adflib)

FetchContent_Declare(
  SFML
  GIT_REPOSITORY https://github.com/SFML/SFML.git
  GIT_TAG 3.1.0
  GIT_SHALLOW ON
  EXCLUDE_FROM_ALL SYSTEM)
FetchContent_MakeAvailable(SFML)

set(ARGS_BUILD_EXAMPLE
    OFF
    CACHE BOOL "Build example" FORCE)
set(ARGS_BUILD_UNITTESTS
    OFF
    CACHE BOOL "Build unittests" FORCE)
FetchContent_Declare(
  args
  GIT_REPOSITORY https://github.com/Taywee/args
  GIT_TAG 6.4.7
  GIT_SHALLOW ON
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(args)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog
  GIT_TAG v1.15.3
  GIT_SHALLOW ON
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(spdlog)

# Platform-independent game core: no exceptions, no RTTI. The same source list will compile into
# the rp2350 firmware (cmake/hp2_sources.cmake).
add_library(hp2_core STATIC ${HP2_CORE_SOURCES})
target_include_directories(hp2_core PUBLIC ${COMMON_SOURCE_DIR})

# Host-side loading of the original files (std allowed).
add_library(
  hp2_host STATIC src/host/format/amiga_hunk.cpp src/host/format/bob_bank.cpp src/host/format/cpv.cpp
                  src/host/format/dif.cpp src/host/format/font.cpp src/host/format/iff_8svx.cpp
                  src/host/format/music_file.cpp src/host/format/object_placement.cpp src/host/format/palette_list.cpp
                  src/host/format/road_map.cpp src/host/format/road_shapes.cpp
                  src/host/adf.cpp src/host/asset_manager.cpp)
target_include_directories(hp2_host PUBLIC ${COMMON_SOURCE_DIR})
target_link_libraries(hp2_host PUBLIC hp2_core spdlog::spdlog adf)

# scripts/pack_assets.py packs the decoded assets for the rp2350 flash partition
# (docs/building.md, "Asset image"): assets.bin, assets.uf2 at the partition's XIP address, and
# the checked-in layout header the firmware binds by. The host test checks the image against the
# asset manager. Needs the repository's .venv and the extracted game under assets/hp2/.
set(HP2_PYTHON
    "${CMAKE_CURRENT_LIST_DIR}/.venv/bin/python"
    CACHE FILEPATH "Python with the hp2lib package (uv sync --extra dev)")
set(HP2_ASSET_PARTITIONS "${CMAKE_CURRENT_LIST_DIR}/src/boards/adafruit_feather_rp2350/partitions.json")
set(HP2_ASSET_IMAGE_DIR ${CMAKE_BINARY_DIR}/assets)
set(HP2_ASSET_LAYOUT_HEADER ${CMAKE_CURRENT_LIST_DIR}/src/target/flash/asset_layout.hpp)
file(GLOB HP2_PACKER_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/scripts/hp2lib/*.py)
list(APPEND HP2_PACKER_SOURCES ${CMAKE_CURRENT_LIST_DIR}/scripts/pack_assets.py)
add_custom_command(
  OUTPUT ${HP2_ASSET_IMAGE_DIR}/assets.bin ${HP2_ASSET_IMAGE_DIR}/assets.uf2
  COMMAND ${HP2_PYTHON} ${CMAKE_CURRENT_LIST_DIR}/scripts/pack_assets.py --partitions ${HP2_ASSET_PARTITIONS}
          --out-dir ${HP2_ASSET_IMAGE_DIR} --layout-header ${HP2_ASSET_LAYOUT_HEADER}
  DEPENDS ${HP2_PACKER_SOURCES} ${HP2_ASSET_PARTITIONS}
  COMMENT "Packing the asset image for the rp2350 flash partition"
  VERBATIM)
add_custom_target(hp2_assets DEPENDS ${HP2_ASSET_IMAGE_DIR}/assets.bin)

add_executable(hp2 src/main.cpp src/host/audio_output.cpp src/host/renderer.cpp src/host/sfml_input.cpp)
target_link_libraries(hp2 PRIVATE hp2_host SFML::Audio SFML::Graphics args spdlog::spdlog)

# No RTTI anywhere (docs/coding-style.md). Exceptions stay enabled only where third-party libraries
# need them: the executable (args, SFML) and the tests (GoogleTest).
foreach(target hp2 hp2_core hp2_host)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /GR-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -fno-rtti)
  endif()
endforeach()

if(MSVC)
  target_compile_options(hp2_core PRIVATE /EHs-c-)
  target_compile_definitions(hp2_core PRIVATE _HAS_EXCEPTIONS=0)
else()
  target_compile_options(hp2_core PRIVATE -fno-exceptions)
endif()

if(HP2_BUILD_TESTS)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest
    GIT_TAG v1.17.0
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL)
  set(INSTALL_GTEST
      OFF
      CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  include(GoogleTest)

  add_executable(
    hp2_tests
    test/core/action_stack_test.cpp
    test/core/audio_engine_test.cpp
    test/core/component_test.cpp
    test/core/highway_test.cpp
    test/core/key_events_test.cpp
    test/core/map_view_test.cpp
    test/core/mission_test.cpp
    test/core/mission_end_test.cpp
    test/core/music_player_test.cpp
    test/core/office_test.cpp
    test/core/palette_test.cpp
    test/core/road_test.cpp
    test/core/screen_test.cpp
    test/core/station_test.cpp
    test/core/title_test.cpp
    test/core/vehicle_test.cpp
    test/core/voice_test.cpp
    test/format/amiga_hunk_test.cpp
    test/format/bob_bank_test.cpp
    test/format/color_test.cpp
    test/format/cpv_test.cpp
    test/format/dif_test.cpp
    test/format/font_test.cpp
    test/format/iff_8svx_test.cpp
    test/format/music_file_test.cpp
    test/format/object_placement_test.cpp
    test/format/palette_list_test.cpp
    test/format/planar_test.cpp
    test/format/road_map_test.cpp
    test/format/road_shapes_test.cpp
    test/host/adf_test.cpp
    test/host/asset_image_test.cpp
    test/host/asset_manager_test.cpp)
  target_include_directories(hp2_tests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/test)
  target_compile_definitions(hp2_tests PRIVATE HP2_DISK_IMAGE="${HP2_DISK_IMAGE}"
                                             HP2_ASSET_IMAGE="${HP2_ASSET_IMAGE_DIR}/assets.bin")
  target_link_libraries(hp2_tests PRIVATE hp2_host GTest::gtest_main)
  if(NOT MSVC)
    target_compile_options(hp2_tests PRIVATE -Wall -Wextra -Wpedantic -fno-rtti)
  endif()
  gtest_discover_tests(hp2_tests)
endif()
