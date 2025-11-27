# IL-Based Command & Telemetry Framework

## Overview

This document outlines the design of a schema-driven, IL-based system for encoding spacecraft commands and decoding telemetry. The system includes:

- A human-readable DSL for defining packet schemas
- A compiler that emits compressed IL (Intermediate Language)
- A lightweight VM that executes IL to serialize commands and parse telemetry
- Support for decorators to control encoding, validation, and metadata
- A test-driven development (TDD) plan for implementation

---

## 1. System Components

| Component       | Description |
|----------------|-------------|
| **DSL Parser**  | Parses schema definitions written in the custom DSL |
| **IL Compiler** | Converts DSL into compact binary IL format |
| **IL VM**       | Executes IL to encode commands or decode telemetry |
| **Input Binder**| Maps user inputs to schema fields (for commands) |
| **Validator**   | Applies decorators like range, optional, etc. |
| **Serializer**  | Emits binary output from parsed and validated fields |
| **Deserializer**| Parses binary telemetry into structured output |
| **Cache Layer** | Stores parsed telemetry with invalidation logic |

---

## 2. DSL Language Features

The DSL is designed to be explicit about data layout. It supports the following types with strict bit-widths.
A single `.cnd` file may contain multiple `struct` definitions but is limited to **one** `packet` definition to ensure a clear 1:1 mapping between a schema file and a message type.

| Category | Type Names | Size (bits) | Description |
|---|---|---|---|
| **Unsigned Integers** | `uint8` (alias `byte`), `uint16`, `uint32`, `uint64` | 8, 16, 32, 64 | Standard unsigned integers |
| **Signed Integers** | `int8`, `int16`, `int32`, `int64` | 8, 16, 32, 64 | Two's complement signed integers |
| **Floating Point** | `float`, `double` | 32, 64 | IEEE 754 single/double precision |
| **Text** | `string` | Variable | ASCII/UTF-8 text (null-terminated or prefixed) |
| **Complex** | `struct`, `type[]` | Variable | Nested structures or arrays (fixed or prefixed) |
| **Bitfields** | `uintX:N` | N bits | Packed bitfields (e.g., `uint8:3`) |

### Example: Struct Definition

```il
struct Vector3 {
  float x; // 32 bits
  float y; // 32 bits
  float z; // 32 bits
}
```

### Example: Bitfield Packing

Bitfields allow you to pack multiple small values into a single integer container (like `uint8` or `uint32`) to save space. The syntax is `type name : bits`.

**Rule:** Bitfields are packed contiguously. If a non-bitfield type is encountered, or the container type changes, the current container is finalized (padded if necessary) and a new one begins.

```il
telemetry PowerStatus {
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

### Example: Mixed Command with Bitfields

```il
struct Flags {
  uint8 enable_logging : 1;
  uint8 enable_radio   : 1;
  uint8 power_mode     : 2;
  
  // Explicit padding to fill the remaining 4 bits of the byte
  @pad(4)
  uint8 _reserved : 4;
}

