"""Python-subset frontend for yfsvm.

Parses a restricted, deterministic subset of Python with the standard library
`ast` module and lowers it to yfsvm bytecode. This is a shader language with
Python *syntax* — not a Python runtime. Anything that has no clear, allocation-
free, bounded shader meaning (imports, classes, exceptions, generators,
containers, attribute/method calls, inter-function calls) is rejected with a
line number rather than miscompiled.

Each top-level `def` becomes one VM function returning one scalar (`r0`). The
VM selects a function by index at render time, so functions are independent
entry points; they cannot call one another (there is no CALL opcode).

Parameter binding. Each parameter maps to a VM input. A parameter named exactly
`x`, `y`, `t`, `time`, or `s0`..`s7` binds to that input. Any other name binds
positionally to the input sequence `[x, y, time, s0, s1, ..., s7]`. So
`field(x, y, time, s0)` is literal and `mandel_escape(cx, cy)` gets `cx = x`,
`cy = y`.

Registers. `r0` is the return value. Named locals (parameters plus every
assigned name in the function) are pinned to `r1..rL`; expression temporaries
use a stack above them. Python has no block scope, so every assignment in a
`def` lives in one flat local set.
"""

from __future__ import annotations

import ast
import re

import intrinsics
from assembler import FunctionAssembler, Label, ProgramBuilder
from errors import CompileError
from opcodes import OpcodeTable, load_opcode_table

_SAMPLER_NAME = re.compile(r"s([0-7])$")

_BINOP_OPCODE = {
    ast.Add: "ADD",
    ast.Sub: "SUB",
    ast.Mult: "MUL",
    ast.Div: "DIV",
    ast.Mod: "MOD",
    ast.Pow: "POW",
}

_COMPARE_OPCODE = {
    ast.Lt: "LT",
    ast.Gt: "GT",
    ast.LtE: "LE",
    ast.GtE: "GE",
    ast.Eq: "EQ",
    ast.NotEq: "NE",
}


class CompiledProgram:
    def __init__(
        self,
        words: list[int],
        function_names: list[str],
        uses_x: bool,
        uses_y: bool,
        uses_time: bool,
        builder: ProgramBuilder,
    ):
        self.words = words
        self.function_names = function_names
        self.uses_x = uses_x
        self.uses_y = uses_y
        self.uses_time = uses_time
        self.builder = builder


class _LocalCollector(ast.NodeVisitor):
    """Gathers every name assigned anywhere in a function body, in order."""

    def __init__(self):
        self.names: list[str] = []
        self._seen: set[str] = set()

    def _add(self, name: str) -> None:
        if name not in self._seen:
            self._seen.add(name)
            self.names.append(name)

    def visit_Assign(self, node: ast.Assign) -> None:
        for target in node.targets:
            if isinstance(target, ast.Name):
                self._add(target.id)
        self.generic_visit(node)

    def visit_AugAssign(self, node: ast.AugAssign) -> None:
        if isinstance(node.target, ast.Name):
            self._add(node.target.id)
        self.generic_visit(node)

    def visit_For(self, node: ast.For) -> None:
        if isinstance(node.target, ast.Name):
            self._add(node.target.id)
        self.generic_visit(node)

    def visit_FunctionDef(self, node: ast.FunctionDef) -> None:
        # Nested defs are rejected during compilation; do not pull their
        # locals into the enclosing function's register set.
        return


