"""Reference CPU interpreter for yfsvm bytecode.

This mirrors the generated WGSL interpreter (`yfsvm.gen.wgsl`) instruction for
instruction: same header parsing, same function-relative jumps, same step
limit. It exists so the Python frontend can be tested end-to-end — compile a
program, run it here, compare against expected math — without a GPU.

It is a *reference*: arithmetic is done in float64, whereas the GPU runs in
float32, so the last bits can differ. The GPU/WGSL path remains the source of
truth for on-screen results.
"""

from __future__ import annotations

import math

from opcodes import (
    OpcodeTable,
    decode_dst,
    decode_imm12,
    decode_opcode,
    decode_src1,
    decode_src2,
    load_opcode_table,
    u32_bits_to_float,
)


def _fract(value: float) -> float:
    return value - math.floor(value)


def _safe(function, *operands) -> float:
    """Evaluate a math op, returning NaN/inf instead of raising, like the GPU."""
    try:
        return function(*operands)
    except (ValueError, ZeroDivisionError, OverflowError):
        return math.nan


def _erf(value: float) -> float:
    t = 1.0 / (1.0 + 0.3275911 * abs(value))
    poly = (
        (((1.061405429 * t - 1.453152027) * t + 1.421413741) * t - 0.284496736) * t
        + 0.254829592
    ) * t
    return math.copysign(1.0, value) * (1.0 - poly * math.exp(-value * value))


def _value_noise_1d(value: float) -> float:
    base = math.floor(value)
    frac = value - base
    smooth = frac * frac * (3.0 - 2.0 * frac)
    a = _fract(math.sin(base * 12.9898) * 43758.5453)
    b = _fract(math.sin((base + 1.0) * 12.9898) * 43758.5453)
    return a + (b - a) * smooth


def _hash2(ix: float, iy: float) -> float:
    return _fract(math.sin(ix * 12.9898 + iy * 78.233) * 43758.5453)


def _value_noise_2d(vx: float, vy: float) -> float:
    ix, iy = math.floor(vx), math.floor(vy)
    fx, fy = vx - ix, vy - iy
    ux = fx * fx * (3.0 - 2.0 * fx)
    uy = fy * fy * (3.0 - 2.0 * fy)
    a = _hash2(ix, iy)
    b = _hash2(ix + 1.0, iy)
    c = _hash2(ix, iy + 1.0)
    d = _hash2(ix + 1.0, iy + 1.0)
    return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy


