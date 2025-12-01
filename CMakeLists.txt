cmake_minimum_required(VERSION 3.14)
project(concordia C CXX)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 14)

if(MSVC)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# --- Version Info ---
find_package(Git)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(GIT_VERSION "unknown")
    set(GIT_HASH "unknown")
endif()

if("${GIT_VERSION}" STREQUAL "")
    set(GIT_VERSION "dev")
endif()

add_compile_definitions(CND_VERSION="${GIT_VERSION}")
add_compile_definitions(CND_GIT_HASH="${GIT_HASH}")

# --- Dependencies ---

include(FetchContent)

# GoogleTest
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/heads/main.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Google Benchmark
FetchContent_Declare(
  benchmark
  URL https://github.com/google/benchmark/archive/refs/tags/v1.9.1.zip
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

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

# --- Subdirectories ---

# Core Libraries
add_subdirectory(src/vm)
add_subdirectory(src/compiler)

# Tools & CLI
add_subdirectory(src/tools)
add_subdirectory(src/cli)

# Tests & Benchmarks
add_subdirectory(tests)
add_subdirectory(benchmarks)

# Examples & Demos
add_subdirectory(demos)
add_subdirectory(examples)

# --- Installation ---

include(GNUInstallDirs)

# Install headers
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
