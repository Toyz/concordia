cmake_minimum_required(VERSION 3.14)

set(TEST_SOURCES
    test_common.cpp
    vm_opcode_tests.cpp
    compiler_tests.cpp 
    lexer_tests.cpp
    feature_tests.cpp
    verifier_tests.cpp
)

add_executable(test_runner ${TEST_SOURCES})

# Link against libraries
target_link_libraries(test_runner 
    PRIVATE 
    concordia 
    cnd_compiler 
    gtest_main
)

add_executable(unaligned_benchmark unaligned_benchmark.cpp)
target_link_libraries(unaligned_benchmark PRIVATE concordia benchmark::benchmark)

# Ensure we can find the project headers if not fully propagated (though they should be)
target_include_directories(test_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

if(MSVC)
  target_compile_definitions(test_runner PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(SKIP_TEST_DISCOVERY)
    add_test(NAME test_runner COMMAND test_runner)
else()
    include(GoogleTest)
    gtest_discover_tests(test_runner)
endif()
