"""Builtin function tables for the Python-subset frontend.

The one- and two-argument tables mirror the C compiler's `funcs_1arg` /
`funcs_2arg` so an expression compiles identically whether it came from a yexpr
string or a Python `def`. Three-argument forms and the higher-level "macro"
builtins (which expand to short opcode sequences rather than a single opcode)
are listed here too; their lowering lives in the frontend, which has the
instruction builder and argument ASTs in hand.
"""

from __future__ import annotations

# name -> opcode name. Direct single-instruction lowering.
FUNCS_1ARG: dict[str, str] = {
    "sin": "SIN",
    "cos": "COS",
    "tan": "TAN",
    "asin": "ASIN",
    "acos": "ACOS",
    "atan": "ATAN",
    "sinh": "SINH",
    "cosh": "COSH",
    "tanh": "TANH",
    "asinh": "ASINH",
    "acosh": "ACOSH",
    "atanh": "ATANH",
    "sinc": "SINC",
    "exp": "EXP",
    "exp2": "EXP2",
    "log": "LOG",
    "ln": "LOG",
    "log2": "LOG2",
    "sqrt": "SQRT",
    "rsqrt": "RSQRT",
    "inverseSqrt": "RSQRT",
    "abs": "ABS",
    "floor": "FLOOR",
    "ceil": "CEIL",
    "round": "ROUND",
    "trunc": "TRUNC",
    "fract": "FRACT",
    "frac": "FRACT",
    "sign": "SIGN",
    "saturate": "CLAMP01",
    "radians": "RADIANS",
    "degrees": "DEGREES",
    "erf": "ERF",
    "erfc": "ERFC",
    "rand": "RAND",
    "noise": "NOISE",
}

FUNCS_2ARG: dict[str, str] = {
    "pow": "POW",
    "atan2": "ATAN2",
    "min": "MIN",
    "max": "MAX",
    "mod": "MOD",
    "fmod": "MOD",
    "step": "STEP",
    "lt": "LT",
    "gt": "GT",
    "le": "LE",
    "ge": "GE",
    "eq": "EQ",
    "ne": "NE",
    "rand2": "RAND2",
    "noise2": "NOISE2",
}

# Three-argument builtins whose third operand is a register packed into imm.
# (mix/lerp -> MIX, smoothstep -> SMOOTHSTEP, select -> SELECT, clamp expands.)
THREE_ARG: set[str] = {"mix", "lerp", "smoothstep", "select", "clamp"}

# Shader-oriented "macro" builtins. These expand to short opcode sequences in
# the frontend. fbm2/ridge2 take a compile-time-constant octave count.
MACRO_NAMES: set[str] = {
    "safe_div",
    "safe_log",
    "safe_sqrt",
    "length2",
    "dist2",
    "remap",
    "remap01",
    "polar_radius",
    "polar_angle",
    "fbm2",
    "ridge2",
    "domain_warp_x",
    "domain_warp_y",
    "checker",
    "stripe",
}

# Named numeric constants usable as bare identifiers.
NAMED_CONSTANTS: dict[str, float] = {
    "pi": 3.14159265358979323846,
    "PI": 3.14159265358979323846,
    "e": 2.71828182845904523536,
    "E": 2.71828182845904523536,
    "tau": 6.28318530717958647693,
    "TAU": 6.28318530717958647693,
}
