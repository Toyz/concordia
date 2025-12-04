cmake_minimum_required(VERSION 3.14)

add_executable(hexview hexview.c)
if(MSVC)
  target_compile_definitions(hexview PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_executable(gen_bindings gen_bindings.c)
if(MSVC)
  target_compile_definitions(gen_bindings PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_executable(vm_fuzzer vm_fuzzer.c)
target_link_libraries(vm_fuzzer PRIVATE concordia)
target_include_directories(vm_fuzzer PRIVATE ../../include)
if(MSVC)
  target_compile_definitions(vm_fuzzer PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
