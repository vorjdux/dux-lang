# stdlib: math

Import with:
```
import math
```

The `math` module exposes the standard mathematical functions from the C `<math.h>`
library.

---

## Functions

### Trigonometric

| Function            | Signature              | Description                          |
|---------------------|------------------------|--------------------------------------|
| `math.sin(x)`       | `double sin(double x)` | Sine of x (radians)                  |
| `math.cos(x)`       | `double cos(double x)` | Cosine of x (radians)                |
| `math.tan(x)`       | `double tan(double x)` | Tangent of x (radians)               |
| `math.asin(x)`      | `double asin(double x)`| Arc sine of x (result in radians)    |
| `math.acos(x)`      | `double acos(double x)`| Arc cosine of x (result in radians)  |
| `math.atan(x)`      | `double atan(double x)`| Arc tangent of x (result in radians) |
| `math.atan2(y, x)`  | `double atan2(double y, double x)` | Arc tangent of y/x |

### Exponential and Logarithmic

| Function            | Signature              | Description                          |
|---------------------|------------------------|--------------------------------------|
| `math.exp(x)`       | `double exp(double x)` | e raised to the power x              |
| `math.log(x)`       | `double log(double x)` | Natural logarithm of x               |
| `math.log2(x)`      | `double log2(double x)`| Base-2 logarithm of x                |
| `math.log10(x)`     | `double log10(double x)`| Base-10 logarithm of x              |
| `math.pow(x, y)`    | `double pow(double x, double y)` | x raised to the power y |

### Root and Rounding

| Function            | Signature              | Description                          |
|---------------------|------------------------|--------------------------------------|
| `math.sqrt(x)`      | `double sqrt(double x)`| Square root of x                     |
| `math.cbrt(x)`      | `double cbrt(double x)`| Cube root of x                       |
| `math.ceil(x)`      | `double ceil(double x)`| Smallest integer ≥ x                 |
| `math.floor(x)`     | `double floor(double x)`| Largest integer ≤ x                 |
| `math.round(x)`     | `double round(double x)`| Round to nearest integer             |
| `math.trunc(x)`     | `double trunc(double x)`| Truncate toward zero                 |
| `math.fabs(x)`      | `double fabs(double x)`| Absolute value                       |

### Hyperbolic

| Function            | Signature              | Description                          |
|---------------------|------------------------|--------------------------------------|
| `math.sinh(x)`      | `double sinh(double x)`| Hyperbolic sine                      |
| `math.cosh(x)`      | `double cosh(double x)`| Hyperbolic cosine                    |
| `math.tanh(x)`      | `double tanh(double x)`| Hyperbolic tangent                   |

---

## Constants

The `math` module does not currently expose named constants. Use literal values:

| Constant | Value                  |
|----------|------------------------|
| π (pi)   | `3.141592653589793`    |
| e        | `2.718281828459045`    |

---

## Example

```dux
import math

void main() {
    double x = 2.0
    double root = math.sqrt(x)
    println(root)   # prints 1.4142135623730951
}
```

---

## Implementation Notes

All `math.*` functions are lowered to LLVM intrinsics where available
(e.g. `llvm.sqrt.f64`, `llvm.pow.f64`, `llvm.sin.f64`, etc.) or to the
corresponding C library function via an external declaration. The compiler
links against the system `libm` when any `math.*` call is present.
