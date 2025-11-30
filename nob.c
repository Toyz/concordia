#define NOB_IMPLEMENTATION
#include "nob.h"

#define BUILD_DIR "./build_nob"
#define DEPS_DIR "./nob_deps"

const char *filename_from_path(const char *path) {
    const char *last_slash = strrchr(path, '/');
    if (last_slash) return last_slash + 1;
    return path;
}

void cflags(Nob_Cmd *cmd) {
    nob_cmd_append(cmd, "-Wall", "-Wextra", "-std=c99", "-Iinclude", "-I" DEPS_DIR "/cJSON");
    nob_cmd_append(cmd, "-DCND_VERSION=\"nob-build\"", "-DCND_GIT_HASH=\"unknown\"");
}

bool setup_deps() {
    if (!nob_mkdir_if_not_exists(DEPS_DIR)) return false;
    
    const char *cjson_dir = DEPS_DIR "/cJSON";
    if (nob_file_exists(cjson_dir)) return true;

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "git", "clone", "https://github.com/DaveGamble/cJSON.git", cjson_dir);
    bool ok = nob_cmd_run_sync(cmd);
    nob_cmd_free(cmd);
    return ok;
}

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    if (!setup_deps()) return 1;
    if (!nob_mkdir_if_not_exists(BUILD_DIR)) return 1;

    Nob_Cmd cmd = {0};
    Nob_File_Paths obj_files = {0};

    // --- Compile cJSON ---
    const char *cjson_src = DEPS_DIR "/cJSON/cJSON.c";
    const char *cjson_obj = BUILD_DIR "/cJSON.o";
    cmd.count = 0;
    nob_cmd_append(&cmd, "cc");
    cflags(&cmd);
    nob_cmd_append(&cmd, "-c", cjson_src, "-o", cjson_obj);
    if (!nob_cmd_run_sync(cmd)) return 1;
    nob_da_append(&obj_files, cjson_obj);

    // --- Compile Concordia Lib ---
    Nob_File_Paths concordia_objs = {0};
    const char *concordia_srcs[] = {
        "src/vm/vm_exec.c",
        "src/vm/vm_io.c"
    };
    for (size_t i = 0; i < NOB_ARRAY_LEN(concordia_srcs); ++i) {
        const char *src = concordia_srcs[i];
        const char *obj = nob_temp_sprintf(BUILD_DIR "/%s.o", filename_from_path(src));
        cmd.count = 0;
        nob_cmd_append(&cmd, "cc");
        cflags(&cmd);
        nob_cmd_append(&cmd, "-c", src, "-o", obj);
        if (!nob_cmd_run_sync(cmd)) return 1;
        nob_da_append(&obj_files, obj);
        nob_da_append(&concordia_objs, obj);
    }
    // Create libconcordia.a
    cmd.count = 0;
    nob_cmd_append(&cmd, "ar", "rcs", BUILD_DIR "/libconcordia.a");
    for (size_t i = 0; i < concordia_objs.count; ++i) nob_cmd_append(&cmd, concordia_objs.items[i]);
    if (!nob_cmd_run_sync(cmd)) return 1;
    nob_da_free(concordia_objs);

    // --- Compile Compiler Lib ---
    Nob_File_Paths compiler_objs = {0};
    const char *compiler_srcs[] = {
        "src/compiler/cndc.c",
        "src/compiler/cnd_utils.c",
        "src/compiler/cnd_lexer.c",
        "src/compiler/cnd_parser.c",
        "src/compiler/cnd_fmt.c"
    };
    for (size_t i = 0; i < NOB_ARRAY_LEN(compiler_srcs); ++i) {
        const char *src = compiler_srcs[i];
        const char *obj = nob_temp_sprintf(BUILD_DIR "/%s.o", filename_from_path(src));
        cmd.count = 0;
        nob_cmd_append(&cmd, "cc");
        cflags(&cmd);
        nob_cmd_append(&cmd, "-c", src, "-o", obj);
        if (!nob_cmd_run_sync(cmd)) return 1;
        nob_da_append(&obj_files, obj);
        nob_da_append(&compiler_objs, obj);
    }
    // Create libcnd_compiler.a
    cmd.count = 0;
    nob_cmd_append(&cmd, "ar", "rcs", BUILD_DIR "/libcnd_compiler.a");
    for (size_t i = 0; i < compiler_objs.count; ++i) nob_cmd_append(&cmd, compiler_objs.items[i]);
    if (!nob_cmd_run_sync(cmd)) return 1;
    nob_da_free(compiler_objs);

    // --- Compile CLI ---
    const char *cli_srcs[] = {
        "src/cli/main.c",
        "src/cli/cli_helpers.c",
        "src/cli/json_binding.c",
        "src/cli/cmd_compile.c",
        "src/cli/cmd_encode.c",
        "src/cli/cmd_decode.c",
        "src/cli/cmd_fmt.c",
        "src/cli/cmd_inspect.c",
        "src/cli/cmd_lsp.c"
    };
    for (size_t i = 0; i < NOB_ARRAY_LEN(cli_srcs); ++i) {
        const char *src = cli_srcs[i];
        const char *obj = nob_temp_sprintf(BUILD_DIR "/%s.o", filename_from_path(src));
        cmd.count = 0;
        nob_cmd_append(&cmd, "cc");
        cflags(&cmd);
        nob_cmd_append(&cmd, "-c", src, "-o", obj);
        if (!nob_cmd_run_sync(cmd)) return 1;
        nob_da_append(&obj_files, obj);
    }

    // --- Link cnd ---
    cmd.count = 0;
    nob_cmd_append(&cmd, "cc");
    nob_cmd_append(&cmd, "-o", BUILD_DIR "/cnd");
    for (size_t i = 0; i < obj_files.count; ++i) {
        nob_cmd_append(&cmd, obj_files.items[i]);
    }
    if (!nob_cmd_run_sync(cmd)) return 1;

    // --- Compile Hexview ---
    cmd.count = 0;
    nob_cmd_append(&cmd, "cc");
    cflags(&cmd);
    nob_cmd_append(&cmd, "src/tools/hexview.c", "-o", BUILD_DIR "/hexview");
    if (!nob_cmd_run_sync(cmd)) return 1;

    nob_cmd_free(cmd);
    nob_da_free(obj_files);
    return 0;
}
