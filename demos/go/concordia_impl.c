// Wrapper to compile Concordia sources with CGO
// CGO compiles all .c files in the directory.
// We include the implementation files here so they are compiled and linked.

#include "../../src/vm/vm_exec.c"
#include "../../src/vm/vm_io.c"

// Forward declaration of the Go callback (exported from Go)
extern int go_io_callback(void* user_ptr, int mode, uint16_t key_id, uint8_t type, void* ptr);

// Gateway function to match the cnd_io_cb signature
cnd_error_t c_gateway(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    return (cnd_error_t)go_io_callback(ctx->user_ptr, (int)ctx->mode, key_id, type, ptr);
}
