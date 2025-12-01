cmake_minimum_required(VERSION 3.14)

add_executable(kitchen_sink kitchen_sink/kitchen_sink.c)
target_link_libraries(kitchen_sink PRIVATE concordia cnd_compiler)
