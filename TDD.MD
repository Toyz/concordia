# **Concordia Protocol: Specification & TDD**

## **1. The Instruction Set Architecture (ISA)**

The Concordia IL is a stream of bytes designed for the "hostile environment" of space or embedded systems. The first byte is the OPCODE. Following bytes depend on the instruction arguments.

The VM maintains a **Context** consisting of:

- Cursor: Current position in the binary buffer.

- BitOffset: Current bit position (0-7) for bitfield operations.

- Endianness: Current byte order mode.

- Map: The input/output key-value store.

- PacketVersion: Metadata provided by the Host (e.g. from CCSDS Header).


### **Category A: Meta & State**

|   |
| - |

|                 |         |          |                                                                                         |
| --------------- | ------- | -------- | --------------------------------------------------------------------------------------- |
| **Opcode**      | **Hex** | **Args** | **Description**                                                                         |
| NOOP            | 0x00    | None     | No operation. Used for alignment.                                                       |
| SET\_ENDIAN\_LE | 0x01    | None     | Switch to Little Endian (Intel/ARM).                                                    |
| SET\_ENDIAN\_BE | 0x02    | None     | Switch to Big Endian (Network/PowerPC).                                                 |
| ENTER\_STRUCT   | 0x03    | None     | Push scope for nested maps.                                                             |
| EXIT\_STRUCT    | 0x04    | None     | Pop current scope.                                                                      |
| META\_VERSION   | 0x05    | u8       | **Logging:** Writes Arg to the output Map as \_version. Does **NOT** validate or abort. |


### **Category B: Primitives (Byte Aligned)**

_Implies byte-alignment check. If BitOffset != 0, advance to next byte._

|            |         |             |                             |
| ---------- | ------- | ----------- | --------------------------- |
| **Opcode** | **Hex** | **Args**    | **Description**             |
| IO\_U8     | 0x10    | u16 (KeyID) | Read/Write uint8.           |
| IO\_U16    | 0x11    | u16 (KeyID) | Read/Write uint16.          |
| IO\_U32    | 0x12    | u16 (KeyID) | Read/Write uint32.          |
| IO\_U64    | 0x13    | u16 (KeyID) | Read/Write uint64.          |
| IO\_I8     | 0x14    | u16 (KeyID) | Read/Write int8.            |
| IO\_I16    | 0x15    | u16 (KeyID) | Read/Write int16.           |
| IO\_I32    | 0x16    | u16 (KeyID) | Read/Write int32.           |
| IO\_I64    | 0x17    | u16 (KeyID) | Read/Write int64.           |
| IO\_F32    | 0x18    | u16 (KeyID) | Read/Write Float (32-bit).  |
| IO\_F64    | 0x19    | u16 (KeyID) | Read/Write Double (64-bit). |


### **Category C: Bitfields & Padding**

_Operates on BitOffset._

|               |         |                        |                               |
| ------------- | ------- | ---------------------- | ----------------------------- |
| **Opcode**    | **Hex** | **Args**               | **Description**               |
| IO\_BIT\_U    | 0x20    | u16 (KeyID), u8 (Bits) | Read/Write N bits (Unsigned). |
| IO\_BIT\_I    | 0x21    | u16 (KeyID), u8 (Bits) | Read/Write N bits (Signed).   |
| IO\_BIT\_BOOL | 0x22    | u16 (KeyID)            | Read/Write 1 bit as Bool.     |
| ALIGN\_PAD    | 0x23    | u8 (Bits)              | Skip N bits (Explicit @pad).  |
| ALIGN\_BYTE   | 0x24    | None                   | Force alignment to next byte. |


### **Category D: Arrays & Strings (Variable Logic)**

_Supports your prefix syntax._

|               |         |                        |                                |
| ------------- | ------- | ---------------------- | ------------------------------ |
| **Opcode**    | **Hex** | **Args**               | **Description**                |
| STR\_NULL     | 0x30    | u16 (KeyID), u16 (Max) | C-String (null terminated).    |
| STR\_PRE\_U8  | 0x31    | u16 (KeyID)            | String with u8 length prefix.  |
| STR\_PRE\_U16 | 0x32    | u16 (KeyID)            | String with u16 length prefix. |
| STR\_PRE\_U32 | 0x33    | u16 (KeyID)            | String with u32 length prefix. |
| ARR\_FIXED    | 0x34    | u16 (Count)            | Loop fixed N times (@count).   |
| ARR\_PRE\_U8  | 0x35    | u16 (KeyID)            | Loop N times (prefix u8).      |
| ARR\_PRE\_U16 | 0x36    | u16 (KeyID)            | Loop N times (prefix u16).     |
| ARR\_PRE\_U32 | 0x37    | u16 (KeyID)            | Loop N times (prefix u32).     |
| ARR\_END      | 0x38    | None                   | End loop block.                |
| RAW\_BYTES    | 0x39    | u16 (KeyID), u16 (Len) | Fixed blob (e.g., hash).       |


