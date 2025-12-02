// bridge.c
// This file includes the C implementation directly to allow CGO to compile it
// as part of the Go package without requiring a separate library installation.

// We need to define these macros to ensure the C files compile correctly in this context
#define CND_IMPLEMENTATION

#include "../src/vm/vm_exec.c"
#include "../src/vm/vm_io.c"
