#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyyaml"]
# ///
"""Test suite for the yfsvm Python-subset frontend and reference interpreter.

Covers the behaviours the issue calls out: yexpr/math parity, branch
correctness, `while`/`for` lowering, loop-timeout semantics, an escape-time
fractal, the macro builtins, rejection of unsupported Python, and a drift guard
that the interpreter implements every opcode in the YAML.

Run:  uv run src/yetty/yfsvm/pyc/run_tests.py
"""

from __future__ import annotations

import math
import sys

import interp
from assembler import FunctionAssembler, ProgramBuilder
from errors import CompileError
from frontend import compile_source
from opcodes import load_opcode_table

_TABLE = load_opcode_table()
_PASSED = 0
_FAILED = 0


def check(name: str, condition: bool, detail: str = "") -> None:
    global _PASSED, _FAILED
    if condition:
        _PASSED += 1
        print(f"  ok   {name}")
    else:
        _FAILED += 1
        print(f"  FAIL {name}: {detail}")


def evaluate(source: str, func=0, x=0.0, y=0.0, t=0.0, samplers=None):
    program = compile_source(source)
    index = func if isinstance(func, int) else program.function_names.index(func)
    return interp.run(program.words, index, x=x, y=y, t=t, samplers=samplers, table=_TABLE)


def close(a: float, b: float, tol: float = 1e-6) -> bool:
    return abs(a - b) <= tol


# ---------------------------------------------------------------------------


def test_expression_parity():
    source = "def f(x):\n    return sin(x * 2.0) + 1.0\n"
    for sample in (-2.0, -0.5, 0.0, 0.5, 1.3, 3.0):
        result = evaluate(source, x=sample)
        expected = math.sin(sample * 2.0) + 1.0
        check(f"parity sin@{sample}", close(result.value, expected), f"{result.value} != {expected}")


def test_named_constants_and_calls():
    source = "def f(x):\n    return atan2(sin(pi), cos(0.0)) + sqrt(16.0)\n"
    result = evaluate(source)
    expected = math.atan2(math.sin(math.pi), math.cos(0.0)) + math.sqrt(16.0)
    check("named constants + 2-arg call", close(result.value, expected))


def test_if_else():
    source = "def pick(x, y):\n    if x > y:\n        return 1.0\n    return 0.0\n"
    check("if true branch", close(evaluate(source, x=2.0, y=1.0).value, 1.0))
    check("if false branch", close(evaluate(source, x=1.0, y=2.0).value, 0.0))


def test_ifexp_ternary():
    source = "def f(x):\n    return 1.0 if x > 0.0 else -1.0\n"
    check("ternary positive", close(evaluate(source, x=2.0).value, 1.0))
    check("ternary negative", close(evaluate(source, x=-2.0).value, -1.0))


def test_boolean_ops():
    both = "def f(x, y):\n    if x > 0.0 and y > 0.0:\n        return 1.0\n    return 0.0\n"
    check("and both", close(evaluate(both, x=1.0, y=1.0).value, 1.0))
    check("and one", close(evaluate(both, x=1.0, y=-1.0).value, 0.0))

    either = "def f(x, y):\n    if x > 0.0 or y > 0.0:\n        return 1.0\n    return 0.0\n"
    check("or one", close(evaluate(either, x=-1.0, y=1.0).value, 1.0))
    check("or none", close(evaluate(either, x=-1.0, y=-1.0).value, 0.0))

    negate = "def f(x):\n    if not (x > 0.0):\n        return 1.0\n    return 0.0\n"
    check("not true", close(evaluate(negate, x=-1.0).value, 1.0))
    check("not false", close(evaluate(negate, x=1.0).value, 0.0))


def test_while_sum():
    source = (
        "def f(x):\n"
        "    s = 0.0\n"
        "    i = 0.0\n"
        "    while i < 5.0:\n"
        "        s = s + i\n"
        "        i = i + 1.0\n"
        "    return s\n"
    )
    check("while sum", close(evaluate(source).value, 10.0))