class FunctionCompiler:
    def __init__(
        self,
        builder: ProgramBuilder,
        table: OpcodeTable,
        func_def: ast.FunctionDef,
        function_names: set[str],
    ):
        self.builder = builder
        self.table = table
        self.func_def = func_def
        self.function_names = function_names
        self.assembler = FunctionAssembler(func_def.name)
        self.locals: dict[str, int] = {}
        self.first_temp = 0
        self.temp_used = [False] * table.max_registers
        self.current_lineno = func_def.lineno

    # -- register management ------------------------------------------------

    def alloc_temp(self) -> int:
        for reg in range(self.first_temp, self.table.max_registers):
            if not self.temp_used[reg]:
                self.temp_used[reg] = True
                return reg
        raise CompileError(
            f"out of registers: expression in '{self.func_def.name}' is too complex",
            self.current_lineno,
        )

    def free_temp(self, reg: int) -> None:
        # Named locals live below first_temp and are never freed.
        if reg >= self.first_temp:
            self.temp_used[reg] = False

    # -- emit helpers -------------------------------------------------------

    def emit(self, opcode_name, dst=0, src1=0, src2=0, imm=0) -> None:
        self.assembler.emit(opcode_name, dst, src1, src2, imm)

    def emit_jump(self, target: Label) -> None:
        self.assembler.emit_branch("JMP", target)

    def load_const(self, value: float) -> int:
        index = self.builder.add_constant(value)
        if index >= self.table.max_constants:
            raise CompileError("too many constants", self.current_lineno)
        reg = self.alloc_temp()
        self.emit("LOAD_C", dst=reg, imm=index)
        return reg

    def combine(self, opcode_name: str, left: int, right: int) -> int:
        """left OP right -> fresh temp; frees both operands (if temps)."""
        dst = self.alloc_temp()
        self.emit(opcode_name, dst, left, right)
        self.free_temp(left)
        self.free_temp(right)
        return dst

    def apply(self, opcode_name: str, operand: int) -> int:
        """OP operand -> fresh temp; frees the operand (if temp)."""
        dst = self.alloc_temp()
        self.emit(opcode_name, dst, operand)
        self.free_temp(operand)
        return dst

    # -- top-level function compile ----------------------------------------

    def compile(self) -> None:
        self._reject_signature_extras()
        params = self._parameter_names()

        collector = _LocalCollector()
        for statement in self.func_def.body:
            collector.visit(statement)

        ordered = list(params)
        for name in collector.names:
            if name not in ordered:
                ordered.append(name)

        next_reg = 1
        for name in ordered:
            if next_reg >= self.table.max_registers:
                raise CompileError(
                    f"too many local variables in '{self.func_def.name}' "
                    f"(max {self.table.max_registers - 1})",
                    self.func_def.lineno,
                )
            self.locals[name] = next_reg
            next_reg += 1
        self.first_temp = next_reg

        for position, name in enumerate(params):
            self._emit_parameter_load(name, position)

        for statement in self.func_def.body:
            self.compile_stmt(statement)

        # Fallthrough return: if no explicit return ran, hand back the current
        # r0 (zero unless written). Bounded execution guarantees we reach here.
        self.emit("RET")

    def _reject_signature_extras(self) -> None:
        if self.func_def.decorator_list:
            raise CompileError("decorators are not supported", self.func_def.lineno)
        args = self.func_def.args
        if args.vararg or args.kwarg or args.kwonlyargs or args.kw_defaults or args.defaults:
            raise CompileError(
                "only positional scalar parameters are supported "
                "(no *args, **kwargs, keyword-only, or default arguments)",
                self.func_def.lineno,
            )

    def _parameter_names(self) -> list[str]:
        args = self.func_def.args
        return [arg.arg for arg in (args.posonlyargs + args.args)]

    def _emit_parameter_load(self, name: str, position: int) -> None:
        load_op, sampler = self._input_binding(name, position)
        reg = self.locals[name]
        if load_op == "LOAD_S":
            self.emit("LOAD_S", dst=reg, imm=sampler)
        elif load_op == "LOAD_X":
            self.emit("LOAD_X", dst=reg)
            self.assembler.uses_x = True
        elif load_op == "LOAD_Y":
            self.emit("LOAD_Y", dst=reg)
            self.assembler.uses_y = True
        elif load_op == "LOAD_T":
            self.emit("LOAD_T", dst=reg)
            self.assembler.uses_time = True

    def _input_binding(self, name: str, position: int) -> tuple[str, int | None]:
        if name == "x":
            return ("LOAD_X", None)
        if name == "y":
            return ("LOAD_Y", None)
        if name in ("t", "time"):
            return ("LOAD_T", None)
        sampler_match = _SAMPLER_NAME.fullmatch(name)
        if sampler_match:
            return ("LOAD_S", int(sampler_match.group(1)))

        if position == 0:
            return ("LOAD_X", None)
        if position == 1:
            return ("LOAD_Y", None)
        if position == 2:
            return ("LOAD_T", None)
        sampler = position - 3
        if sampler > 7:
            raise CompileError(
                "too many parameters: the VM provides x, y, time and s0..s7 (max 11)",
                self.func_def.lineno,
            )
        return ("LOAD_S", sampler)

    # -- statements ---------------------------------------------------------

    def compile_stmt(self, node: ast.stmt) -> None:
        self.current_lineno = getattr(node, "lineno", self.current_lineno)
        if isinstance(node, ast.Assign):
            self.compile_assign(node)
        elif isinstance(node, ast.AugAssign):
            self.compile_aug_assign(node)
        elif isinstance(node, ast.Return):
            self.compile_return(node)
        elif isinstance(node, ast.If):
            self.compile_if(node)
        elif isinstance(node, ast.While):
            self.compile_while(node)
        elif isinstance(node, ast.For):
            self.compile_for(node)
        elif isinstance(node, ast.Expr):
            self.compile_expr_stmt(node)
        elif isinstance(node, ast.Pass):
            return
        else:
            raise CompileError(
                f"unsupported statement: {type(node).__name__}", self.current_lineno
            )

    def compile_assign(self, node: ast.Assign) -> None:
        if len(node.targets) != 1:
            raise CompileError("chained assignment is not supported", self.current_lineno)
        target = node.targets[0]
        if not isinstance(target, ast.Name):
            raise CompileError(
                "assignment target must be a simple variable", self.current_lineno
            )
        value_reg = self.compile_expr(node.value)
        self.emit("MOV", dst=self.locals[target.id], src1=value_reg)
        self.free_temp(value_reg)

    def compile_aug_assign(self, node: ast.AugAssign) -> None:
        if not isinstance(node.target, ast.Name):
            raise CompileError(
                "augmented-assignment target must be a simple variable",
                self.current_lineno,
            )
        opcode = _BINOP_OPCODE.get(type(node.op))
        if opcode is None:
            raise CompileError(
                f"unsupported augmented operator: {type(node.op).__name__}",
                self.current_lineno,
            )
        target_reg = self.locals[node.target.id]
        value_reg = self.compile_expr(node.value)
        # target_reg is a local; emit explicitly so it is not freed.
        result = self.alloc_temp()
        self.emit(opcode, result, target_reg, value_reg)
        self.free_temp(value_reg)
        self.emit("MOV", dst=target_reg, src1=result)
        self.free_temp(result)

    def compile_return(self, node: ast.Return) -> None:
        if node.value is not None:
            value_reg = self.compile_expr(node.value)
            self.emit("MOV", dst=0, src1=value_reg)
            self.free_temp(value_reg)
        self.emit("RET")

    def compile_expr_stmt(self, node: ast.Expr) -> None:
        # A bare string (docstring) has no runtime meaning; ignore it.
        if isinstance(node.value, ast.Constant) and isinstance(node.value.value, str):
            return
        value_reg = self.compile_expr(node.value)
        self.free_temp(value_reg)

    def compile_if(self, node: ast.If) -> None:
        else_label = self.assembler.new_label("else")
        self.emit_jump_if_false(node.test, else_label)
        for statement in node.body:
            self.compile_stmt(statement)
        if node.orelse:
            end_label = self.assembler.new_label("endif")
            self.emit_jump(end_label)
            self.assembler.place(else_label)
            for statement in node.orelse:
                self.compile_stmt(statement)
            self.assembler.place(end_label)
        else:
            self.assembler.place(else_label)

    def compile_while(self, node: ast.While) -> None:
        if node.orelse:
            raise CompileError("while-else is not supported", self.current_lineno)
        start_label = self.assembler.new_label("while")
        end_label = self.assembler.new_label("endwhile")
        self.assembler.place(start_label)
        self.emit_jump_if_false(node.test, end_label)
        for statement in node.body:
            self.compile_stmt(statement)
        self.emit_jump(start_label)
        self.assembler.place(end_label)

    def compile_for(self, node: ast.For) -> None:
        if node.orelse:
            raise CompileError("for-else is not supported", self.current_lineno)
        if not isinstance(node.target, ast.Name):
            raise CompileError("for-loop variable must be a simple name", self.current_lineno)
        if not (
            isinstance(node.iter, ast.Call)
            and isinstance(node.iter.func, ast.Name)
            and node.iter.func.id == "range"
        ):
            raise CompileError(
                "for-loops must iterate over range(...)", self.current_lineno
            )

        range_args = node.iter.args
        if not 1 <= len(range_args) <= 3:
            raise CompileError("range() takes 1 to 3 arguments", self.current_lineno)
        values = [self._const_int(argument) for argument in range_args]
        if len(values) == 1:
            start, stop, step = 0, values[0], 1
        elif len(values) == 2:
            start, stop, step = values[0], values[1], 1
        else:
            start, stop, step = values
        if step == 0:
            raise CompileError("range() step must not be zero", self.current_lineno)

        var_reg = self.locals[node.target.id]
        start_reg = self.load_const(float(start))
        self.emit("MOV", dst=var_reg, src1=start_reg)
        self.free_temp(start_reg)

        loop_label = self.assembler.new_label("for")
        end_label = self.assembler.new_label("endfor")
        self.assembler.place(loop_label)

        stop_reg = self.load_const(float(stop))
        if step > 0:
            # continue while var < stop; exit (jump) when var >= stop.
            self.assembler.emit_branch("JMP_IF_GE", end_label, src1=var_reg, src2=stop_reg)
        else:
            # continue while var > stop; exit when var <= stop  (stop >= var).
            self.assembler.emit_branch("JMP_IF_GE", end_label, src1=stop_reg, src2=var_reg)
        self.free_temp(stop_reg)

        for statement in node.body:
            self.compile_stmt(statement)

        step_reg = self.load_const(float(step))
        next_reg = self.alloc_temp()
        self.emit("ADD", next_reg, var_reg, step_reg)
        self.emit("MOV", dst=var_reg, src1=next_reg)
        self.free_temp(step_reg)
        self.free_temp(next_reg)

        self.emit_jump(loop_label)
        self.assembler.place(end_label)

    def _const_int(self, node: ast.expr) -> int:
        if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)) and not isinstance(
            node.value, bool
        ):
            value = node.value
        elif (
            isinstance(node, ast.UnaryOp)
            and isinstance(node.op, ast.USub)
            and isinstance(node.operand, ast.Constant)
            and isinstance(node.operand.value, (int, float))
        ):
            value = -node.operand.value
        else:
            raise CompileError(
                "range() arguments must be constant integers", self.current_lineno
            )
        if value != int(value):
            raise CompileError("range() arguments must be whole numbers", self.current_lineno)
        return int(value)

    # -- conditions ---------------------------------------------------------

    def emit_jump_if_false(self, test: ast.expr, target: Label) -> None:
        """Branch to `target` when `test` evaluates falsey."""
        if isinstance(test, ast.Compare) and len(test.ops) == 1:
            operator = test.ops[0]
            left = test.left
            right = test.comparators[0]
            if isinstance(operator, ast.Lt):
                a, b = self.compile_expr(left), self.compile_expr(right)
                self.assembler.emit_branch("JMP_IF_GE", target, src1=a, src2=b)
                self.free_temp(a)
                self.free_temp(b)
                return
            if isinstance(operator, ast.Gt):
                a, b = self.compile_expr(left), self.compile_expr(right)
                self.assembler.emit_branch("JMP_IF_GE", target, src1=b, src2=a)
                self.free_temp(a)
                self.free_temp(b)
                return
            if isinstance(operator, ast.GtE):
                a, b = self.compile_expr(left), self.compile_expr(right)
                self.assembler.emit_branch("JMP_IF_LT", target, src1=a, src2=b)
                self.free_temp(a)
                self.free_temp(b)
                return
            if isinstance(operator, ast.LtE):
                a, b = self.compile_expr(left), self.compile_expr(right)
                self.assembler.emit_branch("JMP_IF_LT", target, src1=b, src2=a)
                self.free_temp(a)
                self.free_temp(b)
                return

        condition = self.compile_bool(test)
        self.assembler.emit_branch("JMP_IF_ZERO", target, src1=condition)
        self.free_temp(condition)

    def compile_bool(self, node: ast.expr) -> int:
        """Evaluate `node` to a register holding 1.0 (true) or 0.0 (false)."""
        if isinstance(node, ast.Compare):
            return self._compile_compare(node)
        if isinstance(node, ast.BoolOp):
            fold = "MUL" if isinstance(node.op, ast.And) else "MAX"
            accumulator = self.compile_bool(node.values[0])
            for value in node.values[1:]:
                operand = self.compile_bool(value)
                accumulator = self.combine(fold, accumulator, operand)
            return accumulator
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.Not):
            inner = self.compile_bool(node.operand)
            one = self.load_const(1.0)
            return self.combine("SUB", one, inner)
        # Any other expression: truthiness is "value != 0".
        value_reg = self.compile_expr(node)
        zero = self.load_const(0.0)
        return self.combine("NE", value_reg, zero)

    def _compile_compare(self, node: ast.Compare) -> int:
        operands = [node.left, *node.comparators]
        result = None
        for index, operator in enumerate(node.ops):
            pair = self._single_compare(operator, operands[index], operands[index + 1])
            if result is None:
                result = pair
            else:
                result = self.combine("MUL", result, pair)
        return result

    def _single_compare(self, operator, left: ast.expr, right: ast.expr) -> int:
        opcode = _COMPARE_OPCODE.get(type(operator))
        if opcode is None:
            raise CompileError(
                f"unsupported comparison: {type(operator).__name__}", self.current_lineno
            )
        a = self.compile_expr(left)
        b = self.compile_expr(right)
        return self.combine(opcode, a, b)

    # -- expressions --------------------------------------------------------

    def compile_expr(self, node: ast.expr) -> int:
        if isinstance(node, ast.Constant):
            return self._compile_constant(node)
        if isinstance(node, ast.Name):
            return self._resolve_name(node)
        if isinstance(node, ast.BinOp):
            return self._compile_binop(node)
        if isinstance(node, ast.UnaryOp):
            return self._compile_unaryop(node)
        if isinstance(node, ast.Compare):
            return self.compile_bool(node)
        if isinstance(node, ast.BoolOp):
            return self.compile_bool(node)
        if isinstance(node, ast.Call):
            return self.compile_call(node)
        if isinstance(node, ast.IfExp):
            return self._compile_ifexp(node)
        raise CompileError(
            f"unsupported expression: {type(node).__name__}", self.current_lineno
        )

    def _compile_constant(self, node: ast.Constant) -> int:
        if isinstance(node.value, bool):
            return self.load_const(1.0 if node.value else 0.0)
        if isinstance(node.value, (int, float)):
            return self.load_const(float(node.value))
        raise CompileError("only numeric literals are supported", self.current_lineno)

    def _resolve_name(self, node: ast.Name) -> int:
        name = node.id
        if name in self.locals:
            return self.locals[name]
        if name in intrinsics.NAMED_CONSTANTS:
            return self.load_const(intrinsics.NAMED_CONSTANTS[name])
        load_op, sampler = self._bare_input(name)
        if load_op is not None:
            reg = self.alloc_temp()
            if load_op == "LOAD_S":
                self.emit("LOAD_S", dst=reg, imm=sampler)
            else:
                self.emit(load_op, dst=reg)
                if load_op == "LOAD_X":
                    self.assembler.uses_x = True
                elif load_op == "LOAD_Y":
                    self.assembler.uses_y = True
                elif load_op == "LOAD_T":
                    self.assembler.uses_time = True
            return reg
        raise CompileError(f"unknown identifier: {name}", self.current_lineno)

    def _bare_input(self, name: str) -> tuple[str | None, int | None]:
        if name == "x":
            return ("LOAD_X", None)
        if name == "y":
            return ("LOAD_Y", None)
        if name in ("t", "time"):
            return ("LOAD_T", None)
        sampler_match = _SAMPLER_NAME.fullmatch(name)
        if sampler_match:
            return ("LOAD_S", int(sampler_match.group(1)))
        return (None, None)

    def _compile_binop(self, node: ast.BinOp) -> int:
        opcode = _BINOP_OPCODE.get(type(node.op))
        if opcode is None:
            raise CompileError(
                f"unsupported operator: {type(node.op).__name__}", self.current_lineno
            )
        left = self.compile_expr(node.left)
        right = self.compile_expr(node.right)
        return self.combine(opcode, left, right)

    def _compile_unaryop(self, node: ast.UnaryOp) -> int:
        if isinstance(node.op, ast.USub):
            operand = self.compile_expr(node.operand)
            return self.apply("NEG", operand)
        if isinstance(node.op, ast.UAdd):
            return self.compile_expr(node.operand)
        if isinstance(node.op, ast.Not):
            return self.compile_bool(node)
        raise CompileError(
            f"unsupported unary operator: {type(node.op).__name__}", self.current_lineno
        )

    def _compile_ifexp(self, node: ast.IfExp) -> int:
        # `body if test else orelse` -> branchless select. Both arms are pure,
        # so evaluating both is safe and deterministic.
        condition = self.compile_bool(node.test)
        if_true = self.compile_expr(node.body)
        if_false = self.compile_expr(node.orelse)
        dst = self.alloc_temp()
        # SELECT(v1, v2, cond): cond truthy -> v2. Pick if_true when condition.
        self.emit("SELECT", dst, if_false, if_true, imm=condition & 0xF)
        self.free_temp(if_true)
        self.free_temp(if_false)
        self.free_temp(condition)
        return dst

    # -- calls --------------------------------------------------------------

    def compile_call(self, node: ast.Call) -> int:
        if not isinstance(node.func, ast.Name):
            raise CompileError(
                "only direct calls to builtins are supported (no attribute or method calls)",
                self.current_lineno,
            )
        name = node.func.id
        if node.keywords:
            raise CompileError("keyword arguments are not supported", self.current_lineno)
        for argument in node.args:
            if isinstance(argument, ast.Starred):
                raise CompileError("argument unpacking is not supported", self.current_lineno)
        argc = len(node.args)

        if name in self.function_names:
            raise CompileError(
                "the VM has no call instruction: functions are independent entry "
                "points and cannot call one another",
                self.current_lineno,
            )
        if name == "range":
            raise CompileError(
                "range() is only valid as the iterable of a for-loop", self.current_lineno
            )

        if name in intrinsics.MACRO_NAMES:
            return self.compile_macro(name, node.args)
        if name in intrinsics.THREE_ARG:
            return self.compile_three_arg(name, node.args)
        if argc == 1 and name in intrinsics.FUNCS_1ARG:
            operand = self.compile_expr(node.args[0])
            return self.apply(intrinsics.FUNCS_1ARG[name], operand)
        if argc == 2 and name in intrinsics.FUNCS_2ARG:
            left = self.compile_expr(node.args[0])
            right = self.compile_expr(node.args[1])
            return self.combine(intrinsics.FUNCS_2ARG[name], left, right)

        if name in intrinsics.FUNCS_1ARG:
            raise CompileError(f"{name}() takes 1 argument, got {argc}", self.current_lineno)
        if name in intrinsics.FUNCS_2ARG:
            raise CompileError(f"{name}() takes 2 arguments, got {argc}", self.current_lineno)
        raise CompileError(f"unknown function: {name}", self.current_lineno)

    def compile_three_arg(self, name: str, args: list[ast.expr]) -> int:
        if len(args) != 3:
            raise CompileError(f"{name}() takes 3 arguments, got {len(args)}", self.current_lineno)
        first = self.compile_expr(args[0])
        second = self.compile_expr(args[1])
        third = self.compile_expr(args[2])
        dst = self.alloc_temp()
        if name in ("mix", "lerp"):
            self.emit("MIX", dst, first, second, imm=third & 0xF)
        elif name == "smoothstep":
            self.emit("SMOOTHSTEP", dst, first, second, imm=third & 0xF)
        elif name == "select":
            self.emit("SELECT", dst, first, second, imm=third & 0xF)
        elif name == "clamp":
            # clamp(value, low, high) = min(max(value, low), high)
            lower = self.alloc_temp()
            self.emit("MAX", lower, first, second)
            self.emit("MIN", dst, lower, third)
            self.free_temp(lower)
        self.free_temp(first)
        self.free_temp(second)
        self.free_temp(third)
        return dst

    # -- macro builtins -----------------------------------------------------

    def _expect_argc(self, name: str, args: list[ast.expr], count: int) -> None:
        if len(args) != count:
            raise CompileError(
                f"{name}() takes {count} arguments, got {len(args)}", self.current_lineno
            )

    def compile_macro(self, name: str, args: list[ast.expr]) -> int:
        if name == "safe_div":
            return self._macro_safe_div(args)
        if name == "safe_log":
            self._expect_argc(name, args, 1)
            value = self.compile_expr(args[0])
            eps = self.load_const(1e-12)
            guarded = self.combine("MAX", value, eps)
            return self.apply("LOG", guarded)
        if name == "safe_sqrt":
            self._expect_argc(name, args, 1)
            value = self.compile_expr(args[0])
            zero = self.load_const(0.0)
            guarded = self.combine("MAX", value, zero)
            return self.apply("SQRT", guarded)
        if name == "length2":
            self._expect_argc(name, args, 2)
            return self._length2(self.compile_expr(args[0]), self.compile_expr(args[1]))
        if name == "dist2":
            self._expect_argc(name, args, 4)
            ax = self.compile_expr(args[0])
            ay = self.compile_expr(args[1])
            bx = self.compile_expr(args[2])
            by = self.compile_expr(args[3])
            dx = self.combine("SUB", ax, bx)
            dy = self.combine("SUB", ay, by)
            return self._length2(dx, dy)
        if name == "polar_radius":
            self._expect_argc(name, args, 2)
            return self._length2(self.compile_expr(args[0]), self.compile_expr(args[1]))
        if name == "polar_angle":
            self._expect_argc(name, args, 2)
            x = self.compile_expr(args[0])
            y = self.compile_expr(args[1])
            return self.combine("ATAN2", y, x)
        if name == "remap":
            return self._macro_remap(args)
        if name == "remap01":
            return self._macro_remap01(args)
        if name == "checker":
            return self._macro_checker(args)
        if name == "stripe":
            return self._macro_stripe(args)
        if name == "domain_warp_x":
            return self._macro_domain_warp(args, seed_x=0.0, seed_y=0.0, axis="x")
        if name == "domain_warp_y":
            return self._macro_domain_warp(args, seed_x=5.2, seed_y=1.3, axis="y")
        if name in ("fbm2", "ridge2"):
            return self._macro_fbm(name, args)
        raise CompileError(f"unknown macro: {name}", self.current_lineno)

    def _length2(self, x_reg: int, y_reg: int) -> int:
        xx = self.alloc_temp()
        self.emit("MUL", xx, x_reg, x_reg)
        self.free_temp(x_reg)
        yy = self.alloc_temp()
        self.emit("MUL", yy, y_reg, y_reg)
        self.free_temp(y_reg)
        total = self.combine("ADD", xx, yy)
        return self.apply("SQRT", total)

    def _macro_safe_div(self, args: list[ast.expr]) -> int:
        self._expect_argc("safe_div", args, 3)
        numerator = self.compile_expr(args[0])
        denominator = self.compile_expr(args[1])
        fallback = self.compile_expr(args[2])
        quotient = self.alloc_temp()
        self.emit("DIV", quotient, numerator, denominator)
        magnitude = self.alloc_temp()
        self.emit("ABS", magnitude, denominator)
        self.free_temp(numerator)
        self.free_temp(denominator)
        eps = self.load_const(1e-12)
        condition = self.alloc_temp()
        self.emit("GT", condition, magnitude, eps)
        self.free_temp(magnitude)
        self.free_temp(eps)
        dst = self.alloc_temp()
        # condition truthy (|denominator| > eps) -> quotient, else fallback.
        self.emit("SELECT", dst, fallback, quotient, imm=condition & 0xF)
        self.free_temp(quotient)
        self.free_temp(fallback)
        self.free_temp(condition)
        return dst

    def _mul_const(self, reg: int, value: float) -> int:
        """reg * constant -> fresh temp; frees reg (if temp)."""
        constant = self.load_const(value)
        return self.combine("MUL", reg, constant)

    def _macro_remap(self, args: list[ast.expr]) -> int:
        # remap(v, in0, in1, out0, out1) = out0 + (v-in0)*(out1-out0)/(in1-in0)
        # in0 and out0 are each used twice, so they outlive a single combine().
        self._expect_argc("remap", args, 5)
        value = self.compile_expr(args[0])
        in0 = self.compile_expr(args[1])
        in1 = self.compile_expr(args[2])
        out0 = self.compile_expr(args[3])
        out1 = self.compile_expr(args[4])

        numerator = self.alloc_temp()
        self.emit("SUB", numerator, value, in0)  # v - in0  (keep in0)
        self.free_temp(value)
        in_span = self.alloc_temp()
        self.emit("SUB", in_span, in1, in0)  # in1 - in0
        self.free_temp(in1)
        self.free_temp(in0)
        out_span = self.alloc_temp()
        self.emit("SUB", out_span, out1, out0)  # out1 - out0  (keep out0)
        self.free_temp(out1)

        ratio = self.combine("DIV", numerator, in_span)
        scaled = self.combine("MUL", ratio, out_span)
        return self.combine("ADD", out0, scaled)

    def _macro_remap01(self, args: list[ast.expr]) -> int:
        # remap01(v, lo, hi) = clamp((v - lo) / (hi - lo), 0, 1)
        self._expect_argc("remap01", args, 3)
        value = self.compile_expr(args[0])
        low = self.compile_expr(args[1])
        high = self.compile_expr(args[2])
        numerator = self.alloc_temp()
        self.emit("SUB", numerator, value, low)  # v - lo  (keep low)
        self.free_temp(value)
        span = self.alloc_temp()
        self.emit("SUB", span, high, low)  # hi - lo
        self.free_temp(high)
        self.free_temp(low)
        ratio = self.combine("DIV", numerator, span)
        return self.apply("CLAMP01", ratio)

    def _macro_checker(self, args: list[ast.expr]) -> int:
        # checker(x, y, scale) = mod(floor(x*scale) + floor(y*scale), 2)
        self._expect_argc("checker", args, 3)
        x = self.compile_expr(args[0])
        y = self.compile_expr(args[1])
        scale = self.compile_expr(args[2])
        scaled_x = self.alloc_temp()
        self.emit("MUL", scaled_x, x, scale)  # keep scale for y
        self.free_temp(x)
        scaled_y = self.alloc_temp()
        self.emit("MUL", scaled_y, y, scale)
        self.free_temp(y)
        self.free_temp(scale)
        floor_x = self.apply("FLOOR", scaled_x)
        floor_y = self.apply("FLOOR", scaled_y)
        total = self.combine("ADD", floor_x, floor_y)
        two = self.load_const(2.0)
        return self.combine("MOD", total, two)

    def _macro_stripe(self, args: list[ast.expr]) -> int:
        # stripe(x, scale) = step(0.5, fract(x*scale))
        self._expect_argc("stripe", args, 2)
        x = self.compile_expr(args[0])
        scale = self.compile_expr(args[1])
        scaled = self.combine("MUL", x, scale)
        fraction = self.apply("FRACT", scaled)
        edge = self.load_const(0.5)
        # STEP(edge, value) -> value >= edge ? 1 : 0
        return self.combine("STEP", edge, fraction)

    def _macro_domain_warp(self, args, seed_x, seed_y, axis) -> int:
        # domain_warp_<axis>(x, y, amount) = <axis> + amount*noise2(x+seed_x, y+seed_y)
        self._expect_argc("domain_warp_" + axis, args, 3)
        x = self.compile_expr(args[0])
        y = self.compile_expr(args[1])
        amount = self.compile_expr(args[2])
        seed_x_reg = self.load_const(seed_x)
        warp_x = self.alloc_temp()
        self.emit("ADD", warp_x, x, seed_x_reg)  # keep x/y for the axis term
        self.free_temp(seed_x_reg)
        seed_y_reg = self.load_const(seed_y)
        warp_y = self.alloc_temp()
        self.emit("ADD", warp_y, y, seed_y_reg)
        self.free_temp(seed_y_reg)
        noise = self.combine("NOISE2", warp_x, warp_y)
        delta = self.combine("MUL", noise, amount)
        base = x if axis == "x" else y
        other = y if axis == "x" else x
        self.free_temp(other)
        return self.combine("ADD", base, delta)

    def _macro_fbm(self, name, args) -> int:
        # fbm2 / ridge2: sum of value-noise octaves, frequency doubling and
        # amplitude halving each octave, normalized to ~[0,1]. The octave count
        # is a compile-time constant so the loop unrolls to a bounded sequence.
        self._expect_argc(name, args, 3)
        x = self.compile_expr(args[0])
        y = self.compile_expr(args[1])
        octaves = self._const_int(args[2])
        if not 1 <= octaves <= 8:
            raise CompileError(f"{name}() octaves must be between 1 and 8", self.current_lineno)
        ridged = name == "ridge2"

        accumulator = self.load_const(0.0)
        norm = 0.0
        amplitude = 0.5
        frequency = 1.0
        for _octave in range(octaves):
            frequency_reg = self.load_const(frequency)
            sample_x = self.alloc_temp()
            self.emit("MUL", sample_x, x, frequency_reg)  # keep x/y across octaves
            sample_y = self.alloc_temp()
            self.emit("MUL", sample_y, y, frequency_reg)
            self.free_temp(frequency_reg)
            noise = self.combine("NOISE2", sample_x, sample_y)
            if ridged:
                shifted = self._mul_const(noise, 2.0)
                one = self.load_const(1.0)
                centered = self.alloc_temp()
                self.emit("SUB", centered, shifted, one)  # 2*n - 1  (keep one)
                self.free_temp(shifted)
                magnitude = self.apply("ABS", centered)
                noise = self.combine("SUB", one, magnitude)  # 1 - |2n-1|
            contribution = self._mul_const(noise, amplitude)
            accumulator = self.combine("ADD", accumulator, contribution)
            norm += amplitude
            amplitude *= 0.5
            frequency *= 2.0

        self.free_temp(x)
        self.free_temp(y)
        return self._mul_const(accumulator, 1.0 / norm)


