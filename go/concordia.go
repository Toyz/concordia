package concordia

/*
#cgo CFLAGS: -I../include -I../src/vm -std=c99
#include "../include/concordia.h"
#include <stdlib.h>

// Forward declaration for the gateway function
extern int go_io_callback_gateway(void* user_ptr, int mode, int endianness, uint16_t key_id, uint8_t type, void* ptr);

// C Gateway function to forward the callback to Go
// We cannot call the exported Go function directly from the function pointer type in C
static cnd_error_t c_callback_shim(cnd_vm_ctx* ctx, uint16_t key_id, uint8_t type, void* ptr) {
    return (cnd_error_t)go_io_callback_gateway(ctx->user_ptr, (int)ctx->mode, (int)ctx->endianness, key_id, type, ptr);
}

static void set_callback(cnd_vm_ctx* ctx) {
    ctx->io_callback = c_callback_shim;
}
*/
import "C"
import (
	"errors"
	"runtime/cgo"
	"unsafe"
)

// Error codes from concordia.h
var (
	GoErrOk             = errors.New("ok")
	GoErrOOB            = errors.New("out of bounds")
	GoErrInvalidOp      = errors.New("invalid opcode")
	GoErrStackOverflow  = errors.New("stack overflow")
	GoErrStackUnderflow = errors.New("stack underflow")
	GoErrValidation     = errors.New("validation error")
	GoErrCallback       = errors.New("callback error")
	GoErrUnknown        = errors.New("unknown error")
)

// String returns the human-readable error message from the C library
func (e Error) String() string {
	return C.GoString(C.cnd_error_string(C.cnd_error_t(e)))
}

func parseError(err Error) error {
	switch err {
	case ErrOk:
		return nil
	case ErrOOB:
		return GoErrOOB
	case ErrInvalidOp:
		return GoErrInvalidOp
	case ErrStackOverflow:
		return GoErrStackOverflow
	case ErrStackUnderflow:
		return GoErrStackUnderflow
	case ErrValidation:
		return GoErrValidation
	case ErrCallback:
		return GoErrCallback
	default:
		return errors.New(err.String())
	}
}

// Mode represents the VM execution mode (Encode or Decode)
// type Mode is defined in consts.go

// Context provides access to the VM state during a callback
type Context struct {
	mode           Mode
	endianness     int // 0 for LE, 1 for BE
	pendingStrings []*C.char
}

// Mode returns the current execution mode
func (c *Context) Mode() Mode {
	return c.mode
}

// FreePending frees any C strings allocated during the previous callback
func (c *Context) FreePending() {
	for _, s := range c.pendingStrings {
		C.free(unsafe.Pointer(s))
	}
	c.pendingStrings = c.pendingStrings[:0]
}

// Value wraps the unsafe pointer passed to the callback
type Value struct {
	ptr unsafe.Pointer
	op  OpCode
	ctx *Context
}

// UnsafeAddr returns the underlying unsafe pointer
func (v Value) UnsafeAddr() unsafe.Pointer {
	return v.ptr
}

// CallbackFunc is the function signature for user callbacks
// value is a safe wrapper around the data pointer.
type CallbackFunc func(ctx *Context, keyID uint16, typeOp OpCode, value Value) error

// VM wraps the C cnd_vm_ctx
type VM struct {
	ctx      C.cnd_vm_ctx
	callback CallbackFunc
	handle   cgo.Handle
}

// Program wraps the C cnd_program and manages its memory
type Program struct {
	cProg *C.cnd_program
	cMem  unsafe.Pointer // Pointer to the C memory holding the bytecode/IL
}

// LoadProgram loads a program from a byte slice (either raw bytecode or IL binary)
func LoadProgram(data []byte) (*Program, error) {
	if len(data) == 0 {
		return nil, errors.New("empty program data")
	}

	// Allocate C memory for the program data (bytecode or IL image)
	cMem := C.CBytes(data)
	cLen := C.size_t(len(data))

	// Allocate C struct for the program
	cProg := (*C.cnd_program)(C.malloc(C.size_t(unsafe.Sizeof(C.cnd_program{}))))
	if cProg == nil {
		C.free(cMem)
		return nil, errors.New("failed to allocate C program struct")
	}

	// Check for IL Header: "CNDIL"
	isIL := false
	if len(data) >= 16 && string(data[:5]) == "CNDIL" {
		isIL = true
	}

	if isIL {
		if ret := C.cnd_program_load_il(cProg, (*C.uint8_t)(cMem), cLen); ret != C.CND_ERR_OK {
			C.free(unsafe.Pointer(cProg))
			C.free(cMem)
			return nil, parseError(Error(ret))
		}
	} else {
		cProg.bytecode = (*C.uint8_t)(cMem)
		cProg.bytecode_len = cLen
		cProg.string_table = nil
		cProg.string_count = 0
	}

	return &Program{
		cProg: cProg,
		cMem:  cMem,
	}, nil
}

