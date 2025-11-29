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

# Google Benchmark
FetchContent_Declare(
  benchmark
  URL https://github.com/google/benchmark/archive/refs/tags/v1.9.1.zip
)
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

# --- Targets ---

# Library
add_library(concordia src/vm/vm_exec.c src/vm/vm_io.c)

# Compiler Library
add_library(cnd_compiler 
    src/compiler/cndc.c
    src/compiler/cnd_utils.c
    src/compiler/cnd_lexer.c
    src/compiler/cnd_parser.c
    src/compiler/cnd_fmt.c
)

add_executable(hexview src/tools/hexview.c)

# Main CLI Tool (cnd)
add_executable(cnd src/cli/main.c src/cli/cli_helpers.c src/cli/json_binding.c src/cli/cmd_compile.c src/cli/cmd_encode.c src/cli/cmd_decode.c src/cli/cmd_fmt.c src/cli/cmd_inspect.c src/cli/cmd_lsp.c)
target_link_libraries(cnd concordia cnd_compiler cjson)

# Test Executable
add_executable(test_runner 
    tests/test_common.cpp
    tests/vm_tests.cpp
    tests/compiler_test.cpp 
    tests/lexer_test.cpp
    tests/compiler_integration_test.cpp
    tests/coverage_test.cpp
    tests/language_features_test.cpp
)
target_link_libraries(test_runner concordia cnd_compiler gtest_main)

# --- Benchmarks ---
add_executable(vm_benchmark 
    benchmarks/main.cpp
    benchmarks/bench_common.cpp
    benchmarks/bench_core.cpp
    benchmarks/bench_features.cpp
    benchmarks/bench_ifelse.cpp
    benchmarks/bench_switch.cpp
)
target_link_libraries(vm_benchmark concordia cnd_compiler benchmark::benchmark)

# --- Installation ---

include(GNUInstallDirs)

install(TARGETS cnd hexview
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS concordia cnd_compiler
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# --- Demos ---
add_executable(demo_c demos/demo_c.c)
target_link_libraries(demo_c concordia)

add_executable(demo_cpp demos/demo_cpp.cpp)
target_link_libraries(demo_cpp concordia)

add_executable(kitchen_sink examples/kitchen_sink/kitchen_sink.c)
target_link_libraries(kitchen_sink concordia cnd_compiler)
