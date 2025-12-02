# Concordia Go Demo

This folder demonstrates how to use the Concordia VM from Go using `cgo`.

## Prerequisites

*   Go installed
*   GCC installed (for cgo)
*   Concordia CLI (`cnd`) built and in your path (or use the one in `../../build/Debug/cnd.exe`)

## How it works

The `main.go` file uses `cgo` to include the Concordia C headers and source files directly. This allows it to statically link the VM without needing a separate library build step for this demo.

It defines a Go struct `KitchenSink` and a callback function that maps the VM's requests to the Go struct fields.

## Usage

1.  **Compile the Schema**:
    You need the `kitchen_sink.il` file. You can compile the one from the `examples/kitchen_sink` directory.

    ```bash
    # From repo root
    ./build/src/cli/cnd compile examples/kitchen_sink/kitchen_sink.cnd kitchen_sink.il
    ```

2.  **Run the Demo**:
    ```bash
    cd demos/go
    go run main.go ../../kitchen_sink.il
    ```

2.  **Run the Demo**:

    ```bash
    go run main.go ../telemetry.il
    ```

## Output

You should see output indicating that data was encoded into bytes and then decoded back into a Go struct.
