# Project Status & Roadmap

## ✅ Completed Features

### Core Virtual Machine (`src/vm/`)
- [x] **Architecture**: Zero-allocation, context-based execution model.
- [x] **Primitives**: Full support for `uint8` - `uint64`, `int8` - `int64`, `float`, `double`.
- [x] **Endianness**: Runtime switching via `SET_ENDIAN_LE` / `SET_ENDIAN_BE`.
- [x] **Bitfields**: Packing and unpacking of unsigned bits (`IO_BIT_U`) and explicit padding (`ALIGN_PAD`).
- [x] **Structs**: Nested structure support via `ENTER_STRUCT` / `EXIT_STRUCT`.
- [x] **Arrays**: Fixed-count loops (`ARR_FIXED` + `ARR_END`) and Variable-length arrays (`ARR_PRE_U8`..`U32`).
- [x] **Strings**: Null-terminated string support (`STR_NULL`) and Prefix-based strings (`STR_PRE_U8`..`U32`).
- [x] **Constants**: Symmetric `CONST_CHECK` (Write on Encode, Validate on Decode).

### Compiler (`src/compiler/cndc.c`)
- [x] **Parser**: Recursive descent parser for `.cnd` DSL.
- [x] **Code Generation**: Emits optimized `.il` binary format.
- [x] **Features**:
    - Struct definitions and referencing.
    - Packet definitions.
    - Decorators: `@const`, `@count`, `@version`.
    - String options (`max`, `until`, `prefix`).
    - Array options (`[]`, `[N]`, `prefix`).
- [x] **String Table**: Automatic deduplication of field names.

### Tooling & CLI (`cnd`)
- [x] **Commands**:
    - `compile`: Source to IL.
    - `encode`: JSON + IL -> Binary.
    - `decode`: Binary + IL -> JSON.
- [x] **JSON Binding**: Stack-based context for handling nested JSON objects.

### Infrastructure
- [x] **Build System**: CMake 3.14+ with `FetchContent` for dependencies.
- [x] **Testing**: Google Test integration.
- [x] **Compiler Tests**: Unit tests for the compiler logic (`cnd_compile_file`).
- [x] **Dependencies**: cJSON (for CLI) and GoogleTest (for Unit Tests).
- [x] **Code Organization**: VM logic refactored into `src/vm/` directory.

### Language Features
- [x] **`bool` Type**: Added explicit boolean type for byte-aligned and bitfields.
- [x] **Lexer Refactoring**: Implemented keyword map for cleaner, more extensible lexing.
- [x] **Enum Support**: Added `enum` keyword with underlying types, validation, and `Enum.Value` syntax sugar.
- [x] **Tagged Unions (`switch`)**: Conditional field inclusion based on a discriminator, supporting integer and enum values, default cases, and nested usage.

---

## 🚧 Remaining / Todo

### 1. Advanced Data Types
- [x] **Signed Bitfields**: Implement sign-extension logic for `IO_BIT_I`.

### 2. Validation & Transformation
- [x] **Range Checking**: Implement `RANGE_CHECK` opcode and `@range(min, max)` compiler support.
- [x] **Scaling**: Implement `SCALE_LIN` opcode and `@scale(factor)` / `@offset(val)` compiler support (Raw <-> Engineering value conversion). Supports both Float (linear) and Integer (mul, div, add, sub) transforms.
- [x] **CRC/Checksum**: Implement `CRC_16` opcode for integrity verification.

### 3. Control Flow
- [x] **Conditionals**: Implement `JUMP_IF_NOT` opcode and `@depends_on(field)` decorator logic in compiler to allow optional fields.
- [x] **Conceptual: Conditional Field Inclusion (if/else)**:
    This feature allows fields or blocks of fields to be conditionally included in the binary based on complex logic involving previous fields (bitwise operations, comparisons, etc.).
    **Proposed Syntax**:
    ```cnd
    packet ConditionalData {
        uint8 status_flags;
        int16 temperature;

        // Bitmask Check: Has GPS Data
        if (status_flags & 0x01) {
            Vec3 position;
        }

        // Complex Logic: "If high temp OR error mode"
        if ((status_flags & 0x80) || temperature > 100) {
            uint16 alarm_code;
        }

        // Range Check: "If value is within valid bounds"
        if (pressure >= 0 && pressure <= 5000) {
            uint8 valid_reading;
        } else {
            uint8 error_code;
        }
    }
    ```
    **Implementation Sketch**:
    *   **Compiler**: 
        *   Implement an expression parser to convert conditions into RPN (Reverse Polish Notation) bytecode.
        *   **Operator Precedence**: Handle standard C-style precedence (e.g., `&` > `==` > `&&` > `||`) to ensure correct evaluation order.
        *   **Branching**: Support `else` blocks by inserting unconditional jumps at the end of `if` blocks.
    *   **VM Architecture**: Add a small "Expression Stack" (e.g., `uint64_t expr_stack[8]`) to the VM Context.
    *   **New Opcodes**:
        *   `OP_LOAD_CTX key`: Queries host for field value, pushes to stack.
        *   `OP_PUSH_IMM val`: Pushes immediate constant to stack.
        *   **ALU Operations**:
            *   **Bitwise**: `OP_BIT_AND`, `OP_BIT_OR`, `OP_BIT_XOR`, `OP_BIT_NOT`, `OP_SHL`, `OP_SHR`
            *   **Comparison**: `OP_EQ`, `OP_NEQ`, `OP_GT`, `OP_LT`, `OP_GTE`, `OP_LTE`
            *   **Logical**: `OP_LOG_AND`, `OP_LOG_OR`, `OP_LOG_NOT`
        *   `OP_JUMP_IF_FALSE off`: Pops top of stack; if 0 (false), jumps `off` bytes (skipping the block).
        *   `OP_JUMP off`: Unconditional jump (used to skip `else` block after executing `if` block).

### 4. CLI & JSON Improvements
- [x] **Robust Array Handling**: Improve `json_io_callback` to correctly track array indices during loops. This will likely involve making the `IOCtx` more stateful for array elements.
- [x] **Hex/Binary Output**: Add options to output binary data as Hex strings in JSON for `uint8[]` blobs.
- [x] **Refactor VM Architecture**: Separate 'Program' (Code) from 'Context' (State) to support concurrent execution and shared resources.

### 5. Documentation & Examples
- [x] **Language Guide**: Detailed documentation of the `.cnd` syntax.
- [x] **Host Integration Guide**: Example C++ dispatcher loop for embedded targets.

### 6. Compiler Enhancements
- [x] **Imports**: Add `@import("file.cnd")` support to allow splitting definitions across multiple files. The compiler should resolve these and emit a single `.il` file containing all necessary bytecode.
