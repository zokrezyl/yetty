# yfsvm - Yetty Fragment Shader Virtual Machine

A register-based bytecode VM that executes in the fragment shader for evaluating mathematical expressions.

## Overview

yfsvm is designed for evaluating expressions like `sin(x * 2) + cos(t)` directly in the GPU shader. It's used by yplot and other components that need per-pixel mathematical evaluation.

## Architecture

### Bytecode Format

Programs are serialized as 32-bit words:
```
[magic][version][funcCount][constCount][funcTable...][constants...][code...]
```

- **magic**: `0x5946534D` ("YFSM")
- **version**: 1
- **funcCount**: Number of functions (max 16)
- **constCount**: Number of constants (max 4096)
- **funcTable**: 16 packed entries `(offset:16 | length:16)`
- **constants**: f32 values, bitcast to u32
- **code**: Instructions

### Instruction Format (32-bit)

```
[31:24] opcode  (8 bits)
[23:20] dst     (4 bits) - destination register
[19:16] src1    (4 bits) - first source register
[15:12] src2    (4 bits) - second source register
[11:0]  imm12   (12 bits) - immediate/constant index
```

### Registers

16 general-purpose f32 registers (`r0`-`r15`). Register `r0` is the return value.

### Inputs

- **x**: Domain value (e.g., x-coordinate in a plot)
- **t**: Time value for animations
- **samplers[0..7]**: External sampler values

## Opcodes

### Control
| Opcode | Name | Description |
|--------|------|-------------|
| 0x00 | NOP | No operation |
| 0x01 | RET | Return r0 |

### Load/Move
| Opcode | Name | Description |
|--------|------|-------------|
| 0x02 | LOAD_C | Load constant: `dst = constants[imm]` |
| 0x03 | LOAD_X | Load domain: `dst = x` |
| 0x04 | LOAD_T | Load time: `dst = t` |
| 0x05 | LOAD_S | Load sampler: `dst = samplers[imm & 7]` |
| 0x06 | MOV | Move: `dst = src1` |

### Arithmetic
| Opcode | Name | Description |
|--------|------|-------------|
| 0x10 | ADD | `dst = src1 + src2` |
| 0x11 | SUB | `dst = src1 - src2` |
| 0x12 | MUL | `dst = src1 * src2` |
| 0x13 | DIV | `dst = src1 / src2` |
| 0x14 | NEG | `dst = -src1` |
| 0x15 | MOD | `dst = src1 mod src2` |

### Transcendentals
| Opcode | Name | Description |
|--------|------|-------------|
| 0x20 | SIN | `dst = sin(src1)` |
| 0x21 | COS | `dst = cos(src1)` |
| 0x22 | TAN | `dst = tan(src1)` |
| 0x23 | ASIN | `dst = asin(src1)` |
| 0x24 | ACOS | `dst = acos(src1)` |
| 0x25 | ATAN | `dst = atan(src1)` |
| 0x26 | ATAN2 | `dst = atan2(src1, src2)` |
| 0x27 | SINH | `dst = sinh(src1)` |
| 0x28 | COSH | `dst = cosh(src1)` |
| 0x29 | TANH | `dst = tanh(src1)` |
| 0x2A | ASINH | `dst = asinh(src1)` |
| 0x2B | ACOSH | `dst = acosh(src1)` |
| 0x2C | ATANH | `dst = atanh(src1)` |
| 0x2D | SINC | `dst = sin(src1)/src1` (1 at src1=0) |

### Exponential/Logarithmic
| Opcode | Name | Description |
|--------|------|-------------|
| 0x30 | EXP | `dst = e^src1` |
| 0x31 | EXP2 | `dst = 2^src1` |
| 0x32 | LOG | `dst = ln(src1)` |
| 0x33 | LOG2 | `dst = log2(src1)` |
| 0x34 | POW | `dst = src1^src2` |
| 0x35 | SQRT | `dst = sqrt(src1)` |
| 0x36 | RSQRT | `dst = 1/sqrt(src1)` |

### Utility
| Opcode | Name | Description |
|--------|------|-------------|
| 0x40 | ABS | `dst = abs(src1)` |
| 0x41 | MIN | `dst = min(src1, src2)` |
| 0x42 | MAX | `dst = max(src1, src2)` |
| 0x43 | FLOOR | `dst = floor(src1)` |
| 0x44 | CEIL | `dst = ceil(src1)` |
| 0x45 | ROUND | `dst = round(src1)` |
| 0x46 | FRACT | `dst = fract(src1)` |
| 0x47 | SIGN | `dst = sign(src1)` |
| 0x48 | CLAMP01 | `dst = clamp(src1, 0, 1)` |
| 0x49 | STEP | `dst = step(src1, src2)` |
| 0x4A | MIX | `dst = mix(src1, src2, r[imm&0xF])` |
| 0x4B | SMOOTHSTEP | `dst = smoothstep(src1, src2, r[imm&0xF])` |
| 0x4C | TRUNC | `dst = trunc(src1)` |
| 0x4D | RADIANS | `dst = radians(src1)` |
| 0x4E | DEGREES | `dst = degrees(src1)` |

## Limits

| Limit | Value |
|-------|-------|
| Max registers | 16 |
| Max functions per program | 16 |
| Max constants per program | 4096 |
| Max instructions per function | 256 |
| Max execution steps (safety) | 1024 |

## Code Generation

Source of truth: `src/yetty/yfsvm/yfsvm-opcodes.yaml`

Generator: `src/yetty/yfsvm/gen-yfsvm-code.py`

Outputs:
- `include/yetty/yfsvm/yfsvm.gen.h` - C enum, encode/decode functions
- `src/yetty/yfsvm/yfsvm.gen.wgsl` - WGSL VM execution

## Example

Expression: `sin(x * 2.0) + 1.0`

Bytecode:
```
LOAD_X  r1              ; r1 = x
LOAD_C  r2, [0]         ; r2 = 2.0 (constant index 0)
MUL     r1, r1, r2      ; r1 = x * 2.0
SIN     r1, r1          ; r1 = sin(x * 2.0)
LOAD_C  r2, [1]         ; r2 = 1.0 (constant index 1)
ADD     r0, r1, r2      ; r0 = sin(x * 2.0) + 1.0
RET                     ; return r0
```
