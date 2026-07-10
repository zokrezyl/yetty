@echo off
REM Windows wrapper around wasm-clang.py. Forwards every arg.
setlocal
if exist "%~dp0..\.venv\Scripts\python.exe" (
    set "PY=%~dp0..\.venv\Scripts\python.exe"
) else (
    set "PY=python"
)
"%PY%" "%~dp0wasm-clang.py" %*
exit /b %errorlevel%