def test_for_ranges():
    one = "def f(x):\n    s = 0.0\n    for i in range(4):\n        s = s + i\n    return s\n"
    check("for range(4)", close(evaluate(one).value, 6.0))

    stepped = "def f(x):\n    s = 0.0\n    for i in range(2, 10, 2):\n        s = s + i\n    return s\n"
    check("for range(2,10,2)", close(evaluate(stepped).value, 20.0))

    down = "def f(x):\n    s = 0.0\n    for i in range(3, 0, -1):\n        s = s + i\n    return s\n"
    check("for range(3,0,-1)", close(evaluate(down).value, 6.0))


def test_aug_assign():
    source = "def f(x):\n    s = 0.0\n    s += 3.0\n    s *= 2.0\n    return s\n"
    check("augmented assignment", close(evaluate(source).value, 6.0))


def test_loop_timeout():
    source = (
        "def f(x):\n"
        "    i = 0.0\n"
        "    while i < 1000000000.0:\n"
        "        i = i + 1.0\n"
        "    return i\n"
    )
    result = evaluate(source)
    # Defined timeout semantics: evaluation stops at the step limit and returns
    # the current r0. Here the loop only ever wrote the local i (not the return
    # register), so r0 is still its initial 0.0. The key guarantee is that the
    # non-terminating loop is bounded — it stops instead of hanging.
    check("loop hits step limit", result.halt_reason == "step_limit", result.halt_reason)
    check("loop bounded at step cap", result.steps == _TABLE.max_execution_steps, str(result.steps))
    check("timeout returns current r0", result.value == 0.0, str(result.value))


def _mandel_reference(cx: float, cy: float) -> float:
    zx = zy = i = 0.0
    while i < 128.0 and zx * zx + zy * zy < 4.0:
        xt = zx * zx - zy * zy + cx
        zy = 2.0 * zx * zy + cy
        zx = xt
        i = i + 1.0
    return i / 128.0


def test_mandelbrot():
    source = (
        "def mandel(cx, cy):\n"
        "    zx = 0.0\n"
        "    zy = 0.0\n"
        "    i = 0.0\n"
        "    while i < 128.0 and zx*zx + zy*zy < 4.0:\n"
        "        xt = zx*zx - zy*zy + cx\n"
        "        zy = 2.0*zx*zy + cy\n"
        "        zx = xt\n"
        "        i = i + 1.0\n"
        "    return i / 128.0\n"
    )
    # cx,cy bind positionally to (x, y).
    inside = evaluate(source, x=0.0, y=0.0)
    check("mandel inside completes", inside.halt_reason == "ret", inside.halt_reason)
    check("mandel inside == 1.0", close(inside.value, 1.0))

    for cx in (-2.0, -1.0, -0.5, 0.0, 0.25, 0.5, 1.0):
        for cy in (-1.0, -0.3, 0.0, 0.3, 1.0):
            got = evaluate(source, x=cx, y=cy).value
            expected = _mandel_reference(cx, cy)
            check(f"mandel({cx},{cy})", close(got, expected), f"{got} != {expected}")


def test_macros():
    check("length2(3,4)", close(evaluate("def f(x, y):\n    return length2(x, y)\n", x=3.0, y=4.0).value, 5.0))
    check("dist2", close(evaluate("def f(x, y):\n    return dist2(0.0, 0.0, 3.0, 4.0)\n").value, 5.0))

    remap01 = "def f(x):\n    return remap01(x, 0.0, 10.0)\n"
    check("remap01 mid", close(evaluate(remap01, x=5.0).value, 0.5))
    check("remap01 below", close(evaluate(remap01, x=-5.0).value, 0.0))
    check("remap01 above", close(evaluate(remap01, x=15.0).value, 1.0))

    remap = "def f(x):\n    return remap(x, 0.0, 1.0, 10.0, 20.0)\n"
    check("remap", close(evaluate(remap, x=0.5).value, 15.0))

    safe = "def f(x):\n    return safe_div(1.0, x, -1.0)\n"
    check("safe_div by zero", close(evaluate(safe, x=0.0).value, -1.0))
    check("safe_div normal", close(evaluate(safe, x=2.0).value, 0.5))

    polar = "def f(x, y):\n    return polar_angle(x, y)\n"
    check("polar_angle", close(evaluate(polar, x=1.0, y=1.0).value, math.atan2(1.0, 1.0)))


