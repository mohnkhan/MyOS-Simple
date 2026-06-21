[← Home](../Home.md)

# Fixed-Point Arithmetic

*The Stage 4 calculator does decimals without an FPU, a floating-point library, or a single floating-point instruction — just integers scaled by 1000.*

A freestanding kernel has no `float`, no `double`, no `libm`, and (in MyOS) no FPU setup. Yet the
[Stage 4](../stages/stage-4-clock-processes-calc.md) shell can evaluate `calc 10.5 / 2.5` and
print `4.2`. The trick is **fixed-point arithmetic**: store every decimal as an *integer scaled
by a fixed power of ten*, and do all math with ordinary integer operations. This article traces
the implementation in `shell.c` — `parse_float`, the four operators in `cmd_calc`, and
`float_to_str` — and explains why each one is shaped the way it is.

> 💡 **Tidbit:** "Fixed-point" means the decimal point sits at a *fixed* position in the integer,
> as opposed to floating-point where it moves. MyOS picks a scale of **1000**, i.e. three
> fractional digits, so the number `3.14` is stored as the integer `3140`. The decimal point is
> implied between the thousands and the hundreds — it never actually exists in memory.

## The core idea: scale by 1000

Pick a scale factor `S` (here `S = 1000`) and represent the real value `x` as the integer
`round(x * S)`. Then:

| Real value | Stored as |
| --- | --- |
| `3.14` | `3140` |
| `0.5` | `500` |
| `10` | `10000` |
| `-5.2` | `-5200` |

Three fractional digits gives a net precision of **1/1000** — you can distinguish `1.234` from
`1.235`, but not finer. The range is bounded by the integer width: with 32-bit `int`, scaling by
1000 costs you three decimal orders of magnitude of headroom before overflow.

## Parsing: `parse_float`

`parse_float` turns a string like `"3.14"` into the scaled integer `3140`. It returns the value
*already multiplied by 1000* and sets `*has_decimal` so the caller knows whether the user typed a
decimal point (`shell.c:217`):

```c
int parse_float(const char* str, int* has_decimal) {
    int result = 0;
    int decimal_places = 0;
    int sign = 1;
    int i = 0;
    *has_decimal = 0;

    while (str[i] == ' ') i++;          // skip leading spaces

    if (str[i] == '-') { sign = -1; i++; }   // sign
    else if (str[i] == '+') { i++; }

    // Integer part
    while (str[i] >= '0' && str[i] <= '9') {
        result = result * 10 + (str[i] - '0');
        i++;
    }

    // Fractional part — at most 3 digits kept
    if (str[i] == '.') {
        *has_decimal = 1;
        i++;
        while (str[i] >= '0' && str[i] <= '9' && decimal_places < 3) {
            result = result * 10 + (str[i] - '0');
            decimal_places++;
            i++;
        }
        while (str[i] >= '0' && str[i] <= '9') i++;   // discard extra digits
    }

    // Pad to exactly 3 fractional places
    while (decimal_places < 3) {
        result = result * 10;
        decimal_places++;
    }

    return sign * result;
}
```

The key insight is the **padding loop at the end**. The parser accumulates digits into a single
integer without ever tracking where the point goes; instead it counts how many fractional digits
it consumed and then multiplies by 10 until it has filled exactly three. So:

- `"3"` → integer `3`, 0 decimal places → padded ×10×10×10 → `3000`
- `"3.1"` → `31`, 1 place → padded ×10×10 → `3100`
- `"3.14"` → `314`, 2 places → padded ×10 → `3140`
- `"3.14159"` → `3141`, the `59` is discarded (capped at 3 places), → `3141`

Every parsed number lands in the same scale, which is what makes the arithmetic uniform.

> 💡 **Tidbit:** Notice the parser *truncates* rather than rounds the fourth and later digits —
> `3.1419` becomes `3141`, not `3142`. With a fixed scale and integer math, truncation is the
> cheap default; rounding would mean inspecting the discarded digit and conditionally adding one.

## The four operators

`cmd_calc` parses both operands with `parse_float`, then switches on the operator
(`shell.c:769`). Each operator has to respect the scale.

### Addition and subtraction — trivial

When both operands share the same scale, you can add or subtract them directly: scaling is linear,
so `(a*S) ± (b*S) = (a ± b)*S` (`shell.c:770`):

```c
case '+':
    result = num1 + num2;
    break;

case '-':
    result = num1 - num2;
    break;
```

`3140 + 500 = 3640`, which is `3.64` — correct, no adjustment needed.

### Multiplication — divide out one scale, and split to avoid overflow

Multiplication is where fixed-point gets subtle. If both inputs carry a factor of `S`, then
`(a*S) * (b*S) = (a*b)*S²` — the result has **two** scale factors and must be divided by `S` once
to get back to the canonical single-scale form. Done naïvely as `(num1 * num2) / 1000`, the
intermediate product overflows a 32-bit `int` for even modest inputs. MyOS therefore splits each
operand into its integer and fractional parts and combines the cross-terms (`shell.c:778`):

```c
case '*': {
    if (num1 < 0) num1 = -num1;
    if (num2 < 0) num2 = -num2;

    // Perform multiplication in parts to avoid overflow
    int temp_high = (num1 / 1000) * num2;            // intPart(a) * b
    int temp_low  = (num1 % 1000) * (num2 / 1000);   // fracPart(a) * intPart(b)
    int temp_frac = ((num1 % 1000) * (num2 % 1000)) / 1000;  // frac * frac / 1000

    result = temp_high + temp_low + temp_frac;

    // Handle sign from the original strings
    int sign1 = (num1_str_temp[0] == '-') ? -1 : 1;
    int sign2 = (num2_str_temp[0] == '-') ? -1 : 1;
    if (sign1 * sign2 < 0) result = -result;
    break;
}
```

