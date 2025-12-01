cmake_minimum_required(VERSION 3.14)

set(CND_SOURCES
    main.c
    cli_helpers.c
    json_binding.c
    cmd_compile.c
    cmd_encode.c
    cmd_decode.c
    cmd_fmt.c
    cmd_inspect.c
    cmd_lsp.c
)

add_executable(cnd ${CND_SOURCES})

# Link against our libraries
# Note: cjson is defined in the root CMakeLists.txt
target_link_libraries(cnd 
    PRIVATE 
    concordia 
    cnd_compiler 
    cjson
)

if(MSVC)
  target_compile_definitions(cnd PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

include(GNUInstallDirs)

install(TARGETS cnd
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
