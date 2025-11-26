cmake_minimum_required(VERSION 3.14)
project(concordia C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 14)

include_directories(include)

# --- Dependencies ---

include(FetchContent)

# GoogleTest
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/heads/main.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# cJSON
FetchContent_Declare(
  cjson
  URL https://github.com/DaveGamble/cJSON/archive/refs/heads/master.zip
)
FetchContent_GetProperties(cjson)
if(NOT cjson_POPULATED)
  FetchContent_Populate(cjson)
  # Add library manually to bypass legacy CMakeLists.txt issues
  add_library(cjson ${cjson_SOURCE_DIR}/cJSON.c)
  target_include_directories(cjson PUBLIC ${cjson_SOURCE_DIR})
endif()

# --- Targets ---

# Library
add_library(concordia src/vm/vm_exec.c src/vm/vm_io.c)

# Compiler Library
add_library(cnd_compiler src/compiler/cndc.c)

add_executable(hexview src/tools/hexview.c)

# Main CLI Tool (cnd)
add_executable(cnd src/cli/main.c src/cli/cli_helpers.c src/cli/cmd_compile.c src/cli/cmd_encode.c src/cli/cmd_decode.c)
target_link_libraries(cnd concordia cnd_compiler cjson)

# Test Executable
add_executable(test_runner tests/main_test.cpp)
target_link_libraries(test_runner concordia gtest_main)
