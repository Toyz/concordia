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
add_executable(cnd src/cli/main.c src/cli/cli_helpers.c src/cli/cmd_compile.c src/cli/cmd_encode.c src/cli/cmd_decode.c src/cli/cmd_fmt.c src/cli/cmd_inspect.c)
target_link_libraries(cnd concordia cnd_compiler cjson)

# Test Executable
add_executable(test_runner 
    tests/test_common.cpp
    tests/vm_core_test.cpp
    tests/vm_bitfield_test.cpp
    tests/vm_safety_test.cpp
    tests/vm_features_test.cpp
    tests/compiler_test.cpp 
    tests/lexer_test.cpp
    tests/import_test.cpp
    tests/validation_test.cpp
    tests/extended_test.cpp
    tests/extended_coverage_test.cpp
    tests/spec_coverage_test.cpp
    tests/match_rtt_test.cpp
    tests/enum_test.cpp
    tests/string_array_test.cpp
    tests/vm_concurrency_test.cpp
    tests/bool_test.cpp
    tests/switch_test.cpp
    tests/enum_sugar_test.cpp
    tests/vm_alu_test.cpp
    tests/compiler_if_test.cpp
    tests/rtt_if_test.cpp
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