command ConfigureSystem {
  uint16 command_id;

  // Byte 2: Packed flags (inside a struct)
  Flags system_flags;

  // Byte 3-6: Standard field (starts on new byte)
  uint32 timeout_ms;

  // Byte 7: Another packed set (inline)
  uint8 retry_count : 4;
  uint8 priority    : 4;
}
```

### Example: Variable-Length Arrays (Slices)

Arrays can be variable in length, determined by a prefix field. This is useful for payloads like images or logs.

```il
telemetry ImageDownload {
  uint16 image_id;
  
  // 32-bit length prefix, followed by that many bytes
  byte image_data[] prefix uint32;
  
  // 8-bit length prefix, followed by that many floats
  float histogram[] prefix uint8;
}
```

### Example: Command Schema

```il
command SetNavigation {
  @const(0x1001)
  uint16 command_id; // 16 bits

  @little_endian
  Vector3 target_position; // 96 bits (3 * 32)

  // Fixed-size array (explicit count)
  @count(4)
  float quaternions[4]; // 128 bits (4 * 32)

  // Variable-length byte array (uint16 count prefix)
  byte payload[] prefix uint16; // 16 bits (len) + (len * 8 bits)

  @scale(0.1)
  @unit("seconds")
  uint32 duration; // 32 bits

  @optional
  @string_encoding("ascii")
  string comment until 0x00 max 64; // Variable length + 8 bits (null), max 64 bytes
}
```



### Example: Telemetry Schema

```il
telemetry StatusPacket {
  @match(0x42)
  uint8 packet_type;

  @big_endian
  uint16 voltage_mv;

  @scale(0.01)
  @unit("Celsius")
  float temperature;

  Vector3 velocity;

  @count(3)
  uint8 sensor_status[3];

  // Null-terminated string
  @string_encoding("ascii")
  string status until 0x00 max 32;

  // Length-prefixed string (uint8 length)
  string log_message prefix uint8;

  @crc16
  uint16 checksum;
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
| `@crc16` | CRC validation or generation |
| `@count(n)` | Fixed array size |
| `@pad(n)` | Explicit padding (bits or bytes) |
| `@import("file")` | Import definitions from another file |

---

## 4. IL Instruction Set (Sample)

| Opcode             | Description |
|--------------------|-------------|
| `PUSH_CONST_U16`   | Write constant 16-bit value |
| `LOAD_U8`          | Load 8-bit value from input |
| `LOAD_U32`         | Load 32-bit value |
| `LOAD_F32`         | Load 32-bit float value |
| `LOAD_F64`         | Load 64-bit float value |
| `LOAD_STR_0`       | Load null-terminated string |
| `LOAD_STR_LEN`     | Load string with length prefix (e.g., uint16 length + bytes) |
| `SET_ENDIANNESS`   | Set byte order |
| `VALIDATE_RANGE`   | Check value bounds |
| `APPLY_SCALE`      | Multiply value before encoding or after decoding |
| `MARK_OPTIONAL`    | Skip if not present |
| `MATCH_VALUE`      | Ensure field matches expected value |
| `CHECK_CRC16`      | Validate CRC field |
| `READ_REST`        | Read remaining bytes into a field |
| `ENTER_STRUCT`     | Begin processing a nested struct |
| `EXIT_STRUCT`      | End processing a nested struct |
| `BEGIN_ARRAY`      | Begin processing an array |
| `BEGIN_ARRAY_LEN`  | Begin processing an array with length prefix |
| `END_ARRAY`        | End processing an array |

---

## 5. Binary IL Format (Conceptual)

The compiled IL file is designed to be mapped directly into memory. It consists of a Header, a String Table (for field names), and the Bytecode.

### File Structure

```
[Header]
  - Magic: "CMDIL"
  - Version: 1
  - String Table Offset
  - Bytecode Offset

[String Table]
  - "command_id\0"
  - "target_position\0"
  - "x\0"
  - ...

**Optimization:** Common field names (like "x", "y", "z", "header") are stored once in the string table and referenced by multiple instructions. This deduplication keeps the binary size extremely small.

[Bytecode]
  - [Opcode] [Field_Name_Index] [Args...]
```

### Instruction Encoding

Each instruction includes an index into the String Table, allowing the VM to associate data with field names for JSON output or debugging.

```
[opcode (1 byte)] [name_idx (2 bytes)] [flags (1 byte)] [args...]
```

Example:

```
0x01 0x00 0x00 0x00 0x10 0x01  // PUSH_CONST_U16 (name_idx=0 "command_id", val=0x1001)
0x02 0x00 0x05 0x01            // LOAD_U8 (name_idx=5 "mode", little endian)
```

This allows the VM to be **self-describing**. The flight software can ignore the `name_idx` for speed, while the ground software uses it to generate rich JSON.

---

## 6. TDD Plan

### Test Suite 1: DSL Parsing

- [ ] Parse basic types (`uint8`, `uint16`, `string`)
- [ ] Parse floating point types (`float`, `double`)
- [ ] Parse struct definitions and nested usage
- [ ] Parse array definitions (`type name[n]`)
- [ ] Parse decorators (`@const`, `@range`, `@count`, etc.)
- [ ] Parse optional fields
- [ ] Parse conditional fields
- [ ] Parse telemetry-specific constructs (`@match`, `@crc16`, etc.)

---

### Test Suite 2: IL Compilation

- [ ] Compile constant fields to `PUSH_CONST`
- [ ] Compile decorated fields to IL with metadata
- [ ] Compile struct references to nested IL sequences
- [ ] Compile array definitions to loop or unrolled IL
- [ ] Compress IL into binary format
- [ ] Validate field ordering and offsets
- [ ] Compile telemetry packet schemas into IL for parsing

---

### Test Suite 3: VM Execution (Command Encoding)

- [ ] Load IL and bind inputs
- [ ] Execute IL to produce correct binary
- [ ] Handle nested structs and arrays
- [ ] Handle floating point serialization (IEEE 754)
- [ ] Apply decorators (endianness, scaling, etc.)
- [ ] Skip optional fields if not present
- [ ] Validate range and type constraints

---

### Test Suite 4: VM Execution (Telemetry Decoding)

- [ ] Load IL and parse binary telemetry into structured output
- [ ] Apply decorators (endianness, scaling, etc.)
- [ ] Handle null-terminated strings and variable-length fields
- [ ] Parse arrays and nested structs
- [ ] Decode floating point values
- [ ] Validate CRCs or checksums if defined
- [ ] Support conditional parsing based on packet type or header fields
- [ ] Handle bitfields and packed structures
- [ ] Parse nested or grouped fields

---

### Test Suite 5: Integration

- [ ] Encode full command from JSON input and validate binary output
- [ ] Decode full telemetry packet from binary and validate structured output
- [ ] Handle schema versioning and hot reload
- [ ] Validate caching behavior for parsed telemetry
- [ ] Invalidate cache on sequence gaps or schema changes
- [ ] Support multiple spacecraft streams with isolated state

---

## 7. CLI Tooling (Optional)

```bash
# Compile schema to IL
cmdc compile setmode.cmd --out setmode.cmdil

# Encode command from JSON input
cmdvm encode setmode.cmdil --input setmode.json --out packet.bin

# Decode telemetry from binary
cmdvm decode statuspacket.tlmil --input packet.bin --out parsed.json

# Validate input against schema
cmdvm validate setmode.cmdil --input setmode.json
```

---

## 8. Output Examples

### Command Input JSON

```json
{
  "target_position": { "x": 1.0, "y": 2.0, "z": 3.0 },
  "quaternions": [0.0, 0.0, 0.0, 1.0],
  "duration": 5.0,
  "comment": "Nav"
}
```

### Command Output Binary (hex)

```
10 01                    // command_id (0x1001)
00 00 80 3F              // x = 1.0 (little endian float)
00 00 00 40              // y = 2.0
00 00 40 40              // z = 3.0
00 00 00 00              // q[0] = 0.0
00 00 00 00              // q[1] = 0.0
00 00 00 00              // q[2] = 0.0
00 00 80 3F              // q[3] = 1.0
00 00 00 32              // duration (5.0 / 0.1 = 50 -> 0x32)
4E 61 76 00              // "Nav\0"
```

---

### Telemetry Input Binary (hex)

```
42                       // packet_type
0F A0                    // voltage_mv (4000 mV)
C3 F5 48 40              // temperature (3.14 float, little endian: 0x4048F5C3)
00 00 80 3F              // vel.x = 1.0
00 00 00 00              // vel.y = 0.0
00 00 00 00              // vel.z = 0.0
01 02 03                 // sensor_status[3]
4F 4B 00                 // "OK\0"
12 34                    // checksum
```

### Telemetry Output JSON

```json
{
  "packet_type": 66,
  "voltage_mv": 4000,
  "temperature": 3.14,
  "velocity": { "x": 1.0, "y": 0.0, "z": 0.0 },
  "sensor_status": [1, 2, 3],
  "status": "OK",
  "checksum": 4660
}
```

---

## 9. Data Binding Strategy

To ensure robustness and ease of use, the system employs a **Flexible Binding** strategy for inputs:

1.  **Flexible Input (Map -> Binary)**:
    *   The VM interface accepts a generic **Key-Value Map** (e.g., `HashMap<String, Value>`) rather than a raw JSON string.
    *   This decouples the VM from specific input formats; any source (JSON, YAML, GUI form, Database) that can produce a map is valid.
    *   The VM iterates through the **IL instructions** (the source of truth for wire order).
    *   For each instruction (e.g., `LOAD_U8 "mode"`), it requests the value from the provided Map.
    *   **Result**: The binary output is always perfectly ordered and valid, regardless of the input map's internal order (e.g., `{"duration": 5, "mode": 1}`).

2.  **Deterministic Output (Binary -> Map/JSON)**:
    *   When decoding, the VM generates JSON fields in the exact order defined by the schema.
    *   This ensures that the output is predictable and easy to diff.

---

## 10. External Integration & Workflow

This architecture supports data exchange with external partners (e.g., other spacecraft vendors):

1.  **Schema Exchange**: Partners provide a `.il` file defining their packet structure.
2.  **Compilation**: The system compiles this schema into IL.
3.  **Ingestion**: The VM uses the compiled IL to immediately decode binary telemetry from the partner's spacecraft without code changes.

This allows the system to act as a universal ground station decoder, decoupling the ground software from specific spacecraft implementations.

## 11. Performance & Overhead

The IL VM is designed to function almost like a software-based DMA engine, prioritizing throughput and deterministic latency.

| Feature | Implementation Strategy |
|---------|-------------------------|
| **Zero Allocation** | The VM does not allocate memory during execution. It reads from a source buffer and writes directly to a pre-allocated destination struct or buffer. |
| **Linear Execution** | IL is executed sequentially without complex branching or recursion, ensuring predictable CPU usage (O(n) relative to packet size). |
| **Minimal Dispatch** | The VM loop uses a tight "fetch-decode-execute" cycle, often fitting entirely within the CPU L1 instruction cache. |
| **Bulk Operations** | For simple fields (e.g., byte arrays, strings), the VM uses `memcpy` equivalents rather than byte-by-byte processing. |

### Sizing & Buffer Strategy

To maintain the zero-allocation promise, the VM handles variable-length fields (like strings) using a hybrid approach:

1.  **Pre-calculated Size**: For fixed schemas (structs, primitives), the total packet size is known at compile time.
2.  **Discovered Size**: For variable fields (e.g., `string until 0x00`), the VM scans the input to determine the size dynamically.
    *   **Safety**: The `max` keyword (e.g., `until 0x00 max 64`) enforces a hard limit during scanning to prevent buffer overflows or infinite loops if the terminator is missing.
    *   **Decoding**: The VM returns a **view** (pointer + length) into the raw binary buffer (Zero-Copy) rather than allocating a new string object.
    *   **Encoding**: The VM can perform a fast "dry run" to calculate the required buffer size, or write to a fixed-size output buffer and return the actual length used.

### VM Footprint

The VM is designed to be extremely lightweight, suitable for embedded environments:

*   **Code Size**: The core VM loop and instruction handlers should compile to **< 4KB** of machine code.
*   **Stack Usage**: Minimal stack depth (no recursion), typically **< 512 bytes**.
*   **Heap Usage**: **0 bytes** (unless the host application specifically requests dynamic output structures).

While not as fast as a raw C struct cast (which has zero CPU overhead but is unsafe and inflexible), this approach is orders of magnitude faster than parsing text formats like JSON or XML and comparable to compiled Protobuf, but with dynamic schema loading.

## 12. VM Architecture: Stateless & Hot-Reloadable

The VM is architected as a **stateless execution engine**, separating the *logic* (IL bytecode) from the *processor* (VM loop).

| Concept | Description |
|---------|-------------|
| **The Engine** | A compiled C/C++ function containing a large `switch` statement over the opcodes. It is immutable and compiled once. |
| **The Logic** | The IL bytecode is treated as data. It can be loaded from disk, received over the network, or swapped at runtime. |
| **Hot Reload** | To update a schema, the host application simply passes a pointer to the new IL buffer on the next call to `vm_execute()`. No VM restart or software re-flash is required. |

This design allows the ground station or spacecraft to switch packet formats instantly without downtime, effectively acting as a **programmable state machine** defined entirely by the loaded IL.

## 13. Symmetric Architecture (Ground & Flight)

A key advantage of this IL-based approach is that the **same VM code and same IL schemas** run on both the ground and the spacecraft.

| Location | Role | Operation |
| :--- | :--- | :--- |
| **Ground Station** | **Encoder** | JSON Input + Command Schema -> Binary Command |
| **Spacecraft** | **Decoder** | Binary Command + Command Schema -> C Struct / Action |
| **Spacecraft** | **Encoder** | C Struct / Sensor Data + Telemetry Schema -> Binary Telemetry |
| **Ground Station** | **Decoder** | Binary Telemetry + Telemetry Schema -> JSON Output |

This **Isomorphic Design** eliminates the common source of errors where the ground parser (e.g., Python) and flight parser (e.g., C) disagree on how to handle a specific field. If it works on the ground simulator, it is guaranteed to work in orbit because the engine is identical.

## 14. Future Extensions

- Support for telemetry stream multiplexing
- Command chaining and macros
- Field-level encryption or CRC decorators
- WASM-based VM for browser-based tools
- Schema introspection and documentation generation
- Live schema hot-reloading for ground systems
