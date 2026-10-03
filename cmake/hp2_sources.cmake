# The platform-independent core: the list both the host library and the rp2350 firmware compile.
# The target builds the whole core or nothing, so the compiler reports what is not yet portable.
set(HP2_CORE_SOURCES ${CMAKE_SOURCE_DIR}/src/core/screen.cpp ${CMAKE_SOURCE_DIR}/src/core/title.cpp)
