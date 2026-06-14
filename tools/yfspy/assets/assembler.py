"""yfsvm IR assembler.

This is the lowering target the issue calls for: a small IR with symbolic
labels and a shared constant pool, sitting between the frontend and the raw
bytecode. The frontend emits instructions and places labels; a final pass
resolves each branch's label to a function-relative instruction index and packs
everything into the exact word layout the GPU interpreter expects.

Program word layout (matches `yetty_yfsvm_program_serialize` in compiler.c):

    [0] magic
    [1] version
    [2] function_count
    [3] constant_count
    [4 .. 4+MAX_FUNCTIONS)  function table, packed (length << 16 | offset),
                            padded with zeros
    [..constant_count..]    constants, as f32 bit patterns
    [..code..]              instruction words for every function, concatenated

Branch targets are stored as function-relative instruction indices (0 = the
function's first instruction); the interpreter jumps with
`pc = function_offset + target`, so a target never escapes its function.
"""

from __future__ import annotations

from errors import CompileError
from opcodes import OpcodeTable, encode_instruction, float_to_u32_bits


class Label:
    """A jump target. `index` is filled in when the label is placed."""

    def __init__(self, name: str):
        self.name = name
        self.index: int | None = None


class Instruction:
    def __init__(
        self,
        opcode_name: str,
        dst: int = 0,
        src1: int = 0,
        src2: int = 0,
        imm: int = 0,
        target: Label | None = None,
    ):
        self.opcode_name = opcode_name
        self.dst = dst
        self.src1 = src1
        self.src2 = src2
        self.imm = imm
        self.target = target


class FunctionAssembler:
    """Accumulates the instruction stream for one VM function."""

    def __init__(self, name: str):
        self.name = name
        self.instructions: list[Instruction] = []
        self.uses_x = False
        self.uses_y = False
        self.uses_time = False

    def new_label(self, name: str = "L") -> Label:
        return Label(name)

    def place(self, label: Label) -> None:
        """Bind a label to the position of the next instruction to be emitted."""
        label.index = len(self.instructions)

    def emit(
        self,
        opcode_name: str,
        dst: int = 0,
        src1: int = 0,
        src2: int = 0,
        imm: int = 0,
    ) -> None:
        self.instructions.append(Instruction(opcode_name, dst, src1, src2, imm))

    def emit_branch(
        self, opcode_name: str, target: Label, src1: int = 0, src2: int = 0
    ) -> None:
        self.instructions.append(
            Instruction(opcode_name, src1=src1, src2=src2, target=target)
        )


class ProgramBuilder:
    """Owns the shared constant pool and the program's functions."""

    def __init__(self, table: OpcodeTable):
        self.table = table
        self.constants: list[float] = []
        self.functions: list[FunctionAssembler] = []

    def add_constant(self, value: float) -> int:
        value = float(value)
        for index, existing in enumerate(self.constants):
            if existing == value:
                return index
        index = len(self.constants)
        self.constants.append(value)
        return index

    def add_function(self, function: FunctionAssembler) -> None:
        self.functions.append(function)

    def function_index(self, name: str) -> int | None:
        for index, function in enumerate(self.functions):
            if function.name == name:
                return index
        return None

    def _resolve_target(self, function: FunctionAssembler, instruction: Instruction) -> int:
        label = instruction.target
        if label is None or label.index is None:
            raise AssertionError(
                f"unresolved branch label in function '{function.name}'"
            )
        target = label.index
        # The target must address an instruction inside this function and fit
        # the 12-bit immediate it is encoded into. With max_instructions <= 256
        # the latter is implied, but check explicitly so a future limit change
        # cannot silently truncate a jump through imm12.
        if not 0 <= target < len(function.instructions):
            raise CompileError(
                f"branch target {target} outside function '{function.name}' "
                f"(length {len(function.instructions)})"
            )
        if target > 0xFFF:
            raise CompileError(
                f"branch target {target} exceeds the 12-bit immediate limit (4095)"
            )
        return target

    def _check_limits(self) -> None:
        """Reject programs that violate the VM's declared limits before they are
        serialized — an over-limit blob the C model cannot represent (or would
        reject) must never leave this compiler silently."""
        table = self.table
        if len(self.functions) > table.max_functions:
            raise CompileError(
                f"too many functions: {len(self.functions)} > limit {table.max_functions}"
            )
        if len(self.constants) > table.max_constants:
            raise CompileError(
                f"too many constants: {len(self.constants)} > limit {table.max_constants}"
            )
        total_instructions = sum(len(function.instructions) for function in self.functions)
        if total_instructions > table.max_instructions:
            raise CompileError(
                f"program too large: {total_instructions} instructions > "
                f"limit {table.max_instructions}"
            )
        running_offset = 0
        for function in self.functions:
            length = len(function.instructions)
            if length == 0:
                raise CompileError(f"empty function '{function.name}'")
            # Offset and length are packed into 16 bits each in the function table.
            if running_offset > 0xFFFF or length > 0xFFFF:
                raise CompileError(
                    f"function '{function.name}' range exceeds 16-bit packing "
                    f"(offset {running_offset}, length {length})"
                )
            running_offset += length

    def serialize(self) -> list[int]:
        table = self.table
        self._check_limits()
        words: list[int] = []
        words.append(table.magic)
        words.append(table.version)
        words.append(len(self.functions))
        words.append(len(self.constants))

        # Function table: each function's offset is its start in the
        # concatenated code segment; length is its instruction count.
        running_offset = 0
        offsets: list[tuple[int, int]] = []
        for function in self.functions:
            offsets.append((running_offset, len(function.instructions)))
            running_offset += len(function.instructions)

        for slot in range(table.max_functions):
            if slot < len(offsets):
                offset, length = offsets[slot]
                words.append(((length & 0xFFFF) << 16) | (offset & 0xFFFF))
            else:
                words.append(0)

        for value in self.constants:
            words.append(float_to_u32_bits(value))

        for function in self.functions:
            for instruction in function.instructions:
                if instruction.target is not None:
                    imm = self._resolve_target(function, instruction)
                else:
                    imm = instruction.imm
                words.append(
                    encode_instruction(
                        table.value(instruction.opcode_name),
                        instruction.dst,
                        instruction.src1,
                        instruction.src2,
                        imm,
                    )
                )
        return words
