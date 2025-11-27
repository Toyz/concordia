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

### 4. CLI & JSON Improvements
- [x] **Robust Array Handling**: Improve `json_io_callback` to correctly track array indices during loops. This will likely involve making the `IOCtx` more stateful for array elements.
- [ ] **Hex/Binary Output**: Add options to output binary data as Hex strings in JSON for `uint8[]` blobs.
- [x] **Refactor VM Architecture**: Separate 'Program' (Code) from 'Context' (State) to support concurrent execution and shared resources.

### 5. Documentation & Examples
- [x] **Language Guide**: Detailed documentation of the `.cnd` syntax.
- [x] **Host Integration Guide**: Example C++ dispatcher loop for embedded targets.

### 6. Compiler Enhancements
- [x] **Imports**: Add `@import("file.cnd")` support to allow splitting definitions across multiple files. The compiler should resolve these and emit a single `.il` file containing all necessary bytecode.