### **Category E: Validation & Integrity**

|              |         |                        |                                      |
| ------------ | ------- | ---------------------- | ------------------------------------ |
| **Opcode**   | **Hex** | **Args**               | **Description**                      |
| CONST\_CHECK | 0x40    | u8 (Type), ... (Val)   | **Telemetry:** Assert value matches. |
| CONST\_WRITE | 0x41    | u8 (Type), ... (Val)   | **Command:** Write constant value.   |
| RANGE\_CHECK | 0x42    | u8 (Type), Min, Max    | Validation (@range).                 |
| SCALE\_LIN   | 0x43    | f32 (Mult), f32 (Add)  | Scaling (@scale).                    |
| CRC\_16      | 0x44    | u16 (Poly), u16 (Init) | Checksum (@crc16).                   |


### **Category F: Control Flow (Conditional)**

_Supports your @depends\_on logic._

|               |         |                           |                                                          |
| ------------- | ------- | ------------------------- | -------------------------------------------------------- |
| **Opcode**    | **Hex** | **Args**                  | **Description**                                          |
| JUMP\_IF\_NOT | 0x50    | u16 (KeyID), u16 (Offset) | Skip next Offset bytes of **IL** if KeyID is false/zero. |


## **2. The Concordia DSL (.cnd)**

The compiler strictly maps DSL keywords to specific IL opcodes to ensure deterministic sizing.


### **A. Primitive Type Mapping**

|             |           |                 |               |
| ----------- | --------- | --------------- | ------------- |
| **Keyword** | **Alias** | **Size (Bits)** | **IL Opcode** |
| uint8       | byte, u8  | 8               | IO\_U8        |
| uint16      | u16       | 16              | IO\_U16       |
| uint32      | u32       | 32              | IO\_U32       |
| uint64      | u64       | 64              | IO\_U64       |
| int8        | i8        | 8               | IO\_I8        |
| int16       | i16       | 16              | IO\_I16       |
| int32       | i32       | 32              | IO\_I32       |
| int64       | i64       | 64              | IO\_I64       |
| float       | f32       | 32              | IO\_F32       |
| double      | f64       | 64              | IO\_F64       |


### **B. Basic Structures**

// Reusable struct definition\
struct Vector3 {\
    float x;    // Maps to IO\_F32\
    float y;    // Maps to IO\_F32\
    float z;    // Maps to IO\_F32\
}\
\
// Top-level command definition\
command SetNavigation {\
    @const(0x1001)      // Maps to CONST\_WRITE\
    uint16 command\_id;\
\
    @le                 // Maps to SET\_ENDIAN\_LE\
    Vector3 target;     // Maps to ENTER\_STRUCT -> Ops -> EXIT\_STRUCT\
   \
    double precise\_time; // Maps to IO\_F64\
}


### **C. Advanced Arrays & Strings (Prefixes)**

Direct mapping to Category D opcodes.

telemetry ImageDownload {\
    uint16 image\_id;\
\
    // "prefix uint32" -> Maps to ARR\_PRE\_U32\
    // The VM reads a u32, then loops that many times reading 'byte'\
    byte image\_data\[] prefix uint32;\
\
    // "prefix uint8" -> Maps to ARR\_PRE\_U8\
    // The VM reads a u8, then loops that many times reading 'float'\
    float histogram\[] prefix uint8;\
   \
    // "until 0x00" -> Maps to STR\_NULL\
    string comment until 0x00 max 64;\
}


### **D. Bitfields & Padding**

Direct mapping to Category C opcodes.

struct Flags {\
    // Colon syntax -> Maps to IO\_BIT\_U / IO\_BIT\_BOOL\
    uint8 enable\_logging : 1;\
    uint8 enable\_radio   : 1;\
    uint8 power\_mode     : 2;\
\
    // Explicit padding -> Maps to ALIGN\_PAD\
    @pad(4)\
    uint8 \_reserved : 4;\
}


### **E. Conditional Logic**

Using @depends\_on generates JUMP\_IF\_NOT instructions in the IL.

telemetry SensorPacket {\
    uint8 has\_temp\_data;\
   \
    // Only parse 'temperature' if 'has\_temp\_data' is true\
    @depends\_on(has\_temp\_data)\
    float temperature;\
}


### **F. Framing & Custom Encoding (The "Wire" Layer)**

