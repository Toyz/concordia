cmake_minimum_required(VERSION 3.14)

add_executable(demo_c demo_c.c)
target_link_libraries(demo_c PRIVATE concordia)

add_executable(demo_cpp demo_cpp.cpp)
target_link_libraries(demo_cpp PRIVATE concordia)
