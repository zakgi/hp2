cmake_minimum_required(VERSION 3.28)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

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

option(HP2_BUILD_TESTS "Build the GoogleTest suite" ON)
set(HP2_ASSET_DIR
    "${CMAKE_CURRENT_LIST_DIR}/assets/hp2/Highway Patrol II"
    CACHE PATH "Extracted game disk (hp.prg and DISK2_2/) used by asset-dependent tests")

set(COMMON_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/src)

include(FetchContent)

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
                  src/host/format/dif.cpp src/host/format/palette_list.cpp src/host/asset_manager.cpp)
target_include_directories(hp2_host PUBLIC ${COMMON_SOURCE_DIR})
target_link_libraries(hp2_host PUBLIC hp2_core spdlog::spdlog)

add_executable(hp2 src/main.cpp src/host/renderer.cpp src/host/sfml_input.cpp)
target_link_libraries(hp2 PRIVATE hp2_host SFML::Graphics args spdlog::spdlog)

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
    test/core/component_test.cpp
    test/core/key_events_test.cpp
    test/core/palette_test.cpp
    test/core/screen_test.cpp
    test/core/title_test.cpp
    test/format/amiga_hunk_test.cpp
    test/format/bob_bank_test.cpp
    test/format/cpv_test.cpp
    test/format/dif_test.cpp
    test/format/palette_list_test.cpp
    test/format/planar_test.cpp
    test/host/asset_manager_test.cpp)
  target_include_directories(hp2_tests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/test)
  target_compile_definitions(hp2_tests PRIVATE HP2_ASSET_DIR="${HP2_ASSET_DIR}")
  target_link_libraries(hp2_tests PRIVATE hp2_host GTest::gtest_main)
  if(NOT MSVC)
    target_compile_options(hp2_tests PRIVATE -Wall -Wextra -Wpedantic -fno-rtti)
  endif()
  gtest_discover_tests(hp2_tests)
endif()
