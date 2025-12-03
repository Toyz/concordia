cmake_minimum_required(VERSION 3.14)

set(BENCHMARK_SOURCES
    main.cpp
    bench_common.cpp
    bench_core.cpp
    bench_features.cpp
    bench_math.cpp
    bench_ifelse.cpp
    bench_switch.cpp
)

add_executable(vm_benchmark ${BENCHMARK_SOURCES})

target_link_libraries(vm_benchmark 
    PRIVATE 
    concordia 
    cnd_compiler 
    benchmark::benchmark
)

target_include_directories(vm_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

if(MSVC)
  target_compile_definitions(vm_benchmark PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
