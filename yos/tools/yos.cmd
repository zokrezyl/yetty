@echo off
REM yos.cmd - Windows-native launcher for yos.exe. Pure cmd batch;
REM does NOT rely on PowerShell, so it works regardless of your
REM PowerShell execution policy.
REM
REM Usage:
REM   .\tools\yos.cmd <path-to-something.wasm> [args...]
REM   .\tools\yos.cmd --help                          (forwarded to yos.exe)
REM
REM yos.exe location: %YOS_BIN% if set, otherwise the first existing
REM   build-windows-debug\src\yos\yos.exe
REM   build-windows-release\src\yos\yos.exe
REM   build-windows\src\yos\yos.exe
REM (relative to this script's parent directory).

setlocal

set "REPO=%~dp0.."

if defined YOS_BIN goto :have_yos
set "YOS_BIN=%REPO%\build-windows-debug\src\yos\yos.exe"
if exist "%YOS_BIN%" goto :have_yos
set "YOS_BIN=%REPO%\build-windows-release\src\yos\yos.exe"
if exist "%YOS_BIN%" goto :have_yos
set "YOS_BIN=%REPO%\build-windows\src\yos\yos.exe"
if exist "%YOS_BIN%" goto :have_yos
echo yos.cmd: yos.exe not found. Build with: build-tools\windows\build.bat 1>&2
exit /b 1

:have_yos
"%YOS_BIN%" %*
exit /b %ERRORLEVEL%
