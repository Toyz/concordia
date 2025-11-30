#ifndef NOB_CONCORDIA_H
#define NOB_CONCORDIA_H

#include "nob.h"

// --- Setup & Build ---

// Clones and builds Concordia in the specified directory.
// Returns true on success.
bool cnd_setup(const char *deps_dir) {
    if (!nob_mkdir_if_not_exists(deps_dir)) return false;

    const char *concordia_dir = nob_temp_sprintf("%s/concordia", deps_dir);
    
    // 1. Clone if not exists
    if (!nob_file_exists(concordia_dir)) {
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "git", "clone", "https://github.com/Toyz/concordia.git", concordia_dir);
        if (!nob_cmd_run_sync(cmd)) {
            nob_cmd_free(cmd);
            return false;
        }
        nob_cmd_free(cmd);
    }

    // 2. Build Concordia using its own nob.c
    // We compile the nob.c inside the repo and run it.
    const char *nob_src = nob_temp_sprintf("%s/nob.c", concordia_dir);
    const char *nob_exe = nob_temp_sprintf("%s/nob", concordia_dir);
    
    // Check if nob.c exists (it should if cloned correctly)
    if (!nob_file_exists(nob_src)) {
        nob_log(NOB_ERROR, "Concordia source at %s does not contain nob.c", concordia_dir);
        return false;
    }

    // Compile nob
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "cc", nob_src, "-o", nob_exe);
    if (!nob_cmd_run_sync(cmd)) {
        nob_cmd_free(cmd);
        return false;
    }
    
    // Run nob (inside the directory to keep paths relative)
    // We need to change directory to run it properly? 
    // nob.c uses relative paths like "./build_nob".
    // So we should run it with CWD set to concordia_dir.
    // nob_cmd_run_sync doesn't support setting CWD easily without fork/exec manually or changing global CWD.
    // Changing global CWD is risky.
    // However, we can use "sh -c 'cd ... && ./nob'"
    
    cmd.count = 0;
    nob_cmd_append(&cmd, "sh", "-c", nob_temp_sprintf("cd %s && ./nob", concordia_dir));
    if (!nob_cmd_run_sync(cmd)) {
        nob_cmd_free(cmd);
        return false;
    }
    nob_cmd_free(cmd);

    return true;
}

// Adds include flags for Concordia: -I.../include
void cnd_cflags(Nob_Cmd *cmd, const char *deps_dir) {
    nob_cmd_append(cmd, nob_temp_sprintf("-I%s/concordia/include", deps_dir));
}

// Adds link flags for Concordia: -L... -lconcordia -lcnd_compiler
void cnd_ldflags(Nob_Cmd *cmd, const char *deps_dir) {
    const char *lib_dir = nob_temp_sprintf("%s/concordia/build_nob", deps_dir);
    nob_cmd_append(cmd, nob_temp_sprintf("-L%s", lib_dir));
    nob_cmd_append(cmd, "-lconcordia", "-lcnd_compiler");
}

// Returns the path to the cnd executable
const char *cnd_tool_path(const char *deps_dir) {
    return nob_temp_sprintf("%s/concordia/build_nob/cnd", deps_dir);
}

// --- Tools ---

// Helper function to compile a Concordia file using the cnd CLI
// Example: cnd_compile(cnd_tool_path("deps"), "src/main.cnd", "build/main.cnd.o");
bool cnd_compile(const char *cnd_binary, const char *input_path, const char *output_path) {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, cnd_binary, "compile", input_path, "-o", output_path);
    bool result = nob_cmd_run_sync(cmd);
    nob_cmd_free(cmd);
    return result;
}

// Helper to encode data to Concordia binary format
// Example: cnd_encode(cnd_tool_path("deps"), "schema.cnd", "data.json", "data.bin");
bool cnd_encode(const char *cnd_binary, const char *schema_path, const char *input_json, const char *output_bin) {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, cnd_binary, "encode", schema_path, input_json, "-o", output_bin);
    bool result = nob_cmd_run_sync(cmd);
    nob_cmd_free(cmd);
    return result;
}

// Helper to decode Concordia binary format to JSON
bool cnd_decode(const char *cnd_binary, const char *schema_path, const char *input_bin) {
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, cnd_binary, "decode", schema_path, input_bin);
    bool result = nob_cmd_run_sync(cmd);
    nob_cmd_free(cmd);
    return result;
}

#endif // NOB_CONCORDIA_H
