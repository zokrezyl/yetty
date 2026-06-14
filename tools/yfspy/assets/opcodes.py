"""yfsvm opcode table — loaded from the single source of truth.

The C header (`yfsvm.gen.h`) and the WGSL interpreter (`yfsvm.gen.wgsl`) are both
generated from `yfsvm-opcodes.yaml`. This module loads the same YAML so the
Python frontend, the reference interpreter, and the GPU stay in lock-step: add
an opcode in one place and every consumer sees it.
"""

from __future__ import annotations

import os
import struct
from pathlib import Path

import yaml


def _find_opcodes_yaml() -> Path:
    """Locate the opcode definition file — the single source of truth shared
    with the C/WGSL codegen.

    The compiler ships with the yfspy tool (tools/yfspy/assets/), so the YAML
    may sit either next to it (a self-contained bundle) or back in the source
    tree at src/yetty/yfsvm/ (dev checkout). An explicit override wins for
    unusual layouts.
    """
    override = os.environ.get("YFSVM_OPCODES_YAML")
    if override:
        return Path(override)

    here = Path(__file__).resolve().parent
    candidates = [
        here / "yfsvm-opcodes.yaml",  # bundled alongside the compiler
        # dev checkout: tools/yfspy/assets/ -> repo root -> src/yetty/yfsvm/
        here.parent.parent.parent / "src" / "yetty" / "yfsvm" / "yfsvm-opcodes.yaml",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(
        "yfsvm-opcodes.yaml not found next to the compiler or in the source "
        "tree; set YFSVM_OPCODES_YAML to its path"
    )


_YAML_PATH = _find_opcodes_yaml()


class OpcodeTable:
    """Parsed view of the opcode definition file."""

    def __init__(self, config: dict):
        self.magic = int(config["magic"])
        self.version = int(config["version"])
        self.max_registers = int(config["max_registers"])
        self.max_functions = int(config["max_functions"])
        self.max_constants = int(config["max_constants"])
        self.max_instructions = int(config["max_instructions"])
        self.max_execution_steps = int(config["max_execution_steps"])

        self.value_by_name: dict[str, int] = {}
        self.name_by_value: dict[int, str] = {}
        self.arity_by_name: dict[str, int] = {}
        self.branch_names: set[str] = set()

        for entry in config["opcodes"]:
            name = entry["name"]
            value = int(entry["value"])
            self.value_by_name[name] = value
            self.name_by_value[value] = name
            self.arity_by_name[name] = int(entry.get("arity", 0))
            if entry.get("branch"):
                self.branch_names.add(name)

    def value(self, name: str) -> int:
        return self.value_by_name[name]

    def is_branch(self, name: str) -> bool:
        return name in self.branch_names

    @property
    def opcode_names(self) -> set[str]:
        return set(self.value_by_name.keys())


def load_opcode_table() -> OpcodeTable:
    with open(_YAML_PATH) as stream:
        return OpcodeTable(yaml.safe_load(stream))


def encode_instruction(opcode_value: int, dst: int, src1: int, src2: int, imm12: int) -> int:
    """Pack a 32-bit instruction word, matching `yfsvm_encode` in the C header."""
    return (
        ((opcode_value & 0xFF) << 24)
        | ((dst & 0xF) << 20)
        | ((src1 & 0xF) << 16)
        | ((src2 & 0xF) << 12)
        | (imm12 & 0xFFF)
    )


def decode_opcode(instruction: int) -> int:
    return (instruction >> 24) & 0xFF


def decode_dst(instruction: int) -> int:
    return (instruction >> 20) & 0xF


def decode_src1(instruction: int) -> int:
    return (instruction >> 16) & 0xF


def decode_src2(instruction: int) -> int:
    return (instruction >> 12) & 0xF


def decode_imm12(instruction: int) -> int:
    return instruction & 0xFFF


def float_to_u32_bits(value: float) -> int:
    """IEEE-754 bit pattern of a 32-bit float — what the GPU reads via bitcast."""
    return struct.unpack("<I", struct.pack("<f", value))[0]


def u32_bits_to_float(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]
