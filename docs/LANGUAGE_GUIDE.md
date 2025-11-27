# Concordia Language Guide

Concordia is a schema language for defining binary data formats, primarily for telemetry and command packets. It compiles to a compact Intermediate Language (IL) that can be executed by a lightweight VM.

## 1. Basic Syntax

A Concordia file (`.cnd`) consists of `struct` and `packet` definitions.

### Comments
```cnd
// Single line comment
/* Block comment
   spanning multiple lines */
```

### Packets
A `packet` is the top-level unit of data. It represents a complete message.
**Note:** A `.cnd` file can contain only **one** `packet` definition.

```cnd
packet MyPacket {
    uint8 id;
    uint32 timestamp;
}
```

### Structs
A `struct` is a reusable group of fields.
```cnd
struct Point {
    float x;
    float y;
}

packet Path {
    Point start;
    Point end;
}
```

### Enums
An `enum` defines a set of named constants.
```cnd
// Default underlying type is uint32
enum Color {
    Red = 1,
    Green = 2,
    Blue = 3
}

// Explicit underlying type
enum Status : uint8 {
    Ok = 0,
    Error = 1
}

packet Telemetry {
    Color led_color;
    Status system_status;
}
```

## 2. Data Types

### Integers
*   `uint8`, `byte`, `u8`
*   `int8`, `i8`
*   `uint16`, `u16`, `int16`, `i16`
*   `uint32`, `u32`, `int32`, `i32`
*   `uint64`, `u64`, `int64`, `i64`

### Floating Point
*   `float`, `f32` (32-bit IEEE 754)
*   `double`, `f64` (64-bit IEEE 754)

### Strings
Strings are ASCII or UTF-8 sequences.
*   `string name max 32;` (Fixed max length, null-terminated)
*   `string name until 0x00;` (Read until null terminator)
*   `string name prefix uint8;` (Length prefixed)

### Arrays
*   **Fixed:** `uint8 data[10];`
*   **Prefixed:** `uint8 data[] prefix uint16;` (Length determined by prefix field)

### Bitfields
Pack multiple fields into a single integer type.
```cnd
struct Flags {
    uint8 enable : 1;
    uint8 mode : 3;
    uint8 reserved : 4;
}
```

## 3. Decorators

Decorators provide metadata and transformation logic.

### Validation & Constants
*   `@const(value)`: Field must match this value.
    *   **Note:** Constant fields are handled entirely by the VM. They are automatically written during encoding and verified during decoding. In **decode** mode, the value is reported to the callback (read-only).
*   `@range(min, max)`: Value must be within range.

### Transformation
*   `@scale(factor)`: Multiply raw value by factor (e.g., `raw * 0.1`).
*   `@offset(value)`: Add offset to raw value.
*   `@unit("string")`: Metadata for display units.

### Layout
*   `@big_endian`, `@little_endian`: Set byte order.
*   `@pad(bits)`: Insert padding bits.
*   `@fill`: Align to next byte boundary.

### Logic
*   `@optional`: Field may be missing at end of stream.

## 4. Imports

Split schemas into multiple files.
```cnd
@import("common/types.cnd")
```

## 5. Example

```cnd
packet Telemetry {
    @const(0xAA)
    uint8 sync;

    @big_endian
    uint16 id;

    @scale(0.01)
    @unit("volts")
    uint16 voltage;

    @optional
    string debug_msg max 64;
}
```

