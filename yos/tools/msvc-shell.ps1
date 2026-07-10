# Load MSVC vcvars (x64) into the current PowerShell session, then exec
# the trailing args. Mirrors build-tools/windows/build.bat probe order.
#
# Usage:
#   .\tools\msvc-shell.ps1 meson setup build-windows
#   .\tools\msvc-shell.ps1 ninja -C build-windows

$ErrorActionPreference = 'Stop'

# PowerShell 5.1's `2>&1` on a native exe wraps each stderr line in an
# ErrorRecord (NativeCommandError), which then trips `Stop` even when the
# command's actual exit code is 0 (ninja prints benign progress like
# "[freebsd-fetch] cached" on stderr). Drop down to `Continue` for the
# duration of the native invocation below so caller-side `2>&1` piping
# survives those benign stderr lines.
$origErrorActionPreference = $ErrorActionPreference

# All args via $args so flags like -C / -G aren't swallowed by PS's
# parameter parser (-C is otherwise treated as a script parameter).
$Cmd = $args

# Already inside a Developer prompt? Skip re-loading.
if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
    if ($Cmd.Count -gt 0) {
        $ErrorActionPreference = 'Continue'
        & $Cmd[0] @($Cmd | Select-Object -Skip 1)
        $ErrorActionPreference = $origErrorActionPreference
        exit $LASTEXITCODE
    }
    return
}

$VcVarsCandidates = @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
)
$VcVars = $null
foreach ($c in $VcVarsCandidates) { if (Test-Path $c) { $VcVars = $c; break } }
if (-not $VcVars) {
    throw "vcvars64.bat not found. Install VS Build Tools 18 or VS 2022."
}

# Run vcvars64 in cmd and re-import the resulting env into this shell.
$Bat = "call `"$VcVars`" >nul 2>nul && set"
$lines = & cmd /c $Bat
foreach ($line in $lines) {
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item "Env:$($matches[1])" $matches[2]
    }
}

# Ensure the wasm-side toolchain (LLVM clang + wasm-ld + Binaryen
# wasm-opt) is on PATH before we hand control over. yos's host build
# uses MSVC; the wasm guest tests use clang -target wasm32 plus
# wasm-opt for the asyncify pass.
$ExtraPathCandidates = @(
    'C:\Program Files\LLVM\bin',
    "$env:LOCALAPPDATA\binaryen-129\binaryen-version_129\bin",
    "$env:LOCALAPPDATA\binaryen\binaryen-version_129\bin"
)
foreach ($p in $ExtraPathCandidates) {
    if ((Test-Path $p) -and ($env:PATH -notmatch [regex]::Escape($p))) {
        $env:PATH = "$p;$env:PATH"
    }
}

if ($Cmd.Count -eq 0) { return }

$ErrorActionPreference = 'Continue'
& $Cmd[0] @($Cmd | Select-Object -Skip 1)
$ErrorActionPreference = $origErrorActionPreference
exit $LASTEXITCODE
