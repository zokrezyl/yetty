@echo off
REM yos Windows build driver. Pure MSVC + Ninja.
REM
REM Usage from project root:
REM   build-tools\windows\build.bat                 (debug, configure+build)
REM   build-tools\windows\build.bat release         (release, configure+build)
REM   build-tools\windows\build.bat clean           (wipe build dir)
REM   build-tools\windows\build.bat configure       (configure only)

setlocal enabledelayedexpansion

set CONFIG=debug
set CLEAN=0
set CONFIGURE_ONLY=0

:parse_args
if "%1"=="" goto :start
if /i "%1"=="debug" ( set CONFIG=debug & shift & goto :parse_args )
if /i "%1"=="release" ( set CONFIG=release & shift & goto :parse_args )
if /i "%1"=="clean" ( set CLEAN=1 & shift & goto :parse_args )
if /i "%1"=="configure" ( set CONFIGURE_ONLY=1 & shift & goto :parse_args )
shift
goto :parse_args

:start
set PROJECT_ROOT=%CD%
set BUILD_DIR=%PROJECT_ROOT%\build-windows-%CONFIG%

echo yos Windows build
echo   Project Root: %PROJECT_ROOT%
echo   Build Dir:    %BUILD_DIR%
echo   Config:       %CONFIG%
echo.

REM Set up MSVC env unless cl is already on PATH.
where cl.exe >nul 2>&1
if %errorlevel% neq 0 (
    set "VCVARS="
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    if not defined VCVARS if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    if not defined VCVARS if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    if not defined VCVARS if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    if not defined VCVARS if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    if not defined VCVARS if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    if not defined VCVARS (
        echo ERROR: vcvars64.bat not found. Install VS Build Tools 18 or VS 2022.
        exit /b 1
    )
    call "%VCVARS%" >nul 2>nul
)

where cl.exe >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: cl.exe not found after MSVC setup
    exit /b 1
)

REM Wasm-side toolchain (LLVM + Binaryen). Prepended to PATH so meson's
REM find_program() resolves clang / wasm-ld / wasm-opt at configure.
if exist "C:\Program Files\LLVM\bin\clang.exe" set "PATH=C:\Program Files\LLVM\bin;%PATH%"
if exist "%LOCALAPPDATA%\binaryen-129\binaryen-version_129\bin\wasm-opt.exe" set "PATH=%LOCALAPPDATA%\binaryen-129\binaryen-version_129\bin;%PATH%"
if exist "%LOCALAPPDATA%\binaryen\binaryen-version_129\bin\wasm-opt.exe" set "PATH=%LOCALAPPDATA%\binaryen\binaryen-version_129\bin;%PATH%"

if %CLEAN%==1 (
    if exist "%BUILD_DIR%" (
        echo Cleaning build directory...
        rmdir /s /q "%BUILD_DIR%"
    )
    if %CONFIGURE_ONLY%==0 if not "%~1"=="" goto :eof
    if %CONFIGURE_ONLY%==0 exit /b 0
)

REM Configure if needed.
if not exist "%BUILD_DIR%\build.ninja" (
    echo Configuring meson...
    meson setup --buildtype %CONFIG% --backend ninja "%BUILD_DIR%" "%PROJECT_ROOT%"
    if !errorlevel! neq 0 (
        echo meson setup failed
        exit /b 1
    )
)

if %CONFIGURE_ONLY%==1 (
    echo Configuration complete.
    exit /b 0
)

echo Building...
ninja -C "%BUILD_DIR%"
if %errorlevel% neq 0 (
    echo Build failed
    exit /b 1
)

echo.
echo Build complete: %BUILD_DIR%\src\yos\yos.exe

endlocal