Why this gives the right scale: write `a = A + f_a/1000` and `b = B + f_b/1000` where `A`, `B`
are the integer parts and `f_a`, `f_b` are the three-digit fractions. The product is
`A·b + (f_a · B) + (f_a · f_b / 1000)` — and because `num1 = a*1000`, the term `(num1/1000)*num2`
is exactly `A * (b*1000) = (A·b)*1000`, already in single-scale form. The last cross-term needs
its extra factor divided out, hence `/ 1000`. The split keeps every intermediate small enough to
stay within 32 bits for reasonable inputs. The sign is taken from the original operand strings
because the operands were forced positive before the arithmetic.

### Division — scale the numerator *up* to keep precision

Division has the opposite problem. `(a*S) / (b*S) = a/b` with **no** scale factor — integer
division would throw away the fraction entirely (`5200 / 2500 = 2`, losing the `.08`). MyOS
recovers precision by scaling the remainder back up by 1000 before dividing it (`shell.c:803`):

```c
case '/': {
    if (num2 == 0) {
        println("Error: Division by zero!", RED_ON_BLACK);
        return;
    }
    /* ...pull signs positive, track combined sign... */

    int quotient  = num1 / num2;                 // whole part (in fixed-point units)
    int remainder = num1 % num2;
    int fractional = (remainder * 1000) / num2;   // scaled-up fractional digits

    result = quotient * 1000 + fractional;
    result = result * sign;

    has_decimal_result = 1;  // Division always produces a decimal result
    break;
}
```

`quotient * 1000` re-establishes the canonical scale, and `(remainder * 1000) / num2` extracts
three more fractional digits from what integer division would have discarded. Division is also
the one operator that *always* forces a decimal result (`has_decimal_result = 1`), since the point
of doing it in fixed-point is to show the fraction.

> ⚠️ **Caveat:** This is integer arithmetic wearing a decimal costume, so overflow is real. With
> a 32-bit `int` and a scale of 1000, the multiplication cross-terms and the division's
> `remainder * 1000` can overflow for large operands — the code's own comments call the approach
> "simpler" and good "for reasonable numbers." There is also no rounding: results are truncated
> to three places, so `10 / 3` yields `3.333`, not `3.334`. And precision is hard-capped at
> 1/1000 — a fourth decimal digit the user types is simply dropped.

## Formatting: `float_to_str`

To print a scaled integer, `float_to_str` splits it back into the integer and fractional parts —
the exact inverse of the scale (`shell.c:269`, `shell.c:289`):

```c
int integer_part = num / 1000;
int decimal_part = num % 1000;
```

It then emits the integer part, a `.`, and up to three fractional digits, **stripping trailing
zeros** so `3640` prints as `3.64` rather than `3.640` (`shell.c:323`):

```c
int d1 = decimal_part / 100;        // tenths
int d2 = (decimal_part / 10) % 10;  // hundredths
int d3 = decimal_part % 10;         // thousandths

if (d3 != 0) {                       // 3 digits significant
    str[out_idx++] = d1 + '0';
    str[out_idx++] = d2 + '0';
    str[out_idx++] = d3 + '0';
} else if (d2 != 0) {                // 2 digits
    str[out_idx++] = d1 + '0';
    str[out_idx++] = d2 + '0';
} else if (d1 != 0 || force_decimal) {  // 1 digit
    str[out_idx++] = d1 + '0';
}
```

The `force_decimal` flag (set when either operand had a decimal point, or always for division)
ensures a value like `4.0` still shows its `.0` instead of collapsing to a bare `4`.

## Why fixed-point at all?

Beyond "there is no FPU," fixed-point with a power-of-ten scale has a genuine virtue: **decimal
exactness**. Binary floating-point cannot represent `0.1` exactly, so `0.1 + 0.2` famously yields
`0.30000000000000004`. A base-10 fixed-point representation stores `0.1` as exactly `100` and
`0.2` as exactly `200`; their sum is exactly `300`, i.e. `0.3`. The trade you make is **range**:
those three scale digits are three orders of magnitude you can no longer reach before overflow.

> 💡 **Tidbit:** This decimal-over-binary trade-off is the same one the [CMOS RTC](cmos-rtc.md)
> makes with BCD — encode for human-friendly decimal exactness and pay for it in bits. Real
> financial systems use decimal fixed-point (or dedicated decimal types) for exactly this reason:
> a bank cannot afford `0.1 + 0.2 ≠ 0.3` in your account balance.

## See also

- [CMOS RTC](cmos-rtc.md) — the same decimal-exactness trade-off, in BCD
- [Cooperative scheduling](cooperative-scheduling.md) — the other Stage 4 subsystem
- [Stage 4: clock, processes, calculator](../stages/stage-4-clock-processes-calc.md) — where the calculator is built
- [Command reference](../reference/command-reference.md) — `calc` syntax and examples
- [Freestanding C](freestanding-c.md) — why there is no FPU, no `libm`, no `float`
- [Glossary](../reference/glossary.md) — fixed-point, scale factor, overflow
- [Home](../Home.md)