def _mix(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def _smoothstep(edge0: float, edge1: float, value: float) -> float:
    if edge0 == edge1:
        return 0.0
    t = max(0.0, min(1.0, (value - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


# Value-producing opcodes: each takes (v1, v2, regs, imm) and returns the value
# written to the destination register, matching the WGSL case bodies exactly.
_VALUE_OPS = {
    "LOAD_C": None,  # handled specially (reads constant table)
    "LOAD_X": None,
    "LOAD_T": None,
    "LOAD_S": None,
    "LOAD_Y": None,
    "MOV": lambda v1, v2, regs, imm: v1,
    "ADD": lambda v1, v2, regs, imm: v1 + v2,
    "SUB": lambda v1, v2, regs, imm: v1 - v2,
    "MUL": lambda v1, v2, regs, imm: v1 * v2,
    "DIV": lambda v1, v2, regs, imm: _safe(lambda: v1 / v2),
    "NEG": lambda v1, v2, regs, imm: -v1,
    "MOD": lambda v1, v2, regs, imm: _safe(lambda: v1 - v2 * math.floor(v1 / v2)),
    "SIN": lambda v1, v2, regs, imm: math.sin(v1),
    "COS": lambda v1, v2, regs, imm: math.cos(v1),
    "TAN": lambda v1, v2, regs, imm: _safe(math.tan, v1),
    "ASIN": lambda v1, v2, regs, imm: _safe(math.asin, v1),
    "ACOS": lambda v1, v2, regs, imm: _safe(math.acos, v1),
    "ATAN": lambda v1, v2, regs, imm: math.atan(v1),
    "ATAN2": lambda v1, v2, regs, imm: _safe(math.atan2, v1, v2),
    "SINH": lambda v1, v2, regs, imm: _safe(math.sinh, v1),
    "COSH": lambda v1, v2, regs, imm: _safe(math.cosh, v1),
    "TANH": lambda v1, v2, regs, imm: math.tanh(v1),
    "ASINH": lambda v1, v2, regs, imm: math.asinh(v1),
    "ACOSH": lambda v1, v2, regs, imm: _safe(math.acosh, v1),
    "ATANH": lambda v1, v2, regs, imm: _safe(math.atanh, v1),
    "SINC": lambda v1, v2, regs, imm: 1.0 if abs(v1) < 1e-6 else math.sin(v1) / v1,
    "EXP": lambda v1, v2, regs, imm: _safe(math.exp, v1),
    "EXP2": lambda v1, v2, regs, imm: _safe(lambda: 2.0 ** v1),
    "LOG": lambda v1, v2, regs, imm: _safe(math.log, v1),
    "LOG2": lambda v1, v2, regs, imm: _safe(math.log2, v1),
    "POW": lambda v1, v2, regs, imm: _safe(lambda: v1 ** v2),
    "SQRT": lambda v1, v2, regs, imm: _safe(math.sqrt, v1),
    "RSQRT": lambda v1, v2, regs, imm: _safe(lambda: 1.0 / math.sqrt(v1)),
    "ERF": lambda v1, v2, regs, imm: _erf(v1),
    "ERFC": lambda v1, v2, regs, imm: 1.0 - _erf(v1),
    "ABS": lambda v1, v2, regs, imm: abs(v1),
    "MIN": lambda v1, v2, regs, imm: min(v1, v2),
    "MAX": lambda v1, v2, regs, imm: max(v1, v2),
    "FLOOR": lambda v1, v2, regs, imm: math.floor(v1),
    "CEIL": lambda v1, v2, regs, imm: math.ceil(v1),
    "ROUND": lambda v1, v2, regs, imm: float(round(v1)),
    "FRACT": lambda v1, v2, regs, imm: _fract(v1),
    "SIGN": lambda v1, v2, regs, imm: (0.0 if v1 == 0 else math.copysign(1.0, v1)),
    "CLAMP01": lambda v1, v2, regs, imm: max(0.0, min(1.0, v1)),
    "STEP": lambda v1, v2, regs, imm: (1.0 if v2 >= v1 else 0.0),
    "MIX": lambda v1, v2, regs, imm: _mix(v1, v2, regs[imm & 0xF]),
    "SMOOTHSTEP": lambda v1, v2, regs, imm: _smoothstep(v1, v2, regs[imm & 0xF]),
    "TRUNC": lambda v1, v2, regs, imm: math.trunc(v1),
    "RADIANS": lambda v1, v2, regs, imm: math.radians(v1),
    "DEGREES": lambda v1, v2, regs, imm: math.degrees(v1),
    "SELECT": lambda v1, v2, regs, imm: (v2 if regs[imm & 0xF] > 0.5 else v1),
    "LT": lambda v1, v2, regs, imm: (1.0 if v1 < v2 else 0.0),
    "GT": lambda v1, v2, regs, imm: (1.0 if v1 > v2 else 0.0),
    "LE": lambda v1, v2, regs, imm: (1.0 if v1 <= v2 else 0.0),
    "GE": lambda v1, v2, regs, imm: (1.0 if v1 >= v2 else 0.0),
    "EQ": lambda v1, v2, regs, imm: (1.0 if v1 == v2 else 0.0),
    "NE": lambda v1, v2, regs, imm: (1.0 if v1 != v2 else 0.0),
    "RAND": lambda v1, v2, regs, imm: _fract(math.sin(v1 * 12.9898) * 43758.5453),
    "NOISE": lambda v1, v2, regs, imm: _value_noise_1d(v1),
    "RAND2": lambda v1, v2, regs, imm: _hash2(v1, v2),
    "NOISE2": lambda v1, v2, regs, imm: _value_noise_2d(v1, v2),
}


class ExecutionResult:
    def __init__(self, value: float, steps: int, halt_reason: str):
        self.value = value
        self.steps = steps
        # "ret"        -> executed a RET
        # "fell_off"   -> ran past the function's last instruction
        # "step_limit" -> hit YFSVM_MAX_EXECUTION_STEPS
        self.halt_reason = halt_reason


def run(
    words: list[int],
    func_index: int,
    x: float = 0.0,
    y: float = 0.0,
    t: float = 0.0,
    samplers: list[float] | None = None,
    base: int = 0,
    table: OpcodeTable | None = None,
) -> ExecutionResult:
    if table is None:
        table = load_opcode_table()
    if samplers is None:
        samplers = [0.0] * 8

    if (words[base] & 0xFFFFFFFF) != table.magic:
        return ExecutionResult(0.0, 0, "bad_magic")
    func_count = words[base + 2]
    const_count = words[base + 3]
    if func_index >= func_count:
        return ExecutionResult(0.0, 0, "bad_func_index")

    func_table_offset = base + 4
    packed = words[func_table_offset + func_index]
    func_offset = packed & 0xFFFF
    func_length = packed >> 16

    const_offset = func_table_offset + table.max_functions
    code_offset = const_offset + const_count

    regs = [0.0] * table.max_registers
    pc = func_offset
    end_pc = func_offset + func_length

    name_by_value = table.name_by_value

    for step in range(table.max_execution_steps):
        if pc >= end_pc:
            return ExecutionResult(regs[0], step, "fell_off")

        instruction = words[code_offset + pc]
        pc += 1

        opcode_value = decode_opcode(instruction)
        dst = decode_dst(instruction)
        src1 = decode_src1(instruction)
        src2 = decode_src2(instruction)
        imm = decode_imm12(instruction)
        v1 = regs[src1]
        v2 = regs[src2]

        name = name_by_value.get(opcode_value)
        if name == "RET":
            return ExecutionResult(regs[0], step + 1, "ret")
        if name == "NOP":
            continue
        if name == "JMP":
            pc = func_offset + imm
            continue
        if name == "JMP_IF_ZERO":
            if v1 == 0.0:
                pc = func_offset + imm
            continue
        if name == "JMP_IF_NONZERO":
            if v1 != 0.0:
                pc = func_offset + imm
            continue
        if name == "JMP_IF_LT":
            if v1 < v2:
                pc = func_offset + imm
            continue
        if name == "JMP_IF_GE":
            if v1 >= v2:
                pc = func_offset + imm
            continue
        if name == "LOAD_C":
            regs[dst] = u32_bits_to_float(words[const_offset + imm])
            continue
        if name == "LOAD_X":
            regs[dst] = x
            continue
        if name == "LOAD_Y":
            regs[dst] = y
            continue
        if name == "LOAD_T":
            regs[dst] = t
            continue
        if name == "LOAD_S":
            regs[dst] = samplers[imm & 7]
            continue

        operation = _VALUE_OPS.get(name)
        if operation is not None:
            regs[dst] = operation(v1, v2, regs, imm)
            continue
        # Unknown opcode: match the WGSL default (no-op).

    return ExecutionResult(regs[0], table.max_execution_steps, "step_limit")


def validate_words(words: list[int], table: OpcodeTable | None = None) -> None:
    """Python-side mirror of the C validator (raises CompileError on failure)."""
    from errors import CompileError

    if table is None:
        table = load_opcode_table()
    base = 0
    if (words[base] & 0xFFFFFFFF) != table.magic:
        raise CompileError("validate: bad magic")
    func_count = words[base + 2]
    const_count = words[base + 3]
    if func_count == 0 or func_count > table.max_functions:
        raise CompileError("validate: bad function count")
    if const_count > table.max_constants:
        raise CompileError("validate: too many constants")

    func_table_offset = base + 4
    const_offset = func_table_offset + table.max_functions
    code_offset = const_offset + const_count
    code_count = len(words) - code_offset
    if code_count > table.max_instructions:
        raise CompileError("validate: too many instructions")

    for func_index in range(func_count):
        packed = words[func_table_offset + func_index]
        func_offset = packed & 0xFFFF
        func_length = packed >> 16
        if func_length == 0:
            raise CompileError("validate: empty function")
        # Offset/length are packed into 16 bits each in the function table.
        if func_offset > 0xFFFF or func_length > 0xFFFF:
            raise CompileError("validate: function range too large to pack")
        if func_offset + func_length > code_count:
            raise CompileError("validate: function range out of bounds")
        for pc in range(func_length):
            instruction = words[code_offset + func_offset + pc]
            opcode_value = decode_opcode(instruction)
            name = table.name_by_value.get(opcode_value)
            if name is None:
                raise CompileError("validate: unknown opcode")
            imm = decode_imm12(instruction)
            if name == "LOAD_C" and imm >= const_count:
                raise CompileError("validate: constant index out of range")
            if table.is_branch(name) and imm >= func_length:
                raise CompileError("validate: branch target outside function")
