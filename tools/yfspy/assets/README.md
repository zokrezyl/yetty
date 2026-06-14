# yfsvm Python-subset frontend — reference implementation

A shader language with **Python syntax** that compiles to yfsvm bytecode. It is
not a Python runtime: nothing executes in CPython at render time. This package
parses a restricted, deterministic subset of Python with the standard `ast`
module and lowers it to the exact bytecode layout the GPU interpreter consumes
(see the [yfsvm VM README](../../../src/yetty/yfsvm/README.md)).

> **This is the reference implementation and test oracle, not the runtime
> path.** The `yfspy` tool compiles shaders **in-process in C**
> (`tools/yfspy/compile.c`), using the embedded libpython-free CPython parser
> (`src/cpython`, `yetty_cpython`) to build the AST and the same lowering as
> here to emit bytecode — so it needs no `uv`, no subprocess, and works on
> every target yetty builds for (including webasm/android). This Python package
> defines the language, documents it, and serves as the cross-check oracle: the
> C codegen is verified to emit **byte-identical** bytecode to it across the
> demo shader set, and `run_tests.py` validates the semantics.

You can still run it standalone to produce a `.bin` blob for `yfspy --bytecode`
or to inspect a compile. Its modules live here in `tools/yfspy/assets/`:

| File | Role |
|------|------|
| `opcodes.py` | Loads `yfsvm-opcodes.yaml` (the single source of truth) |
| `errors.py` | `CompileError` (carries a source line) |
| `assembler.py` | IR: instructions + symbolic labels + constant pool → words |
| `intrinsics.py` | Builtin name → opcode tables and the macro registry |
| `frontend.py` | The `ast` → IR lowering |
| `interp.py` | Reference CPU interpreter (mirrors the WGSL op-for-op) |
| `cli.py` | `compile` / `run` / `dump` command line |
| `run_tests.py` | Test suite |

## Running

```sh
# Compile to a little-endian u32 bytecode blob:
uv run tools/yfspy/assets/cli.py compile shader.py -o shader.bin

# Or emit a C array / hex dump:
uv run tools/yfspy/assets/cli.py compile shader.py --c-array shader_bytecode
uv run tools/yfspy/assets/cli.py compile shader.py --hex

# Evaluate a function with the reference interpreter (sanity check, no GPU):
uv run tools/yfspy/assets/cli.py run shader.py --func mandel_escape --x 0.3 --y 0.5

# Disassemble:
uv run tools/yfspy/assets/cli.py dump shader.py
```

## Supported subset

- `def` functions with positional scalar parameters
- local scalar assignment (`z = 0.0`) and augmented assignment (`z += 1.0`)
- `return EXPR` (and bare `return`)
- arithmetic `+ - * / % **`, unary `-`
- comparisons `< <= > >= == !=` (including chained, e.g. `0.0 < x < 1.0`)
- `and` / `or` / `not`
- `if` / `elif` / `else`
- `while`
- `for VAR in range(...)` — `range(stop)`, `range(start, stop)`,
  `range(start, stop, step)` with constant integer bounds
- conditional expression `A if cond else B`
- calls to builtins (see below)
- numeric literals, comments, docstrings

Each top-level `def` becomes one VM function returning one scalar. The GPU
selects a function by index at render time (e.g. yplot evaluates function `i`
per curve), so **functions are independent entry points and cannot call one
another** — there is no CALL opcode.

## Parameter → input binding

Each parameter maps to a VM input. A parameter named exactly `x`, `y`, `t`,
`time`, or `s0`..`s7` binds to that input. Any other name binds **positionally**
to the input sequence `[x, y, time, s0, s1, …, s7]`:

```python
def field(x, y, time, s0):   # literal: x, y, time, s0
    ...

def mandel_escape(cx, cy):   # positional: cx = x, cy = y
    ...
```

The maximum is 11 parameters (`x`, `y`, `time`, `s0`..`s7`). Inside the body the
parameter is an ordinary mutable local. The inputs `x`, `y`, `t`/`time`,
`s0`..`s7` can also be referenced bare without being parameters.

## Registers

`r0` is the return value. Named locals (parameters plus every assigned name in
the function) are pinned to `r1..rL`; expression temporaries use a stack above
them. With 16 registers that leaves 15 for locals + temporaries combined.
Python has no block scope, so every assignment in a `def` is one flat local set.

## Builtins

**One argument:** `sin cos tan asin acos atan sinh cosh tanh asinh acosh atanh
sinc exp exp2 log ln log2 sqrt rsqrt abs floor ceil round trunc fract sign
saturate radians degrees erf erfc rand noise`

