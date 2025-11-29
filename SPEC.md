# Concordia: IL-Based Command & Telemetry Framework

## Overview

Concordia is a schema-driven, IL-based system for encoding spacecraft commands and decoding telemetry. It is designed to be robust, deterministic, and lightweight, suitable for both embedded flight software and ground station systems.

The system includes:
- A human-readable DSL (`.cnd`) for defining packet schemas.
- A compiler (`cnd`) that emits compressed IL (Intermediate Language).
- A lightweight VM that executes IL to serialize commands and parse telemetry.
- Support for decorators to control encoding, validation, and metadata.
- A symmetric architecture where the same VM runs on ground and flight.

---

## 1. System Components

| Component       | Description |
|----------------|-------------|
| **DSL Parser**  | Parses schema definitions written in the custom `.cnd` DSL. |
| **IL Compiler** | Converts DSL into compact binary IL format (`.il`). |
| **IL VM**       | Executes IL to encode commands or decode telemetry. |
| **CLI Tool**    | `cnd` command-line interface for compiling, encoding, and decoding. |
| **LSP Server**  | Language Server Protocol implementation for IDE support (VS Code). |

---

## 2. DSL Language Features

The DSL is designed to be explicit about data layout. It supports the following types with strict bit-widths.
A single `.cnd` file may contain multiple `struct` definitions but is limited to **one** `packet` definition to ensure a clear 1:1 mapping between a schema file and a message type.

| Category | Type Names | Size (bits) | Description |
|---|---|---|---|
| **Unsigned Integers** | `uint8` (alias `byte`), `uint16`, `uint32`, `uint64` | 8, 16, 32, 64 | Standard unsigned integers |
| **Signed Integers** | `int8`, `int16`, `int32`, `int64` | 8, 16, 32, 64 | Two's complement signed integers |
| **Floating Point** | `float`, `double` | 32, 64 | IEEE 754 single/double precision |
| **Boolean** | `bool` | 1 | Single bit boolean (packed if possible) |
| **Text** | `string` | Variable | ASCII/UTF-8 text (null-terminated or prefixed) |
| **Complex** | `struct`, `enum`, `type[]` | Variable | Nested structures, enumerations, or arrays |
| **Bitfields** | `uintX:N` | N bits | Packed bitfields (e.g., `uint8:3`) |

### Example: Enum Definition

Enums allow defining named constants with an optional underlying integer type. If no type is specified, `uint32` is used.

```concordia
enum Status : uint8 {
  Ok = 0,
  Error = 1,
  Unknown = 2
}

enum Color {
  Red = 1,
  Green = 2,
  Blue = 3
}
```

### Example: Struct Definition

```concordia
struct Vector3 {
  float x; // 32 bits
  float y; // 32 bits
  float z; // 32 bits
}
```

### Example: Bitfield Packing

Bitfields allow you to pack multiple small values into a single integer container (like `uint8` or `uint32`) to save space. The syntax is `type name : bits`.

**Rule:** Bitfields are packed contiguously. If a non-bitfield type is encountered, or the container type changes, the current container is finalized (padded if necessary) and a new one begins.

```concordia
packet PowerStatus {
  // The following 5 fields are packed into a single uint8 (8 bits total)
  // [solar:1][battery:1][error:1][mode:3][reserved:2]
  
  uint8 solar_deployed : 1;   // 0 or 1
  uint8 battery_charging : 1; // 0 or 1
  uint8 error_flag : 1;       // 0 or 1
  uint8 mode : 3;             // 0 to 7
  uint8 reserved : 2;         // 0 to 3
  
  // Next field starts on a new byte boundary
  uint16 voltage;
}
```

### Example: Variable-Length Arrays (Slices)

Arrays can be variable in length, determined by a prefix field. This is useful for payloads like images or logs.

```concordia
packet ImageDownload {
  uint16 image_id;
  
  // 32-bit length prefix, followed by that many bytes
  byte image_data[] prefix uint32;
  
  // 8-bit length prefix, followed by that many floats
  float histogram[] prefix uint8;
}
```

### Example: Control Flow (If/Else & Switch)

Concordia supports conditional logic to include or exclude fields based on runtime values.

**If/Else Statements:**
Standard C-style conditional blocks. Conditions can use arithmetic, comparison, and logical operators.

