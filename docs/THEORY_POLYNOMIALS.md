# Theory: Advanced Calibrators (XTCE Parity)

## Overview
To achieve full parity with XTCE (and similar space standards), Concordia needs to support more than just linear scaling. The "Big 4" calibration strategies are:

1.  **Linear** ($y = mx + c$): Currently supported via `@scale` / `@offset`.
2.  **Polynomial** ($y = \sum c_i x^i$): Proposed below.
3.  **Spline** (Piecewise Linear / Lookup Table): Essential for non-linear sensors (e.g., thermistors).
4.  **Math/Expression** (Arbitrary formulas): For complex derivations.

---

## 1. Polynomial Support (`<PolynomialCalibrator>`)

### DSL Syntax
```concordia
// Example: y = 5 + 2x + 0.5x^2
@poly(5.0, 2.0, 0.5)
uint16 sensor_val;
```

### Implementation
*   **IL**: `[OP_TRANS_POLY] [Count: u8] [Coeffs: double...]` (Inline coefficients).
*   **Runtime**: Evaluate polynomial using Horner's method.
*   **Reverse (Encode)**: Not supported (returns Validation Error).

---

## 2. Spline Support (`<SplineCalibrator>`)

Splines define a set of points $(x, y)$ and interpolate linearly between them. This is standard for calibrating raw sensor voltages to physical units.

### DSL Syntax
```concordia
// Define points: (raw, phys), (raw, phys)...
@spline(
    0,   -50,
    100, 0,
    200, 150,
    255, 500
)
uint8 temp_sensor;
```

### Implementation
*   **IL**: `[OP_TRANS_SPLINE] [ConstID: u16]` pointing to a table of `(double, double)` pairs.
*   **Runtime**:
    1.  Binary search to find the segment $[x_i, x_{i+1}]$ containing the raw value.
    2.  Linear interpolation: $y = y_i + (val - x_i) \frac{y_{i+1} - y_i}{x_{i+1} - x_i}$
*   **Reverse (Encode)**: Same logic, but search on $y$ axis to find $x$. (Requires monotonic spline for unique solution).

---

## 3. Math Expressions (`<MathOperationCalibrator>`)

Arbitrary expressions like `y = 5 * log(x) + sin(x)`.

**XTCE Note**: XTCE *does* support this via `<MathOperationCalibrator>`, but it typically requires writing raw Reverse Polish Notation (RPN) in XML tags, which is painful to write and read. Concordia's approach uses standard infix math strings.

### DSL Syntax
```concordia
@expr("x * 5 + 10") // Simple
@expr("log(x) * 100") // Complex
uint16 complex_val;
```

### Implementation
*   **IL**: `[OP_TRANS_EXPR] [Len: u8] [RPN_Bytecode...]`
*   **Runtime**: A tiny stack-based RPN evaluator inside the VM.
    *   Supported ops: `ADD`, `SUB`, `MUL`, `DIV`, `POW`, `LOG`, `SIN`, `COS`.
*   **Reverse (Encode)**: Extremely difficult. Usually read-only (Telemetry).

---

## Summary of Work Required

| Feature | Priority | Difficulty | Encode Support? |
| :--- | :--- | :--- | :--- |
| **Polynomial** | High | Medium | Hard (Roots) |
| **Spline** | High | Medium | Yes (if monotonic) |
| **Math** | Low | High | No |

### Action Items
1.  [x] Implement `OP_TRANS_POLY` (Forward).
2.  [ ] Implement `OP_TRANS_SPLINE` (Forward & Reverse).

