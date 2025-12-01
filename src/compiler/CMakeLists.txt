cmake_minimum_required(VERSION 3.14)

add_library(cnd_compiler STATIC
    cndc.c
    cnd_utils.c
    cnd_lexer.c
    cnd_parser.c
    cnd_fmt.c
)

add_library(concordia::compiler ALIAS cnd_compiler)

target_include_directories(cnd_compiler PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
)

if(MSVC)
  target_compile_definitions(cnd_compiler PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

include(GNUInstallDirs)

install(TARGETS cnd_compiler
    EXPORT ConcordiaTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
