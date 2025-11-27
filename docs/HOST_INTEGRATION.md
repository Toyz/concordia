# Host Integration Guide

Concordia is designed to be embedded into C/C++ applications. The core runtime (`src/vm/`) is zero-allocation and uses a callback mechanism to interface with your application's data structures.

## 1. Include Headers

Include the main Concordia header in your application.

```c
#include "concordia.h"
```

## 2. Load the Program

The Concordia compiler (`cnd`) produces an Intermediate Language (`.il`) file. You need to load this binary data into a `cnd_program` structure.

```c
// Load .il file content into memory (e.g., from flash or file system)
const uint8_t* il_bytecode = ...; 
size_t il_size = ...;

cnd_program program;
cnd_program_load(&program, il_bytecode, il_size);
```

**Note on Imports:**
If your schema uses `@import`, the compiler combines all imported definitions into a single `.il` file. You only need to load this one file; the VM handles the internal structure transparently.

## 3. Implement the IO Callback

The VM does not know about your application's data layout. Instead, it calls a user-provided callback function whenever it needs to read or write a field.

The callback signature is:
```c
typedef cnd_error_t (*cnd_io_cb)(
    struct cnd_vm_ctx_t* ctx,
    uint16_t key_id,      // Unique ID of the field (from String Table)
    uint8_t type_opcode,  // The operation (e.g., OP_IO_U8, OP_ARR_START)
    void* data_ptr        // Pointer to read from or write to
);
```

**Note on Enums:**
Fields defined as `enum` in the schema are passed to the callback as their underlying integer type (e.g., `OP_IO_U32` or `OP_IO_U8`). The VM strictly enforces that the value matches one of the defined enum constants. If an invalid value is encountered during encoding or decoding, `cnd_execute` will return `CND_ERR_VALIDATION`.

### Example Callback

```c
typedef struct {
    uint8_t version;
    uint16_t voltage;
    // ...
} MyData;

cnd_error_t my_callback(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    MyData* data = (MyData*)ctx->user_ptr;

    // Map key_id to struct fields. 
    // Note: In a real app, you might use a generated header or a switch case.
    // The key_id corresponds to the order of strings in the .il file string table.
    
    if (key_id == 0) { // "version"
        if (ctx->mode == CND_MODE_ENCODE) {
            *(uint8_t*)ptr = data->version;
        } else {
            data->version = *(uint8_t*)ptr;
        }
    } else if (key_id == 1) { // "voltage"
        if (ctx->mode == CND_MODE_ENCODE) {
            *(uint16_t*)ptr = data->voltage;
        } else {
            data->voltage = *(uint16_t*)ptr;
        }
    }
    
    return CND_ERR_OK;
}
```

## 4. Execute the VM

Initialize the VM context and execute.

### Encoding (Struct -> Binary)

```c
uint8_t buffer[1024];
MyData my_data = { .version = 1, .voltage = 3300 };

cnd_vm_ctx ctx;
cnd_init(&ctx, CND_MODE_ENCODE, &program, buffer, sizeof(buffer), my_callback, &my_data);

cnd_error_t err = cnd_execute(&ctx);
if (err == CND_ERR_OK) {
    printf("Encoded %zu bytes\n", ctx.cursor);
}
```

### Decoding (Binary -> Struct)

```c
cnd_vm_ctx ctx;
cnd_init(&ctx, CND_MODE_DECODE, &program, buffer, received_len, my_callback, &my_data);

cnd_error_t err = cnd_execute(&ctx);
if (err == CND_ERR_OK) {
    printf("Decoded Version: %d\n", my_data.version);
}
```

## 5. Handling Arrays and Strings

For arrays and strings, the callback protocol is slightly different.

- **Arrays**: The VM calls the callback with `OP_ARR_PRE_...` or `OP_ARR_FIXED`. The `data_ptr` points to a count variable.
    - **Encode**: You write the array count to `*ptr`.
    - **Decode**: You read the array count from `*ptr` and prepare your storage.
    - **Loop**: The VM will then loop `count` times, calling the callback for the inner fields. You need to maintain an index in your `user_ptr` context to know which element to access.

- **Strings**: The VM calls the callback with `OP_STR_...`. The `data_ptr` is a `char**` (Encode) or `char*` (Decode).
    - **Encode**: You write your string pointer to `*(const char**)ptr`.
    - **Decode**: `ptr` points to the string in the binary buffer. You should copy it to your storage.

## 6. Error Handling

The `cnd_execute` function returns a `cnd_error_t`.

- `CND_ERR_OK`: Success.
- `CND_ERR_OOB`: Buffer overflow (read or write).
- `CND_ERR_INVALID_OP`: Corrupt bytecode.
- `CND_ERR_VALIDATION`: A `@const` or `@range` check failed.
- `CND_ERR_CALLBACK`: Your callback returned an error.