def test_fbm_matches_manual():
    source = "def f(x, y):\n    return fbm2(x, y, 4)\n"
    got = evaluate(source, x=0.37, y=0.81).value
    # Recreate the exact octave schedule the macro emits.
    total = 0.0
    norm = 0.0
    amplitude = 0.5
    frequency = 1.0
    for _ in range(4):
        total += amplitude * interp._value_noise_2d(0.37 * frequency, 0.81 * frequency)
        norm += amplitude
        amplitude *= 0.5
        frequency *= 2.0
    expected = total / norm
    check("fbm2 matches manual octave sum", close(got, expected), f"{got} != {expected}")


def test_hand_assembled_jump_if_nonzero():
    # The frontend doesn't emit JMP_IF_NONZERO; assemble one directly to cover
    # the opcode in both the assembler and the interpreter.
    builder = ProgramBuilder(_TABLE)
    function = FunctionAssembler("nz")
    zero_index = builder.add_constant(0.0)
    one_index = builder.add_constant(1.0)
    function.emit("LOAD_X", dst=1)
    nonzero_label = function.new_label("nz")
    function.emit_branch("JMP_IF_NONZERO", nonzero_label, src1=1)
    function.emit("LOAD_C", dst=0, imm=zero_index)
    function.emit("RET")
    function.place(nonzero_label)
    function.emit("LOAD_C", dst=0, imm=one_index)
    function.emit("RET")
    builder.add_function(function)
    words = builder.serialize()
    interp.validate_words(words, _TABLE)
    check("jnz x=0 -> 0", close(interp.run(words, 0, x=0.0, table=_TABLE).value, 0.0))
    check("jnz x=5 -> 1", close(interp.run(words, 0, x=5.0, table=_TABLE).value, 1.0))


def test_validation():
    source = "def f(x):\n    return x\n"
    program = compile_source(source)
    interp.validate_words(program.words, _TABLE)  # must not raise

    bad_magic = list(program.words)
    bad_magic[0] = 0
    raised = False
    try:
        interp.validate_words(bad_magic, _TABLE)
    except CompileError:
        raised = True
    check("validate rejects bad magic", raised)

    # Corrupt a branch target in a looping program.
    loop = compile_source(
        "def f(x):\n    i = 0.0\n    while i < 3.0:\n        i = i + 1.0\n    return i\n"
    )
    words = list(loop.words)
    func_count = words[2]
    const_count = words[3]
    code_offset = 4 + _TABLE.max_functions + const_count
    packed = words[4]
    func_offset = packed & 0xFFFF
    func_length = packed >> 16
    from opcodes import decode_opcode, encode_instruction, decode_dst, decode_src1, decode_src2

    corrupted = None
    for pc in range(func_length):
        position = code_offset + func_offset + pc
        opcode_value = decode_opcode(words[position])
        name = _TABLE.name_by_value.get(opcode_value)
        if name and _TABLE.is_branch(name):
            words[position] = encode_instruction(
                opcode_value,
                decode_dst(words[position]),
                decode_src1(words[position]),
                decode_src2(words[position]),
                func_length + 10,  # target past the end
            )
            corrupted = name
            break
    check("found a branch to corrupt", corrupted is not None)
    raised = False
    try:
        interp.validate_words(words, _TABLE)
    except CompileError:
        raised = True
    check("validate rejects out-of-range branch", raised)


