# Concordia Protocol Tools

This repository contains the reference implementation of the Concordia Protocol, including the IL Virtual Machine and the Schema Compiler.

## Build

Requirements: CMake 3.14+, C99 Compiler (GCC/Clang/MSVC), C++14 Compiler (for Tests).

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## Components

### 1. The Compiler (`cndc`)

Converts human-readable schema files (`.cnd`) into binary Intermediate Language (`.il`) files.

**Usage:**
```bash
./cndc <input.cnd> <output.il>
```

**Example Schema (`example.cnd`):**
```cnd
@version(1)
@import("types.cnd")

packet Status {
    uint16 voltage;
    @count(3)
    uint8 sensors[3];
    string log until 0x00 max 32;
}
```

### 2. The Virtual Machine (`libconcordia`)

A header-only/static library for embedding in flight software or ground stations.

*   **Header:** `include/concordia.h`
*   **Source:** `src/vm.c`

**Features:**
*   Zero Memory Allocation (uses provided buffers).
*   Portable C99.
*   Endianness aware.
*   Bitfield support.

## Testing

The project uses **Google Test** for unit and integration testing.
CMake will automatically download and build Google Test via `FetchContent`.

```bash
./test_runner
```
