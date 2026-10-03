# Fetch pico-sdk into a shared FetchContent cache. No environment variables are
# consulted; the SDK lands under ${FETCHCONTENT_BASE_DIR}/pico_sdk-src.
# CMakePresets.json points FETCHCONTENT_BASE_DIR at one repo-level cache so every
# board variant shares one populated source tree.
#
# Not FetchContent_MakeAvailable: pico-sdk's top-level CMakeLists.txt is meant to
# be reached via include(pico_sdk_init.cmake) from inside the consuming project,
# after that project has called project() with the right toolchain. The
# explicit-details form of FetchContent_Populate populates without an
# add_subdirectory.
#
# Submodules: only lib/tinyusb (USB-CDC stdio) is initialised.

include(FetchContent)
find_package(Git REQUIRED)

set(HP2_PICO_SDK_TAG
    "2.3.1"
    CACHE STRING "pico-sdk git tag to fetch (pinned for reproducible builds)")

if(NOT DEFINED FETCHCONTENT_BASE_DIR OR FETCHCONTENT_BASE_DIR STREQUAL "")
  set(FETCHCONTENT_BASE_DIR ${CMAKE_BINARY_DIR}/_deps)
endif()

set(_pico_sdk_src ${FETCHCONTENT_BASE_DIR}/pico_sdk-src)

# Two independent idempotent stages so an interrupted configure does not poison
# the cache: the source tree, then the tinyusb submodule.
if(NOT EXISTS "${_pico_sdk_src}/pico_sdk_init.cmake")
  message(STATUS "Fetching pico-sdk ${HP2_PICO_SDK_TAG} into ${FETCHCONTENT_BASE_DIR}")
  FetchContent_Populate(
    pico_sdk
    QUIET
    SOURCE_DIR ${_pico_sdk_src}
    BINARY_DIR ${FETCHCONTENT_BASE_DIR}/pico_sdk-build
    SUBBUILD_DIR ${CMAKE_BINARY_DIR}/_deps/pico_sdk-subbuild
    GIT_REPOSITORY https://github.com/raspberrypi/pico-sdk.git
    GIT_TAG ${HP2_PICO_SDK_TAG}
    GIT_SHALLOW TRUE
    GIT_SUBMODULES ""
    GIT_PROGRESS TRUE)
endif()

# No --depth 1: tinyusb's default branch moves past the commit pico-sdk pins.
if(NOT EXISTS "${_pico_sdk_src}/lib/tinyusb/src/tusb.h")
  message(STATUS "Initialising pico-sdk submodule: lib/tinyusb")
  execute_process(
    COMMAND ${GIT_EXECUTABLE} submodule update --init lib/tinyusb
    WORKING_DIRECTORY ${_pico_sdk_src}
    RESULT_VARIABLE _tinyusb_status)
  if(NOT _tinyusb_status EQUAL 0)
    message(FATAL_ERROR "Failed to initialise pico-sdk lib/tinyusb submodule "
                        "(git exit ${_tinyusb_status}) in ${_pico_sdk_src}.")
  endif()
endif()

set(PICO_SDK_PATH
    ${_pico_sdk_src}
    CACHE PATH "" FORCE)
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