def test_rejections():
    cases = [
        ("import os\n", "import"),
        ("class Foo:\n    pass\n", "class"),
        ("async def f(x):\n    return x\n", "async"),
        ("def f(x):\n    return [1.0, 2.0]\n", "list"),
        ("def f(x):\n    return {1: 2}\n", "dict"),
        ("def f(x):\n    return x.real\n", "attribute"),
        ("def f(x):\n    try:\n        return x\n    except Exception:\n        return 0.0\n", "try"),
        ("def f(x):\n    return (i for i in range(3))\n", "generator"),
        ("def a(x):\n    return 1.0\ndef b(x):\n    return a(x)\n", "inter-function call"),
        ("def f(x):\n    return q\n", "unknown identifier"),
        ("def f(x):\n    return range(3)\n", "range outside for"),
        ("def f(x):\n    return floordiv(x)\n", "unknown function"),
        ("def f(x, *args):\n    return x\n", "varargs"),
    ]
    for source, label in cases:
        raised = False
        message = ""
        try:
            compile_source(source)
        except CompileError as error:
            raised = True
            message = str(error)
        check(f"reject {label}", raised, f"did not raise (msg={message})")


def test_limits():
    from opcodes import encode_instruction

    # 1. A program exceeding the instruction limit must be refused at compile
    # time, not silently serialized into a blob the C model cannot represent.
    lines = ["def big(x):", "    s = 0.0"]
    for octave in range(200):
        lines.append(f"    s = s + sin(x + {float(octave)})")
    lines.append("    return s")
    source = "\n".join(lines) + "\n"
    raised = False
    message = ""
    try:
        compile_source(source)
    except CompileError as error:
        raised = True
        message = str(error)
    check("oversized program rejected at compile", raised, message)
    check("oversized error names the instruction limit", "instruction" in message.lower(), message)

    # 2. validate_words rejects an oversized hand-built blob directly.
    count = _TABLE.max_instructions + 1
    oversized = [_TABLE.magic, _TABLE.version, 1, 0]
    oversized.append(((count & 0xFFFF) << 16) | 0)
    oversized.extend([0] * (_TABLE.max_functions - 1))
    ret = encode_instruction(_TABLE.value("RET"), 0, 0, 0, 0)
    oversized.extend([ret] * count)
    raised = False
    try:
        interp.validate_words(oversized, _TABLE)
    except CompileError:
        raised = True
    check("validate_words rejects too many instructions", raised)

    # 3. validate_words rejects too many constants.
    too_many = _TABLE.max_constants + 1
    const_blob = [_TABLE.magic, _TABLE.version, 1, too_many]
    const_blob.append(((1 & 0xFFFF) << 16) | 0)
    const_blob.extend([0] * (_TABLE.max_functions - 1))
    const_blob.extend([0] * too_many)
    const_blob.append(ret)
    raised = False
    try:
        interp.validate_words(const_blob, _TABLE)
    except CompileError:
        raised = True
    check("validate_words rejects too many constants", raised)

    # 4. A program right at the limit still serializes and validates.
    head = ["def ok(x):", "    s = 0.0"]
    # Each "s = s + x" line is a small, fixed number of instructions; keep the
    # body comfortably under the cap.
    for _ in range(20):
        head.append("    s = s + x")
    head.append("    return s")
    program = compile_source("\n".join(head) + "\n")
    interp.validate_words(program.words, _TABLE)
    check("under-limit program validates", True)


def test_opcode_coverage():
    handled = {"NOP", "RET", "JMP", "JMP_IF_ZERO", "JMP_IF_NONZERO", "JMP_IF_LT", "JMP_IF_GE"}
    handled |= set(interp._VALUE_OPS.keys())
    missing = _TABLE.opcode_names - handled
    extra = handled - _TABLE.opcode_names
    check("interpreter covers every opcode", not missing and not extra, f"missing={missing} extra={extra}")


def main() -> int:
    tests = [
        test_expression_parity,
        test_named_constants_and_calls,
        test_if_else,
        test_ifexp_ternary,
        test_boolean_ops,
        test_while_sum,
        test_for_ranges,
        test_aug_assign,
        test_loop_timeout,
        test_mandelbrot,
        test_macros,
        test_fbm_matches_manual,
        test_hand_assembled_jump_if_nonzero,
        test_validation,
        test_rejections,
        test_limits,
        test_opcode_coverage,
    ]
    for test in tests:
        print(test.__name__)
        test()
    print(f"\n{_PASSED} passed, {_FAILED} failed")
    return 1 if _FAILED else 0


if __name__ == "__main__":
    sys.exit(main())