// Close frees the C memory associated with the program
func (p *Program) Close() {
	if p.cProg != nil {
		C.free(unsafe.Pointer(p.cProg))
		p.cProg = nil
	}
	if p.cMem != nil {
		C.free(p.cMem)
		p.cMem = nil
	}
}

// GetKeyName returns the string name for a given Key ID
func (p *Program) GetKeyName(keyID uint16) string {
	if p.cProg == nil {
		return ""
	}
	cStr := C.cnd_get_key_name(p.cProg, C.uint16_t(keyID))
	if cStr == nil {
		return ""
	}
	return C.GoString(cStr)
}

// Execute runs the VM with the given data
func (p *Program) Execute(data []byte, mode Mode, cb CallbackFunc) error {
	var cData unsafe.Pointer
	var cDataLen C.size_t
	if len(data) > 0 {
		cData = C.CBytes(data)
		defer C.free(cData)
		cDataLen = C.size_t(len(data))
	}

	cCtx := (*C.cnd_vm_ctx)(C.calloc(1, C.size_t(unsafe.Sizeof(C.cnd_vm_ctx{}))))
	if cCtx == nil {
		return errors.New("failed to allocate C context struct")
	}
	defer C.free(unsafe.Pointer(cCtx))

	cCtx.program = p.cProg
	cCtx.data_buffer = (*C.uint8_t)(cData)
	cCtx.data_len = cDataLen
	// cCtx.cursor = 0 // calloc zeroes it
	// cCtx.ip = 0
	// cCtx.bit_offset = 0
	cCtx.mode = C.cnd_mode_t(mode)
	cCtx.endianness = C.CND_LE // Default

	// Setup Context and Callback
	ctx := &Context{
		mode: mode,
	}
	defer ctx.FreePending() // Free any remaining strings after execution

	// We pass the *Context as the user_ptr
	// But we also need the user's callback function.
	// So we wrap them.
	wrapper := &callbackWrapper{
		ctx: ctx,
		cb:  cb,
	}

	handle := cgo.NewHandle(wrapper)
	defer handle.Delete()
	// cCtx.user_ptr is void*, but we want to pass the handle (uintptr) through it.
	// Direct conversion unsafe.Pointer(handle) triggers go vet, so we use a pointer cast trick.
	cCtx.user_ptr = *(*unsafe.Pointer)(unsafe.Pointer(&handle))

	C.set_callback(cCtx)

	// Execute
	ret := C.cnd_execute(cCtx)

	// Copy data back if needed
	if len(data) > 0 && cData != nil {
		copy(data, C.GoBytes(cData, C.int(cDataLen)))
	}

	return parseError(Error(ret))
}

type callbackWrapper struct {
	ctx *Context
	cb  CallbackFunc
}

// Execute runs the VM with the given program (IL binary or raw bytecode) and data
func Execute(program []byte, data []byte, mode Mode, cb CallbackFunc) error {
	prog, err := LoadProgram(program)
	if err != nil {
		return err
	}
	defer prog.Close()

	return prog.Execute(data, mode, cb)
}

//export go_io_callback_gateway
func go_io_callback_gateway(userPtr unsafe.Pointer, mode C.int, endianness C.int, keyID C.uint16_t, typeOp C.uint8_t, dataPtr unsafe.Pointer) C.int {
	handle := cgo.Handle(uintptr(userPtr))
	wrapper := handle.Value().(*callbackWrapper)

	// Free strings from the PREVIOUS callback call
	wrapper.ctx.FreePending()

	// Update mode and endianness in context
	wrapper.ctx.mode = Mode(mode)
	wrapper.ctx.endianness = int(endianness)

	err := wrapper.cb(wrapper.ctx, uint16(keyID), OpCode(typeOp), Value{
		ptr: dataPtr,
		op:  OpCode(typeOp),
		ctx: wrapper.ctx,
	})
	if err != nil {
		return C.int(ErrCallback)
	}
	return C.int(ErrOk)
}

