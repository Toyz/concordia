package main

/*
#cgo CFLAGS: -I../../include -I../../src/vm
#include <stdlib.h>
#include <string.h>
#include "concordia.h"

// Gateway function defined in concordia_impl.c
extern cnd_error_t c_gateway(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr);
*/
import "C"
import (
	"fmt"
	"io/ioutil"
	"os"
	"unsafe"
    "encoding/binary"
    "runtime/cgo"
)

// TelemetryData matches the schema
type TelemetryData struct {
	DeviceID     uint32
	Temperature  float32
	BatteryLevel uint8
	Status       uint8
}

//export go_io_callback
func go_io_callback(userPtr unsafe.Pointer, mode C.int, keyID C.uint16_t, typeOp C.uint8_t, dataPtr unsafe.Pointer) C.int {
    // userPtr is the cgo.Handle cast to void*
    handle := cgo.Handle(userPtr)
	data := handle.Value().(*TelemetryData)
    isEncode := (mode == C.CND_MODE_ENCODE)

	switch keyID {
	case 0: // device_id (uint32)
        if isEncode {
            *(*C.uint32_t)(dataPtr) = C.uint32_t(data.DeviceID)
        } else {
            data.DeviceID = uint32(*(*C.uint32_t)(dataPtr))
        }
	case 1: // temperature (float)
        if isEncode {
            *(*C.float)(dataPtr) = C.float(data.Temperature)
        } else {
            data.Temperature = float32(*(*C.float)(dataPtr))
        }
	case 2: // battery_level (uint8)
        if isEncode {
            *(*C.uint8_t)(dataPtr) = C.uint8_t(data.BatteryLevel)
        } else {
            data.BatteryLevel = uint8(*(*C.uint8_t)(dataPtr))
        }
	case 3: // status (uint8)
        if isEncode {
            *(*C.uint8_t)(dataPtr) = C.uint8_t(data.Status)
        } else {
            data.Status = uint8(*(*C.uint8_t)(dataPtr))
        }
	default:
		return C.CND_ERR_INVALID_OP
	}
	return C.CND_ERR_OK
}

func main() {
	if len(os.Args) < 2 {
		fmt.Println("Usage: go run main.go <path_to_telemetry.il>")
		return
	}

	ilPath := os.Args[1]
	ilData, err := ioutil.ReadFile(ilPath)
	if err != nil {
		fmt.Printf("Failed to read IL file: %v\n", err)
		return
	}

    if len(ilData) < 16 {
        fmt.Println("Invalid IL file")
        return
    }

    // Parse Header to find bytecode offset
    // Header: Magic(5) Ver(1) StrCount(2) StrOffset(4) BytecodeOffset(4)
    bytecodeOffset := binary.LittleEndian.Uint32(ilData[12:16])

    // Allocate program struct in C memory to avoid Go pointer rules
    cProgram := (*C.cnd_program)(C.malloc(C.size_t(unsafe.Sizeof(C.cnd_program{}))))
    defer C.free(unsafe.Pointer(cProgram))
    
    cIlData := C.CBytes(ilData)
    defer C.free(cIlData)

    // Pointer arithmetic in Go/CGO
    // cIlData is void*. We need to cast to uint8_t* and add offset.
    bytecodePtr := (*C.uint8_t)(unsafe.Pointer(uintptr(cIlData) + uintptr(bytecodeOffset)))
    bytecodeLen := C.size_t(len(ilData) - int(bytecodeOffset))

	C.cnd_program_load(cProgram, bytecodePtr, bytecodeLen)

	// --- ENCODE ---
	fmt.Println("--- Go Encoding ---")
	myData := TelemetryData{
		DeviceID:     0x600DCAFE,
		Temperature:  42.0,
		BatteryLevel: 99,
		Status:       1,
	}

    // Allocate C buffer for the VM to write to
    cBuffer := C.malloc(128)
    defer C.free(cBuffer)
    C.memset(cBuffer, 0, 128)

    // Allocate ctx in C memory
    cCtx := (*C.cnd_vm_ctx)(C.malloc(C.size_t(unsafe.Sizeof(C.cnd_vm_ctx{}))))
    defer C.free(unsafe.Pointer(cCtx))
    
    // Use cgo.Handle to safely pass Go object to C
    handle := cgo.NewHandle(&myData)
    defer handle.Delete()
    
	C.cnd_init(cCtx, C.CND_MODE_ENCODE, cProgram, (*C.uint8_t)(cBuffer), 128, (C.cnd_io_cb)(C.c_gateway), unsafe.Pointer(handle))

	errCode := C.cnd_execute(cCtx)
	if errCode != C.CND_ERR_OK {
		fmt.Printf("Encoding failed with error %d\n", errCode)
		return
	}

    cursor := int(cCtx.cursor)
	fmt.Printf("Encoded %d bytes: ", cursor)
    
    // Copy back to Go slice for printing/saving
    encodedBytes := C.GoBytes(cBuffer, C.int(cursor))
    for _, b := range encodedBytes {
        fmt.Printf("%02X ", b)
    }
    fmt.Println()

    // Save to file
    ioutil.WriteFile("telemetry_go.bin", encodedBytes, 0644)

	// --- DECODE ---
	fmt.Println("\n--- Go Decoding ---")
	var decodedData TelemetryData
    
    decodeHandle := cgo.NewHandle(&decodedData)
    defer decodeHandle.Delete()

    // Reset context
	C.cnd_init(cCtx, C.CND_MODE_DECODE, cProgram, (*C.uint8_t)(cBuffer), C.size_t(cursor), (C.cnd_io_cb)(C.c_gateway), unsafe.Pointer(decodeHandle))

	errCode = C.cnd_execute(cCtx)
	if errCode != C.CND_ERR_OK {
		fmt.Printf("Decoding failed with error %d\n", errCode)
		return
	}

	fmt.Printf("Device ID: 0x%X\n", decodedData.DeviceID)
	fmt.Printf("Temperature: %.1f C\n", decodedData.Temperature)
	fmt.Printf("Battery: %d%%\n", decodedData.BatteryLevel)
	fmt.Printf("Status: %d\n", decodedData.Status)
}
