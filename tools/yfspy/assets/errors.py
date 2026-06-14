"""Compile-time error type for the Python-subset frontend.

Errors carry a source line where available so authors get a clear pointer to
the offending construct rather than a Python traceback.
"""

from __future__ import annotations


class CompileError(Exception):
    def __init__(self, message: str, lineno: int | None = None):
        self.message = message
        self.lineno = lineno
        if lineno is not None:
            super().__init__(f"line {lineno}: {message}")
        else:
            super().__init__(message)
