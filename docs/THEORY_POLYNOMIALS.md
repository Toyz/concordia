# Theory: Complex Polynomial Support (XTCE Style)

## Overview
Currently, Concordia supports linear scaling (`y = mx + c`) via `@scale` and `@offset`. To reach parity with XTCE's `<PolynomialCalibrator>`, we need to support arbitrary polynomial curves:
$$ y = c_0 + c_1x + c_2x^2 + \dots + c_nx^n $$

## Implementation Plan

### 1. DSL Syntax
Add a new decorator `@poly` that accepts a variable list of coefficients.
```concordia
// Example: y = 5 + 2x + 0.5x^2
@poly(5.0, 2.0, 0.5)
uint16 sensor_val;
```

### 2. IL Structure (OpCode)
We need a new opcode `OP_TRANS_POLY`. Since the number of coefficients is variable, we cannot just embed them in the instruction stream efficiently without bloating the VM loop.

**Option A: Immediate Values**
Store count + values directly in IL.
`[OP_TRANS_POLY] [Count: u8] [C0: f64] [C1: f64] ...`
*   *Pros*: Simple compiler implementation.
*   *Cons*: Bloats IL size if used frequently.

**Option B: Constant Table (Preferred)**
Store coefficients in a separate "Constants" section of the binary (similar to the String Table), and reference them by ID.
`[OP_TRANS_POLY] [ConstID: u16]`
*   *Pros*: Reusable coefficients (e.g., multiple sensors with same curve). Keeps bytecode small.
*   *Cons*: Requires adding a `ConstTable` to the `cnd_program` struct.

### 3. VM Runtime (`cnd_vm_ctx`)
Update `cnd_trans_t` enum:
```c
typedef enum {
    CND_TRANS_NONE = 0,
    // ... existing ...
    CND_TRANS_POLY_F64
} cnd_trans_t;
```

Update Context to hold polynomial state:
```c
typedef struct cnd_vm_ctx_t {
    // ...
    // Transformation State
    cnd_trans_t trans_type;
    union {
        struct { double m; double c; } linear;
        struct { const double* coeffs; uint8_t count; } poly;
        int64_t i_val;
    } trans;
    // ...
} cnd_vm_ctx;
```

### 4. Execution Logic (`vm_exec.c`)

**Decoding (Binary -> App)**
Calculate $y = \sum c_i x^i$.
```c
double x = (double)raw_value;
double y = 0;
double x_pow = 1;
for(int i=0; i < ctx->trans.poly.count; i++) {
    y += ctx->trans.poly.coeffs[i] * x_pow;
    x_pow *= x;
}
```

**Encoding (App -> Binary)**
This is the hard part. Reversing a polynomial $y = f(x)$ to find $x$ requires finding the roots of $f(x) - y = 0$.
*   **Linear ($n=1$)**: Trivial ($x = (y-c)/m$).
*   **Quadratic ($n=2$)**: Quadratic formula.
*   **Higher Order**: Requires iterative approximation (Newton-Raphson).

**Constraint**: For telemetry (Decode), we only need the forward calculation. For Command parameters (Encode), we need the reverse.
*   *Proposal*: Only support `@poly` for Telemetry (Decode) initially.
*   *Alternative*: Use a lookup table (LUT) for reverse mapping if the range is small.

## Comparison with XTCE
XTCE defines `<PolynomialCalibrator>` for both directions. It usually assumes the function is monotonic over the valid range of the sensor.

## Action Items
1.  [ ] Add `ConstTable` support to `cnd_program` and file format.
2.  [ ] Implement `OP_TRANS_POLY` in Compiler.
3.  [ ] Implement Forward Calculation in VM (Decode).
4.  [ ] Decide on Reverse Calculation strategy (Error out? Newton-Raphson?).
