# Concordia WASM Demo

This folder demonstrates how to bridge Concordia to WebAssembly (WASM) for use in a browser.

## Architecture

1.  **C Bridge (`wasm_bridge.c`)**:
    *   Exposes `init_vm` to load the IL (schema).
    *   Exposes `decode_packet` to process binary telemetry.
    *   Implements `wasm_io_callback` which calls out to JavaScript `js_on_field`.

2.  **JavaScript Side**:
    *   Loads the WASM module.
    *   Implements `js_on_field(key, type, ptr)`.
    *   Uses `Module.HEAP*` to read the value from `ptr` based on `type`.
    *   Constructs a JS Object from the fields.

## Compilation

To compile this with Emscripten:

```bash
emcc wasm_bridge.c ../../src/vm/vm_exec.c ../../src/vm/vm_io.c \
  -I ../../include \
  -s EXPORTED_FUNCTIONS="['_init_vm', '_decode_packet', '_alloc_buffer', '_free_buffer']" \
  -s EXPORTED_RUNTIME_METHODS="['ccall', 'cwrap']" \
  -o concordia.js
```

## Usage (JavaScript)

```javascript
// 1. Load Schema
const ilData = await fetch('telemetry.il').then(r => r.arrayBuffer());
const ilPtr = Module._alloc_buffer(ilData.byteLength);
Module.HEAPU8.set(new Uint8Array(ilData), ilPtr);
Module._init_vm(ilPtr, ilData.byteLength);

// 2. Define Callback
Module['js_on_field'] = function(key, type, ptr) {
    let value;
    if (type === 0x10) value = Module.HEAPU8[ptr];       // U8
    else if (type === 0x12) value = Module.HEAPU32[ptr>>2]; // U32
    else if (type === 0x18) value = Module.HEAPF32[ptr>>2]; // F32
    // ... handle other types
    
    console.log(`Field ${key}: ${value}`);
};

// 3. Decode Packet (from Websocket)
socket.onmessage = (event) => {
    const data = new Uint8Array(event.data);
    const buf = Module._alloc_buffer(data.length);
    Module.HEAPU8.set(data, buf);
    Module._decode_packet(buf, data.length);
    Module._free_buffer(buf);
};
```