```concordia
packet ConditionalPacket {
    bool has_extra;
    
    if (has_extra) {
        string extra_data prefix uint8;
    } else {
        uint8 fallback_code;
    }
}
```

**Switch Statements:**
Switch on a value (integer or enum) to select a block of fields.

```concordia
enum Mode { A=1, B=2 }

packet SwitchPacket {
    Mode current_mode;
    
    switch (current_mode) {
        case Mode.A: {
            uint16 val_a;
        }
        case Mode.B: {
            float val_b;
        }
        default: {
            uint8 unknown_data[4];
        }
    }
}
```

---

## 3. Supported Decorators

| Decorator | Purpose |
|----------|---------|
| `@const(value)` | Hardcoded field value (commands) |
| `@match(value)` | Field must match value (telemetry) |
| `@little_endian` / `@big_endian` | Byte order override |
| `@range(min, max)` | Input validation |
| `@scale(factor)` | Apply scaling |
| `@optional` | Optional field |
| `@crc16` | CRC-16 validation or generation |
| `@crc32` | CRC-32 validation or generation |
| `@count(n)` | Fixed array size |
| `@pad(n)` | Explicit padding (bits or bytes) |
| `@import("file")` | Import definitions from another file |

---

## 4. IL Instruction Set (Overview)

The VM executes a stream of opcodes. Here is a summary of the instruction categories:

| Category | Description | Examples |
|---|---|---|
| **Meta & State** | Manage VM state, endianness, and versioning. | `OP_SET_ENDIAN_LE`, `OP_META_VERSION` |
| **Primitives** | Read/Write basic types. | `OP_IO_U8`, `OP_IO_F32`, `OP_IO_BOOL` |
| **Bitfields** | Read/Write packed bits. | `OP_IO_BIT_U`, `OP_ALIGN_PAD` |
| **Arrays & Strings** | Handle strings and loops. | `OP_STR_NULL`, `OP_ARR_PRE_U16` |
| **Validation** | Check constraints and CRCs. | `OP_RANGE_CHECK`, `OP_CRC_16`, `OP_CONST_CHECK` |
| **Control Flow** | Conditional execution. | `OP_JUMP_IF_NOT`, `OP_SWITCH` |
| **Expressions** | Stack-based arithmetic and logic. | `OP_ADD`, `OP_EQ`, `OP_LOAD_CTX` |

---

## 5. CLI Tooling

The `cnd` tool provides a unified interface for all operations.

```bash
# Compile schema to IL
cnd compile schema.cnd schema.il

# Encode JSON to Binary
cnd encode schema.il input.json output.bin

# Decode Binary to JSON
cnd decode schema.il input.bin output.json

# Check version
cnd version
```

---

## 6. VM Architecture

The VM is architected as a **stateless execution engine**, separating the *logic* (IL bytecode) from the *processor* (VM loop).

| Concept | Description |
|---------|-------------|
| **The Engine** | A compiled C/C++ function containing a large `switch` statement over the opcodes. It is immutable and compiled once. |
| **The Logic** | The IL bytecode is treated as data. It can be loaded from disk, received over the network, or swapped at runtime. |
| **Hot Reload** | To update a schema, the host application simply passes a pointer to the new IL buffer on the next call to `vm_execute()`. No VM restart or software re-flash is required. |

This design allows the ground station or spacecraft to switch packet formats instantly without downtime, effectively acting as a **programmable state machine** defined entirely by the loaded IL.

## 7. Symmetric Architecture (Ground & Flight)

A key advantage of this IL-based approach is that the **same VM code and same IL schemas** run on both the ground and the spacecraft.

| Location | Role | Operation |
| :--- | :--- | :--- |
| **Ground Station** | **Encoder** | JSON Input + Command Schema -> Binary Command |
| **Spacecraft** | **Decoder** | Binary Command + Command Schema -> C Struct / Action |
| **Spacecraft** | **Encoder** | C Struct / Sensor Data + Telemetry Schema -> Binary Telemetry |
| **Ground Station** | **Decoder** | Binary Telemetry + Telemetry Schema -> JSON Output |

This **Isomorphic Design** eliminates the common source of errors where the ground parser (e.g., Python) and flight parser (e.g., C) disagree on how to handle a specific field. If it works on the ground simulator, it is guaranteed to work in orbit because the engine is identical.
