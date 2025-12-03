package main

import (
	"fmt"
	"io/ioutil"
	"os"

	concordia "github.com/Toyz/concordia/go"
)

// Status Enum
type Status uint8

const (
	StatusOK   Status = 0
	StatusFail Status = 1
)

// Vec3 Struct
type Vec3 struct {
	X float32
	Y float32
	Z float32
}

// KitchenSink Struct
type KitchenSink struct {
	Magic     uint32
	FlagsA    uint16
	FlagB     bool
	ValC      int8
	Timestamp int64
	Position  Vec3
	Matrix    [4]uint8
	Points    []uint16
	Name      string
	Status    Status

	// Union fields
	Confidence uint8
	ErrorCode  uint16
	Reason     string

	Percentage  uint8
	Temperature float64 // Scaled

	ValAdd uint8
	ValSub uint8
	ValMul uint8
	ValDiv uint8

	Year uint16 // Proof: 2025 fits in uint8 via @add(1900)

	PolyVal   float64
	SplineVal float64
	ExprVal   uint8

	BitPacked struct {
		A3bits   uint8
		B5bits   uint8
		C4bits   uint8
		DAligned uint8
	}

	HasExtra  bool
	ExtraData string

	Checksum uint32

	AdvMode         uint8
	AdvSimpleVal    uint16
	AdvHasDetails   bool
	AdvDetails      string
	AdvFallbackCode uint8
}