// Helper functions to read values from the unsafe pointer in the callback

func (v Value) String() string {
	ptr := v.ptr
	typeOp := uint8(v.op)
	switch typeOp {
	case C.OP_STR_NULL:
		return C.GoString((*C.char)(ptr))
	case C.OP_STR_PRE_U8:
		// Length is at ptr - 1
		lenPtr := (*uint8)(unsafe.Pointer(uintptr(ptr) - 1))
		length := int(*lenPtr)
		return C.GoStringN((*C.char)(ptr), C.int(length))
	case C.OP_STR_PRE_U16:
		// Length is at ptr - 2.
		b0 := *(*uint8)(unsafe.Pointer(uintptr(ptr) - 2))
		b1 := *(*uint8)(unsafe.Pointer(uintptr(ptr) - 1))
		var length int
		if v.ctx.endianness == 0 { // LE
			length = int(uint16(b0) | uint16(b1)<<8)
		} else { // BE
			length = int(uint16(b1) | uint16(b0)<<8)
		}
		return C.GoStringN((*C.char)(ptr), C.int(length))
	case C.OP_STR_PRE_U32:
		// Length is at ptr - 4.
		b0 := *(*uint8)(unsafe.Pointer(uintptr(ptr) - 4))
		b1 := *(*uint8)(unsafe.Pointer(uintptr(ptr) - 3))
		b2 := *(*uint8)(unsafe.Pointer(uintptr(ptr) - 2))
		b3 := *(*uint8)(unsafe.Pointer(uintptr(ptr) - 1))
		var length int
		if v.ctx.endianness == 0 { // LE
			length = int(uint32(b0) | uint32(b1)<<8 | uint32(b2)<<16 | uint32(b3)<<24)
		} else { // BE
			length = int(uint32(b3) | uint32(b2)<<8 | uint32(b1)<<16 | uint32(b0)<<24)
		}
		return C.GoStringN((*C.char)(ptr), C.int(length))
	default:
		return ""
	}
}

func (v Value) SetString(val string) {
	cs := C.CString(val)
	v.ctx.pendingStrings = append(v.ctx.pendingStrings, cs)
	*(**C.char)(v.ptr) = cs
}

func (v Value) Uint8() uint8 {
	return *(*uint8)(v.ptr)
}

func (v Value) SetUint8(val uint8) {
	*(*uint8)(v.ptr) = val
}

func (v Value) Uint16() uint16 {
	return *(*uint16)(v.ptr)
}

func (v Value) SetUint16(val uint16) {
	*(*uint16)(v.ptr) = val
}

func (v Value) Uint32() uint32 {
	return *(*uint32)(v.ptr)
}

func (v Value) SetUint32(val uint32) {
	*(*uint32)(v.ptr) = val
}

func (v Value) Uint64() uint64 {
	return *(*uint64)(v.ptr)
}

func (v Value) SetUint64(val uint64) {
	*(*uint64)(v.ptr) = val
}

func (v Value) Float32() float32 {
	return *(*float32)(v.ptr)
}

func (v Value) SetFloat32(val float32) {
	*(*float32)(v.ptr) = val
}

func (v Value) Float64() float64 {
	return *(*float64)(v.ptr)
}

func (v Value) SetFloat64(val float64) {
	*(*float64)(v.ptr) = val
}

func (v Value) Int8() int8 {
	return *(*int8)(v.ptr)
}

func (v Value) SetInt8(val int8) {
	*(*int8)(v.ptr) = val
}

func (v Value) Int16() int16 {
	return *(*int16)(v.ptr)
}

func (v Value) SetInt16(val int16) {
	*(*int16)(v.ptr) = val
}

func (v Value) Int32() int32 {
	return *(*int32)(v.ptr)
}

func (v Value) SetInt32(val int32) {
	*(*int32)(v.ptr) = val
}

func (v Value) Int64() int64 {
	return *(*int64)(v.ptr)
}

func (v Value) SetInt64(val int64) {
	*(*int64)(v.ptr) = val
}

func (v Value) Bool() bool {
	return *(*uint8)(v.ptr) != 0
}

func (v Value) SetBool(val bool) {
	v_ := uint8(0)
	if val {
		v_ = 1
	}
	*(*uint8)(v.ptr) = v_
}