def compile_source(source: str) -> CompiledProgram:
    try:
        tree = ast.parse(source)
    except SyntaxError as error:
        raise CompileError(f"syntax error: {error.msg}", error.lineno) from error

    table = load_opcode_table()
    func_defs: list[ast.FunctionDef] = []
    for node in tree.body:
        if isinstance(node, ast.FunctionDef):
            func_defs.append(node)
        elif isinstance(node, ast.Expr) and isinstance(node.value, ast.Constant) and isinstance(
            node.value.value, str
        ):
            continue  # module docstring
        else:
            raise CompileError(
                f"top-level {type(node).__name__} is not allowed; "
                "only function definitions",
                getattr(node, "lineno", None),
            )

    if not func_defs:
        raise CompileError("no function definitions found")
    if len(func_defs) > table.max_functions:
        raise CompileError(f"too many functions (max {table.max_functions})")

    function_names = {fd.name for fd in func_defs}
    builder = ProgramBuilder(table)
    for func_def in func_defs:
        compiler = FunctionCompiler(builder, table, func_def, function_names)
        compiler.compile()
        builder.add_function(compiler.assembler)

    words = builder.serialize()
    uses_x = any(fn.uses_x for fn in builder.functions)
    uses_y = any(fn.uses_y for fn in builder.functions)
    uses_time = any(fn.uses_time for fn in builder.functions)
    return CompiledProgram(
        words, [fd.name for fd in func_defs], uses_x, uses_y, uses_time, builder
    )
