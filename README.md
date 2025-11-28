# Concordia

![CI](https://github.com/Toyz/concordia/actions/workflows/ci.yml/badge.svg)
![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Standard](https://img.shields.io/badge/standard-C99-green.svg)

**The Universal Binary Protocol for Aerospace and Embedded Systems.**

Concordia is a schema-driven, IL-based serialization framework designed for environments where reliability, bandwidth, and flexibility are critical. It decouples data definition from data processing, allowing you to update packet structures in orbit without reflashing flight software.

---

## Key Features

*   **Zero Allocation & Zero Copy:** The VM acts as a "Virtual DMA," moving data directly between the wire and your C structs. No intermediate objects, no `malloc`.
*   **Hot Reloadable:** Logic is data. Update your telemetry definitions by uploading a tiny `.il` file. The VM adapts instantly.
*   **Isomorphic:** The exact same VM code runs on your microcontroller (C), your ground station (C++/Go), and your web dashboard (WASM).
*   **Bit-Perfect Control:** Explicit support for bitfields, endianness, padding, and alignment. You control every bit on the wire.
*   **Language Agnostic:** Bindings for C, C++, Go, and WebAssembly included.
*   **Ultra-Compact:** The entire VM binary is ~90KB (Windows/Debug) and <10KB (Embedded/Release), making it ideal for constrained environments.

### Comparison

| Feature | Concordia | FlatBuffers | Protobuf |
| :--- | :--- | :--- | :--- |
| **Paradigm** | Schema-Driven VM | Offset-Based Access | Hydration / Parse |
| **Runtime Footprint** | ~4KB (Static VM) | Varies (Generated Code) | High (Lib + Generated) |
| **Wire Density** | Max (Pure Entropy) | Low (Offsets + Padding) | Med (Tag Overhead) |
| **Hot Reload** | ✅ Yes (Upload IL) | ❌ No (Recompile Binary) | ❌ No (Recompile Binary) |
| **Bit-Level Control** | ✅ Yes (Bitfields/Logic) | ❌ No (Byte Aligned) | ❌ No (Varints Only) |
| **Safety** | ✅ Sandboxed (Bound/CRC) | ⚠️ Unsafe Pointers | ⚠️ Allocation/Complexity |
| **Tooling Size** | 91KB (All-in-One) | ~2MB (Compiler) | ~20MB (Compiler) |

## Safety & Reliability

Concordia is engineered for hostile environments where stability is paramount.

*   **Memory Safety:** The VM is sandboxed. It cannot read or write outside the bounds of the provided buffer or user context. All pointer arithmetic is internal and verified.
*   **Deterministic Resource Usage:** No dynamic memory allocation (`malloc`) means no heap fragmentation or OOM crashes. Stack usage is fixed and predictable.
*   **Stack Protection:** Recursion and loops are tracked on a fixed-size internal stack, preventing stack overflow crashes even with deeply nested structures.
*   **Input Validation:** Implicit validation of enums, ranges, and array sizes ensures that your application logic only ever sees valid data.

## Installation

### Requirements
*   CMake 3.14+
*   C99 Compiler (GCC, Clang, MSVC)

### Build from Source

```bash
git clone https://github.com/Toyz/concordia.git
cd concordia
mkdir build && cd build
cmake ..
cmake --build .
```

This will build:
*   `cnd`: The CLI tool for compiling schemas.
*   `concordia`: The static library for the VM.
*   `test_runner`: The unit test suite.
*   `vm_benchmark`: Performance benchmarks.

### Running Benchmarks

To run the performance benchmarks:

```bash
./build/vm_benchmark
```

## Usage

### 1. Define a Schema (`telemetry.cnd`)

A `.cnd` file can contain multiple `struct` definitions but only **one** `packet` definition.

```cnd
enum Status : uint8 {
    Ok = 0,
    Error = 1
}

packet Telemetry {
    @const(0xCAFE)
    uint16 sync_word;

    /// Temperature in Celsius
    float temperature;

    @count(3)
    uint8 sensors[3];
    
    Status status;
    
    uint8 flags : 4;
    uint8 mode  : 4;
}
```

### 2. Compile to IL

```bash
./cnd compile telemetry.cnd telemetry.il
```

### 3. Run in your Application (C Example)

```c
// Load the IL
cnd_program_load(&prog, il_data, il_size);

// Initialize VM (Zero-Copy)
cnd_init(&ctx, CND_MODE_ENCODE, &prog, buffer, 128, my_callback, &my_struct);

// Execute
cnd_execute(&ctx);
```

## Project Structure

*   `src/compiler`: The `cnd` compiler source (DSL -> IL).
*   `src/vm`: The lightweight Virtual Machine (IL -> Binary).
*   `include/`: Public headers.
*   `demos/`: Examples in C, C++, Go, and WASM.
*   `editors/`: VS Code extension for syntax highlighting and IntelliSense.
*   `docs/`: Detailed documentation.

## Documentation

*   [Language Guide](docs/LANGUAGE_GUIDE.md): Learn the `.cnd` syntax.
*   [Host Integration](docs/HOST_INTEGRATION.md): How to embed the VM in your C/C++ app.
*   [Specification](SPEC.md): Deep dive into the architecture and IL opcodes.

## Demos

Check out the `demos/` folder for complete working examples:
*   **C/C++**: Basic encoding/decoding.
*   **Go**: Using `cgo` for static linking.
*   **WASM**: Running the VM in a browser.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
