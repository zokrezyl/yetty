#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyyaml"]
# ///
"""Command-line front door for the yfsvm Python-subset compiler.

Usage:
  cli.py compile  SOURCE [-o OUT.bin] [--hex] [--c-array NAME]
  cli.py run      SOURCE [--func NAME|INDEX] [--x V --y V --time V --s0 V ...]
  cli.py dump     SOURCE          # human-readable disassembly

`compile` lowers a Python-subset source file to yfsvm bytecode in the exact
word layout the GPU interpreter consumes. `run` additionally evaluates a
function with the reference CPU interpreter — handy for sanity checks. `dump`
prints the assembled instructions.
"""

from __future__ import annotations

import argparse
import struct
import sys

from errors import CompileError
from frontend import compile_source
from interp import run as interpret
from interp import validate_words
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


def _resolve_function_index(program, selector: str | None) -> int:
    if selector is None:
        return 0
    if selector in program.function_names:
        return program.function_names.index(selector)
    try:
        index = int(selector)
    except ValueError:
        raise SystemExit(f"unknown function: {selector}")
    if not 0 <= index < len(program.function_names):
        raise SystemExit(f"function index out of range: {index}")
    return index


def disassemble(words: list[int], table: OpcodeTable) -> str:
    func_count = words[2]
    const_count = words[3]
    func_table_offset = 4
    const_offset = func_table_offset + table.max_functions
    code_offset = const_offset + const_count

    lines: list[str] = []
    lines.append(f"magic=0x{words[0]:08X} version={words[1]} functions={func_count} constants={const_count}")
    if const_count:
        constants = [u32_bits_to_float(words[const_offset + i]) for i in range(const_count)]
        lines.append("constants: " + ", ".join(f"[{i}]={value:g}" for i, value in enumerate(constants)))

    for func_index in range(func_count):
        packed = words[func_table_offset + func_index]
        offset = packed & 0xFFFF
        length = packed >> 16
        lines.append(f"function {func_index}: offset={offset} length={length}")
        for pc in range(length):
            instruction = words[code_offset + offset + pc]
            opcode_value = decode_opcode(instruction)
            name = table.name_by_value.get(opcode_value, f"0x{opcode_value:02X}")
            dst = decode_dst(instruction)
            src1 = decode_src1(instruction)
            src2 = decode_src2(instruction)
            imm = decode_imm12(instruction)
            if table.is_branch(name):
                operands = f"-> {imm}"
                if name in ("JMP_IF_ZERO", "JMP_IF_NONZERO"):
                    operands += f"  if r{src1}"
                elif name in ("JMP_IF_LT", "JMP_IF_GE"):
                    operands += f"  r{src1}, r{src2}"
            elif name == "LOAD_C":
                operands = f"r{dst} <- const[{imm}]"
            elif name in ("LOAD_X", "LOAD_Y", "LOAD_T"):
                operands = f"r{dst}"
            elif name == "LOAD_S":
                operands = f"r{dst} <- s{imm & 7}"
            elif name in ("MIX", "SMOOTHSTEP", "SELECT"):
                operands = f"r{dst} <- r{src1}, r{src2}, r{imm & 0xF}"
            else:
                operands = f"r{dst} <- r{src1}, r{src2}"
            lines.append(f"  {pc:3d}: {name:<14} {operands}")
    return "\n".join(lines)


def cmd_compile(args: argparse.Namespace) -> None:
    source = open(args.source).read()
    program = compile_source(source)
    table = program.builder.table
    validate_words(program.words, table)

    if args.out:
        packed = struct.pack(f"<{len(program.words)}I", *program.words)
        if args.out == "-":
            # Raw little-endian u32 stream to stdout — for piping into a
            # renderer (e.g. tools/yfspy). Keep stdout binary-clean.
            sys.stdout.buffer.write(packed)
            sys.stdout.buffer.flush()
        else:
            with open(args.out, "wb") as stream:
                stream.write(packed)
            print(
                f"wrote {len(program.words)} words ({len(program.words) * 4} bytes) to {args.out}",
                file=sys.stderr,
            )
    if args.c_array:
        print(f"static const uint32_t {args.c_array}[] = {{")
        for index in range(0, len(program.words), 8):
            chunk = program.words[index : index + 8]
            print("    " + ", ".join(f"0x{word:08X}u" for word in chunk) + ",")
        print("};")
    if args.hex or (not args.out and not args.c_array):
        print(" ".join(f"{word:08X}" for word in program.words))


def cmd_run(args: argparse.Namespace) -> None:
    source = open(args.source).read()
    program = compile_source(source)
    table = program.builder.table
    func_index = _resolve_function_index(program, args.func)
    samplers = [getattr(args, f"s{i}") for i in range(8)]
    result = interpret(
        program.words,
        func_index,
        x=args.x,
        y=args.y,
        t=args.time,
        samplers=samplers,
        table=table,
    )
    name = program.function_names[func_index]
    print(f"{name}(x={args.x}, y={args.y}, time={args.time}) = {result.value!r}")
    print(f"  steps={result.steps} halt={result.halt_reason}", file=sys.stderr)


def cmd_dump(args: argparse.Namespace) -> None:
    source = open(args.source).read()
    program = compile_source(source)
    print(disassemble(program.words, program.builder.table))


def main() -> None:
    parser = argparse.ArgumentParser(description="yfsvm Python-subset compiler")
    subparsers = parser.add_subparsers(dest="command", required=True)

    compile_parser = subparsers.add_parser("compile", help="compile to bytecode")
    compile_parser.add_argument("source")
    compile_parser.add_argument("-o", "--out", help="write little-endian u32 binary")
    compile_parser.add_argument("--hex", action="store_true", help="print hex words")
    compile_parser.add_argument("--c-array", metavar="NAME", help="print a C uint32_t array")
    compile_parser.set_defaults(handler=cmd_compile)

    run_parser = subparsers.add_parser("run", help="compile and evaluate")
    run_parser.add_argument("source")
    run_parser.add_argument("--func", help="function name or index (default 0)")
    run_parser.add_argument("--x", type=float, default=0.0)
    run_parser.add_argument("--y", type=float, default=0.0)
    run_parser.add_argument("--time", type=float, default=0.0)
    for sampler_index in range(8):
        run_parser.add_argument(f"--s{sampler_index}", type=float, default=0.0)
    run_parser.set_defaults(handler=cmd_run)

    dump_parser = subparsers.add_parser("dump", help="disassemble")
    dump_parser.add_argument("source")
    dump_parser.set_defaults(handler=cmd_dump)

    args = parser.parse_args()
    try:
        args.handler(args)
    except CompileError as error:
        raise SystemExit(f"compile error: {error}")


if __name__ == "__main__":
    main()
