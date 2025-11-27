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

## Installation

### Requirements
*   CMake 3.14+
*   C99 Compiler (GCC, Clang, MSVC)

### Build from Source

```bash
git clone https://github.com/username/concordia.git
cd concordia
mkdir build && cd build
cmake ..
cmake --build .
```

This will build:
*   `cnd`: The CLI tool for compiling schemas.
*   `concordia`: The static library for the VM.
*   `test_runner`: The unit test suite.

## Usage

### 1. Define a Schema (`telemetry.cnd`)

```cnd
packet Telemetry {
    @const(0xCAFE)
    uint16 sync_word;

    @unit("Celsius")
    float temperature;

    @count(3)
    uint8 sensors[3];
    
    uint8 status : 1;
    uint8 error  : 1;
    uint8 mode   : 6;
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
