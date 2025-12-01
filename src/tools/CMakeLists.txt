cmake_minimum_required(VERSION 3.14)

add_executable(hexview hexview.c)
if(MSVC)
  target_compile_definitions(hexview PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