Users can define **Custom Encodings** via decorators. These do not generate IL Opcodes. Instead, they generate metadata for the Host Driver.

This separation enables **Middleware Injection**:

- **Chaining:** Wrap a payload in Encryption -> Then COBS -> Then TCP.

- **Fuzzing:** Inject "Bad Frame" logic to test receiver robustness.

- **Transport Agnosticism:** Send the same IL payload over UART (COBS) or Ethernet (UDP) by swapping the framer.

**Syntax:** @frame(encoding = "name", ...args)

// Example 1: Standard COBS Encoding\
@frame(encoding = "cobs", delimiter = 0x00)\
packet TelemetryStandard {\
    u16 voltage;\
    u16 current;\
}\
\
// Example 2: Custom User Encoding (e.g., Encryption or proprietary compression)\
// The Host Runtime must have a registered callback for "my\_crypto".\
@frame(encoding = "my\_crypto", key\_id = 1)\
packet SecureCommand {\
    u8 opcode;\
    u64 timestamp;\
}

**Compiler Output (Metadata):** When compiling SecureCommand, the compiler outputs the IL for the payload, plus a Manifest entry: { "packet": "SecureCommand", "post\_process": "my\_crypto", "args": { "key\_id": 1 } }


### **G. Global & Packet Metadata**

You can define high-level metadata using "free-floating" decorators.

**Syntax:** @version(major) or @id(val)

- **@version(N):** Compiles to META\_VERSION (0x05) with arg N. This is purely informational.

- **@id(N):** Sets the Packet ID (header metadata).

// --- Top of File ---\
// Applies to all packets below unless overridden\
@version(2)\
\
// --- Packet Definition ---\
packet Status {\
    // VM executes: META\_VERSION 2\
    // VM output Map will contain: { "\_version": 2, "voltage": ... }\
    u16 voltage;\
}


## **3. Theory Validation (TDD Plan)**

### **Suite 1: The ALU (Primitives & Endianness)**

1. **Instruction:** SET\_ENDIAN\_BE, IO\_U16

- **Theory:** Input { "val": 0x1234 } in BE mode.

- **Expect:** Binary \[0x12, 0x34].

2. **Instruction:** SET\_ENDIAN\_LE, IO\_U16

- **Theory:** Input { "val": 0x1234 } in LE mode.

- **Expect:** Binary \[0x34, 0x12].


### **Suite 2: The Scalpel (Bitfields & Padding)**

1. **Instruction:** IO\_BIT\_U(1), IO\_BIT\_U(1), ALIGN\_PAD(6)

- **Theory:** Input { "a": 1, "b": 1 }.

- **Logic:** 1 (bit 0) + 1 (bit 1) + 000000 (pad).

- **Expect:** Output 0x03 (or 0xC0 depending on bit order convention defined in impl).


### **Suite 3: Memory Safety (Prefixes & Limits)**

1. **Instruction:** ARR\_PRE\_U32 (Variable Array)

- **Theory:** Binary provides length 0xFFFFFFFF.

- **Expect:** VM checks RemainingBytes < (0xFFFFFFFF \* ElementSize) and returns **OOB Error** immediately. It does NOT attempt to allocate.

2. **Instruction:** STR\_NULL (Max 64)

- **Theory:** Input string is 70 bytes.

- **Expect:** Encoder truncates to 63 bytes + null terminator.


### **Suite 4: Logic (Consts & Conditionals)**

1. **Instruction:** CONST\_CHECK (Telemetry)

- **Theory:** Schema expects 0xCAFE. Binary has 0xBABE.

- **Expect:** Decoder returns **Validation Error**.

2. **Instruction:** JUMP\_IF\_NOT (Conditional)

- **Theory:** flag = 0. Next instruction is IO\_F32.

- **Expect:** VM skips the IO\_F32 instruction entirely.

3. **Instruction:** META\_VERSION (Global Metadata)

- **Theory:** DSL has @version(2).

- **Expect:** VM Output Map contains key \_version with value 2. **No Error** occurs regardless of input.


### **Suite 5: The "1:1 Symmetry" Guarantee**

1. **Round Trip:**

- Map -> Encode(LE) -> Binary -> Decode(LE) -> Map

- **Assert:** Input Map equals Output Map.

2. **Cross-Arch:**

- Encode on Little Endian host.

- Decode on Big Endian host.

- **Assert:** Result is identical.


## **4. The Polyglot Ecosystem**

The Concordia IL format is the "Universal Contract." The implementations can vary by target.


### **A. The Compiler (Rust/Go)**

- **Role:** Transforms human-readable DSL (.cnd) into optimized Binary IL.