func main() {
	if len(os.Args) < 2 {
		fmt.Println("Usage: go run main.go <path_to_il>")
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

	prog, err := concordia.LoadProgram(ilData)
	if err != nil {
		fmt.Printf("Failed to load program: %v\n", err)
		return
	}
	defer prog.Close()

	// --- Encode Example ---
	fmt.Println("--- Encoding ---")
	data := &KitchenSink{
		Magic:       0xDEADBEEF,
		FlagsA:      1,
		FlagB:       true,
		ValC:        -5,
		Timestamp:   1678886400,
		Position:    Vec3{X: 1.0, Y: 2.0, Z: 3.0},
		Matrix:      [4]uint8{10, 20, 30, 40},
		Points:      []uint16{100, 200, 300},
		Name:        "Concordia Go",
		Status:      StatusFail,
		ErrorCode:   404,
		Reason:      "Not Found",
		Percentage:  75,
		Temperature: 36.65, // @scale(0.1) -> Should truncate to 36.6
		ValAdd:      20,
		ValSub:      20,
		ValMul:      11, // @mul(2) -> 11/2 = 5 -> 5*2 = 10. Lossy!
		ValDiv:      20,
		Year:        2025, // Requires 2 bytes normally, but fits in 1 byte here!
		PolyVal:     75.0, // Raw 10 -> 5 + 20 + 50 = 75
		SplineVal:   50.0, // Raw 5 -> 50.0
		BitPacked: struct {
			A3bits   uint8
			B5bits   uint8
			C4bits   uint8
			DAligned uint8
		}{
			A3bits:   7,
			B5bits:   31,
			C4bits:   15,
			DAligned: 255,
		},
		HasExtra:      true,
		ExtraData:     "Some extra payload",
		Checksum:      0,
		AdvMode:       1,
		AdvHasDetails: true,
		AdvDetails:    "Advanced Details",
	}

	buffer := make([]byte, 1024) // Output buffer

	matrixIdx := 0
	pointsIdx := 0

	err = prog.Execute(buffer, concordia.ModeEncode, func(ctx *concordia.Context, keyID uint16, typeOp concordia.OpCode, val concordia.Value) error {
		if val.UnsafeAddr() == nil {
			return nil
		}

		name := prog.GetKeyName(keyID)
		switch name {
		case "magic":
			val.SetUint32(data.Magic)
		case "flags_a":
			val.SetUint16(data.FlagsA)
		case "flag_b":
			val.SetBool(data.FlagB)
		case "val_c":
			val.SetInt8(data.ValC)
		case "timestamp":
			val.SetInt64(data.Timestamp)
		case "x":
			val.SetFloat32(data.Position.X)
		case "y":
			val.SetFloat32(data.Position.Y)
		case "z":
			val.SetFloat32(data.Position.Z)
		case "matrix":
			// Array Start
			if typeOp == 0x34 { // OP_ARR_FIXED
				val.SetUint32(4)
				matrixIdx = 0
			} else {
				// Element (if it shares the name)
				if matrixIdx < len(data.Matrix) {
					val.SetUint8(data.Matrix[matrixIdx])
					matrixIdx++
				}
			}
		case "points":
			if typeOp == 0x35 { // OP_ARR_PRE_U8
				val.SetUint8(uint8(len(data.Points)))
				pointsIdx = 0
			} else {
				if pointsIdx < len(data.Points) {
					val.SetUint16(data.Points[pointsIdx])
					pointsIdx++
				}
			}
		case "name":
			val.SetString(data.Name)
		case "status":
			val.SetUint8(uint8(data.Status))
		case "confidence":
			val.SetUint8(data.Confidence)
		case "error_code":
			val.SetUint16(data.ErrorCode)
		case "reason":
			val.SetString(data.Reason)
		case "percentage":
			val.SetUint8(data.Percentage)
		case "temperature":
			val.SetFloat64(data.Temperature)
		case "val_add":
			val.SetUint8(data.ValAdd)
		case "val_sub":
			val.SetUint8(data.ValSub)
		case "val_mul":
			val.SetUint8(data.ValMul)
		case "val_div":
			val.SetUint8(data.ValDiv)
		case "year":
			fmt.Printf(">> PROOF: 'year' OpCode is 0x%X (16 = OP_IO_U8, 17 = OP_IO_U16)\n", uint8(typeOp))
			val.SetUint16(data.Year)
		case "poly_val":
			val.SetFloat64(data.PolyVal)
		case "spline_val":
			val.SetFloat64(data.SplineVal)
		case "a_3bits":
			val.SetUint8(data.BitPacked.A3bits)
		case "b_5bits":
			val.SetUint8(data.BitPacked.B5bits)
		case "c_4bits":
			val.SetUint8(data.BitPacked.C4bits)
		case "d_aligned":
			val.SetUint8(data.BitPacked.DAligned)
		case "has_extra":
			val.SetBool(data.HasExtra)
		case "extra_data":
			val.SetString(data.ExtraData)
		case "checksum":
			val.SetUint32(data.Checksum)
		case "adv_mode":
			val.SetUint8(data.AdvMode)
		case "adv_simple_val":
			val.SetUint16(data.AdvSimpleVal)
		case "adv_has_details":
			val.SetBool(data.AdvHasDetails)
		case "adv_details":
			val.SetString(data.AdvDetails)
		case "adv_fallback_code":
			val.SetUint8(data.AdvFallbackCode)
		default:
			// If we are in an array loop, the key might be the array key?
			// Or if the element has no name?
			// For now, assume elements share the key if they are primitives.
			// If not, we might need to handle by ID or context.
			// fmt.Printf("Unknown Key: %s (%d)\n", name, keyID)
		}
		return nil
	})

	if err != nil {
		fmt.Printf("Encode Error: %v\n", err)
		return
	}

	fmt.Printf("Encoded Data (Hex): %X\n", buffer[:100])

	// --- Decode Example ---
	fmt.Println("\n--- Decoding ---")
	decoded := &KitchenSink{}

	matrixIdx = 0
	pointsIdx = 0

	err = prog.Execute(buffer, concordia.ModeDecode, func(ctx *concordia.Context, keyID uint16, typeOp concordia.OpCode, val concordia.Value) error {
		if val.UnsafeAddr() == nil {
			return nil
		}

		isQuery := typeOp == concordia.OpCtxQuery || typeOp == concordia.OpLoadCtx

		name := prog.GetKeyName(keyID)
		switch name {
		case "magic":
			decoded.Magic = val.Uint32()
		case "flags_a":
			decoded.FlagsA = val.Uint16()
		case "flag_b":
			decoded.FlagB = val.Bool()
		case "val_c":
			decoded.ValC = val.Int8()
		case "timestamp":
			decoded.Timestamp = val.Int64()
		case "x":
			decoded.Position.X = val.Float32()
		case "y":
			decoded.Position.Y = val.Float32()
		case "z":
			decoded.Position.Z = val.Float32()
		case "matrix":
			if typeOp == concordia.OpArrFixed {
				matrixIdx = 0
			} else {
				if matrixIdx < 4 {
					decoded.Matrix[matrixIdx] = val.Uint8()
					matrixIdx++
				}
			}
		case "points":
			if typeOp == concordia.OpArrPreU8 {
				count := val.Uint8()
				decoded.Points = make([]uint16, count)
				pointsIdx = 0
			} else {
				if pointsIdx < len(decoded.Points) {
					decoded.Points[pointsIdx] = val.Uint16()
					pointsIdx++
				}
			}
		case "name":
			decoded.Name = val.String()
		case "status":
			if isQuery {
				val.SetUint8(uint8(decoded.Status))
			} else {
				decoded.Status = Status(val.Uint8())
			}
		case "confidence":
			decoded.Confidence = val.Uint8()
		case "error_code":
			decoded.ErrorCode = val.Uint16()
		case "reason":
			decoded.Reason = val.String()
		case "percentage":
			decoded.Percentage = val.Uint8()
		case "temperature":
			decoded.Temperature = val.Float64()
		case "val_add":
			if isQuery {
				val.SetUint8(decoded.ValAdd)
			} else {
				decoded.ValAdd = val.Uint8()
			}
		case "val_sub":
			if isQuery {
				val.SetUint8(decoded.ValSub)
			} else {
				decoded.ValSub = val.Uint8()
			}
		case "val_mul":
			decoded.ValMul = val.Uint8()
		case "val_div":
			decoded.ValDiv = val.Uint8()
		case "year":
			decoded.Year = val.Uint16()
		case "poly_val":
			decoded.PolyVal = val.Float64()
		case "spline_val":
			decoded.SplineVal = val.Float64()
		case "expr_val":
			decoded.ExprVal = val.Uint8()
		case "a_3bits":
			decoded.BitPacked.A3bits = val.Uint8()
		case "b_5bits":
			decoded.BitPacked.B5bits = val.Uint8()
		case "c_4bits":
			decoded.BitPacked.C4bits = val.Uint8()
		case "d_aligned":
			decoded.BitPacked.DAligned = val.Uint8()
		case "has_extra":
			if isQuery {
				val.SetBool(decoded.HasExtra)
			} else {
				decoded.HasExtra = val.Bool()
			}
		case "extra_data":
			decoded.ExtraData = val.String()
		case "checksum":
			decoded.Checksum = val.Uint32()
		case "adv_mode":
			if isQuery {
				val.SetUint8(decoded.AdvMode)
			} else {
				decoded.AdvMode = val.Uint8()
			}
		case "adv_simple_val":
			decoded.AdvSimpleVal = val.Uint16()
		case "adv_has_details":
			if isQuery {
				val.SetBool(decoded.AdvHasDetails)
			} else {
				decoded.AdvHasDetails = val.Bool()
			}
		case "adv_details":
			decoded.AdvDetails = val.String()
		case "adv_fallback_code":
			decoded.AdvFallbackCode = val.Uint8()
		}
		return nil
	})

	if err != nil {
		fmt.Printf("Decode Error: %v\n", err)
		return
	}

	fmt.Printf("Decoded: %+v\n", decoded)
}
