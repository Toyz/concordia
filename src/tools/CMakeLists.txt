cmake_minimum_required(VERSION 3.14)

add_executable(hexview hexview.c)
if(MSVC)
  target_compile_definitions(hexview PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_executable(gen_bindings gen_bindings.c)
if(MSVC)
  target_compile_definitions(gen_bindings PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
