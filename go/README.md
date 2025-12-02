# Concordia Go Wrapper

This directory contains the Go bindings for the Concordia VM.

## Usage

```go
package main

import (
    "fmt"
    "unsafe"
    "github.com/Toyz/concordia/go"
)

func main() {
    bytecode := []byte{ ... } // Your compiled bytecode
    data := []byte{ ... }     // Data to process

    err := concordia.Execute(bytecode, data, concordia.ModeDecode, func(mode concordia.Mode, keyID uint16, typeOp uint8, val unsafe.Pointer) error {
        // Handle IO callbacks
        fmt.Printf("IO Callback: Key=%d Type=%d\n", keyID, typeOp)
        return nil
    })

    if err != nil {
        panic(err)
    }
}
```

## Building

The package uses CGO to compile the Concordia C implementation directly.
Ensure you have a C compiler installed (gcc or clang).

```bash
go build ./go
```