- **Safety:** Performs static analysis (infinite loop detection, struct size calculation).

- **Output:** Generates .il binary files AND .h or .ts type definitions for IDE autocompletion.


### **B. The Flight Runtime (C/C++)**

- **Target:** Embedded Systems (ARM Cortex-M, AVR, RISC-V).

- **Constraints:** No malloc, minimal stack usage.

- **Implementation:** A tight switch loop. Uses a void\* context to bind fields to struct members via offset mapping.


### **C. The Ground Runtime (Rust/Python)**

- **Target:** Backend Services, Data Analysis.

- **Constraints:** High throughput.

- **Implementation:** Optimized for converting Binary Telemetry directly to JSON/Parquet for storage.


### **D. The Web Runtime (WASM/TypeScript)**

- **Target:** Browser-based Mission Control (React/Angular).

- **Constraints:** Bundle size.

- **Implementation:** A pure TS or WASM parser that reads binary packets over WebSockets and hydrates UI components instantly.


## **5. Binary File Format (.il)**

The compiled .il file is a flat binary designed for direct memory mapping (mmap) or simple read(). It is optimized for zero-copy access.


### **A. File Structure**

+----------------+  <-- 0x00\
\|     Header     |  Fixed Size (16 Bytes)\
+----------------+  <-- 0x10\
|  String Table  |  Variable Size (List of Null-Terminated Strings)\
+----------------+  <-- Header.bytecode\_offset\
|    Bytecode    |  Variable Size (Instruction Stream)\
+----------------+


### **B. Header Layout (16 Bytes)**

|            |          |                                             |
| ---------- | -------- | ------------------------------------------- |
| **Offset** | **Type** | **Description**                             |
| 0x00       | u8\[5]   | Magic Signature "CNDIL" (ASCII)             |
| 0x05       | u8       | Version (Current: 0x01)                     |
| 0x06       | u16      | String Table Count (Number of entries)      |
| 0x08       | u32      | String Table Offset (Bytes from file start) |
| 0x0C       | u32      | Bytecode Offset (Bytes from file start)     |


### **C. String Table**

To minimize size and allow generic JSON output, field names are stored once. The KeyID argument in Opcodes (like IO\_U8) refers to the index in this table.

- **Format:** A continuous sequence of Null-Terminated strings.

- **Indexing:** Index 0 is the first string, Index 1 is the second, etc. The VM loads these into a lightweight lookup table (or points to them directly) at startup.


### **D. Instruction Encoding**

Instructions are tightly packed variable-length sequences. The OPCODE determines the length.

**General Format:** \[OPCODE] \[ARGS...]

**Examples:**

- NOOP (0 Args): 0x00

- IO\_U8 (1 Arg: u16 KeyID): 0x10 0x05 0x00 (Opcode 0x10, KeyID 5)

- RANGE\_CHECK (3 Args): 0x42 0x01 0x00 0x64 (Opcode 0x42, Type U8, Min 0, Max 100)


## **6. Host Integration (The Dispatcher)**

The VM is purely a "Structure Parser." It does not know _which_ structure to parse until you tell it. This section explains the **Dispatcher Loop** (the code you write in C++ to drive the VM).


### **Step 1: Framing (The Physical Layer)**

The Host receives raw bytes from the wire (UART/TCP). It applies the framing logic defined by @frame (e.g., COBS decode) to get a clean **Packet Payload**.


### **Step 2: Identification (The Dispatch Layer)**

The Host must look at the first few bytes of the Payload (the Header) to find the **Packet ID** (Command ID).

- _Note:_ This ID matches the @id(N) decorator in your DSL.


### **Step 3: Execution (The VM Layer)**

The Host uses the Packet ID to select the correct .il schema from memory.

// Pseudocode for C++ Host\
void OnDataReceived(uint8\_t\* wire\_data, size\_t wire\_len) {\
    // 1. DEFRAME (COBS, etc.)\
    uint8\_t payload\[1024];\
    size\_t payload\_len = cobs\_decode(wire\_data, wire\_len, payload);\
\
    // 2. IDENTIFY\
    // Assume first byte is always Packet ID (Application Opcode)\
    uint8\_t packet\_id = payload\[0];\
\
    // 3. LOOKUP IL\
    // The registry maps "Packet ID 0x10" -> "SetNavigation.il"\
    const uint8\_t\* il\_bytecode = IL\_Registry\[packet\_id];\
\
    // 4. EXECUTE VM\
    // Run the VM to parse the REST of the payload\
    VM\_Context ctx;\
    ctx.cursor = 1; // Skip the ID byte we just read\
    vm\_execute(il\_bytecode, payload, payload\_len, \&ctx);\
}
