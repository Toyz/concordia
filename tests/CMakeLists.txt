cmake_minimum_required(VERSION 3.14)

set(TEST_SOURCES
    test_common.cpp
    vm_tests.cpp
    compiler_test.cpp 
    lexer_test.cpp
    compiler_integration_test.cpp
    coverage_test.cpp
    language_features_test.cpp
)

add_executable(test_runner ${TEST_SOURCES})

# Link against libraries
target_link_libraries(test_runner 
    PRIVATE 
    concordia 
    cnd_compiler 
    gtest_main
)

# Ensure we can find the project headers if not fully propagated (though they should be)
target_include_directories(test_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

if(MSVC)
  target_compile_definitions(test_runner PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
