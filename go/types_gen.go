//go:build ignore

package concordia

/*
#cgo CFLAGS: -I../include
#include "concordia.h"
*/
import "C"

type Error C.cnd_error_t
type Mode C.cnd_mode_t
type Endian C.cnd_endian_t

const (
	ErrOk            = C.CND_ERR_OK
	ErrOOB           = C.CND_ERR_OOB
	ErrInvalidOp     = C.CND_ERR_INVALID_OP
	ErrValidation    = C.CND_ERR_VALIDATION
	ErrCallback      = C.CND_ERR_CALLBACK
	ErrStackOverflow = C.CND_ERR_STACK_OVERFLOW
	ErrStackUnderflow= C.CND_ERR_STACK_UNDERFLOW
)

const (
	ModeEncode = C.CND_MODE_ENCODE
	ModeDecode = C.CND_MODE_DECODE
)

const (
    OpNoop = C.OP_NOOP
    OpSetEndianLe = C.OP_SET_ENDIAN_LE
    OpSetEndianBe = C.OP_SET_ENDIAN_BE
    OpEnterStruct = C.OP_ENTER_STRUCT
    OpExitStruct = C.OP_EXIT_STRUCT
    OpMetaVersion = C.OP_META_VERSION
    OpCtxQuery = C.OP_CTX_QUERY
    OpMetaName = C.OP_META_NAME
    
    OpIoU8 = C.OP_IO_U8
    OpIoU16 = C.OP_IO_U16
    OpIoU32 = C.OP_IO_U32
    OpIoU64 = C.OP_IO_U64
    OpIoI8 = C.OP_IO_I8
    OpIoI16 = C.OP_IO_I16
    OpIoI32 = C.OP_IO_I32
    OpIoI64 = C.OP_IO_I64
    OpIoF32 = C.OP_IO_F32
    OpIoF64 = C.OP_IO_F64
    OpIoBool = C.OP_IO_BOOL
    
    OpIoBitU = C.OP_IO_BIT_U
    OpIoBitI = C.OP_IO_BIT_I
    OpIoBitBool = C.OP_IO_BIT_BOOL
    OpAlignPad = C.OP_ALIGN_PAD
    OpAlignFill = C.OP_ALIGN_FILL
    
    OpStrNull = C.OP_STR_NULL
    OpStrPreU8 = C.OP_STR_PRE_U8
    OpStrPreU16 = C.OP_STR_PRE_U16
    OpStrPreU32 = C.OP_STR_PRE_U32
    OpArrFixed = C.OP_ARR_FIXED
    OpArrPreU8 = C.OP_ARR_PRE_U8
    OpArrPreU16 = C.OP_ARR_PRE_U16
    OpArrPreU32 = C.OP_ARR_PRE_U32
    OpArrEnd = C.OP_ARR_END
    OpRawBytes = C.OP_RAW_BYTES
)