**Two arguments:** `pow atan2 min max mod fmod step lt gt le ge eq ne rand2
noise2`

**Three arguments:** `mix`/`lerp`, `smoothstep`, `select`, `clamp`. For `mix`,
`smoothstep`, and `select` the third argument is a register, matching the VM's
`imm`-slot convention; `select(a, b, cond)` returns `b` when `cond` is truthy.

**Named constants:** `pi`, `e`, `tau` (and uppercase forms).

**Macros** (expand to short opcode sequences):

| Macro | Meaning |
|-------|---------|
| `safe_div(a, b, fallback)` | `a/b`, or `fallback` when `b ≈ 0` |
| `safe_log(x)` / `safe_sqrt(x)` | guarded `log` / `sqrt` |
| `length2(x, y)` / `dist2(ax, ay, bx, by)` | 2D length / distance |
| `remap(v, in0, in1, out0, out1)` | affine remap |
| `remap01(v, lo, hi)` | normalize to `[0,1]`, clamped |
| `polar_radius(x, y)` / `polar_angle(x, y)` | `√(x²+y²)` / `atan2(y, x)` |
| `fbm2(x, y, octaves)` / `ridge2(x, y, octaves)` | fractal value noise (constant octaves, 1–8) |
| `domain_warp_x(x, y, amount)` / `domain_warp_y(...)` | noise-driven warp |
| `checker(x, y, scale)` / `stripe(x, scale)` | procedural patterns |

## Lowering notes

- **`if` / `while`** lower to `JMP` / `JMP_IF_ZERO`. A relational guard
  (`<`, `>`, `<=`, `>=`) uses the fused `JMP_IF_LT` / `JMP_IF_GE` (the four
  directions reduce to two by swapping operands).
- **`for … range(N)`** lowers to an induction-variable loop (not unrolled), so a
  large `N` does not blow the instruction budget.
- **`and` / `or` / `not`** lower branchlessly over `0.0`/`1.0` values
  (`MUL` / `MAX` / `1 − x`). Operands are side-effect-free, so evaluating both
  is deterministic.
- **Timeout** is bounded by `YFSVM_MAX_EXECUTION_STEPS`; on overrun a function
  returns the current `r0` (see the README's timeout semantics).

## Rejected

Constructs with no deterministic, allocation-free shader meaning are rejected
with a line number rather than miscompiled: imports, classes, exceptions,
generators/comprehensions, `async`/`await`, dictionaries/lists/tuples,
attribute or method calls, `*args`/`**kwargs`/defaults, calls between
user-defined functions, and unknown identifiers or functions.

## Example

```python
def mandel_escape(cx, cy):
    zx = 0.0
    zy = 0.0
    i = 0.0
    while i < 128.0 and zx*zx + zy*zy < 4.0:
        xt = zx*zx - zy*zy + cx
        zy = 2.0*zx*zy + cy
        zx = xt
        i = i + 1.0
    return i / 128.0
```

```sh
uv run tools/yfspy/assets/cli.py run mandel.py --x 0.0 --y 0.0   # -> 1.0 (inside)
uv run tools/yfspy/assets/cli.py run mandel.py --x 0.6 --y 0.6   # -> ~0.02 (escapes)
```

## Testing

```sh
uv run tools/yfspy/assets/run_tests.py
```

The suite checks math parity, branch correctness, `while`/`for` lowering, the
loop-timeout sentinel, an escape-time fractal (against an independent CPU
reference), the macros, rejection of unsupported Python, and a drift guard that
the reference interpreter implements every opcode in the YAML.

## Relationship to the VM and codegen

The frontend reads the same `yfsvm-opcodes.yaml` that generates the C header and
the WGSL interpreter, so adding an opcode in one place keeps every consumer in
sync. The reference interpreter (`interp.py`) is a *reference*: it computes in
float64 while the GPU runs float32, so the last bits can differ — the
WGSL/GPU path remains the source of truth for on-screen results.

## Not yet implemented (future work)

- **Native vectors** (`vec2`/`vec3`/`vec4`, swizzles, `dot`/`length`/`normalize`)
  — the VM is scalar today; this is "Path B" in the issue.
- **Sampled buffers/textures** (`SAMPLE_1D` / `SAMPLE_2D`) beyond the current
  scalar sampler slots.

In-process compilation is **done**: `yfspy` no longer shells out — it compiles
shaders in C via the embedded CPython parser (`tools/yfspy/compile.c`), so a
Python-authored shader can be compiled and rendered live inside yetty with no
external toolchain.
