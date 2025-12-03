cmake_minimum_required(VERSION 3.14)

# Define the VM library
add_library(concordia STATIC
    vm_exec.c
    vm_io.c
)

# Create an alias so users can link against concordia::vm if they prefer namespaced targets
add_library(concordia::vm ALIAS concordia)

# Include directories
# We point to the root include directory relative to this file
target_include_directories(concordia PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
)

# Compiler definitions
if(MSVC)
    target_compile_definitions(concordia PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(NOT WIN32)
    target_link_libraries(concordia PRIVATE m)
endif()

# Installation rules for the VM library
include(GNUInstallDirs)

install(TARGETS concordia
    EXPORT ConcordiaTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
