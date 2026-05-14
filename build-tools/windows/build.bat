@echo off
REM Yetty Windows Build Script
REM Usage: build.bat [debug|release] [clean|configure]

setlocal enabledelayedexpansion

set CONFIG=Release
set CLEAN=0
set CONFIGURE_ONLY=0

REM Add GNU Make and NASM to PATH if available
if exist "C:\Program Files (x86)\GnuWin32\bin" set PATH=C:\Program Files (x86)\GnuWin32\bin;%PATH%
if exist "C:\Program Files\NASM" set PATH=C:\Program Files\NASM;%PATH%
if exist "%LOCALAPPDATA%\bin\NASM" set PATH=%LOCALAPPDATA%\bin\NASM;%PATH%

REM brotli.exe ships with Git for Windows under mingw64\bin. Without it on
REM PATH the configure-time find_program(BROTLI_EXECUTABLE) in incbin.cmake
REM fails and the data/ prefix gets embedded raw (~40 MB of shaders+fonts
REM instead of ~6 MB compressed). The runtime already decompresses brotli
REM at asset-extract time (decompress_brotli in incbin-assets.c).
if exist "C:\Program Files\Git\mingw64\bin\brotli.exe" set PATH=C:\Program Files\Git\mingw64\bin;%PATH%
if exist "C:\Program Files\Git\usr\bin\brotli.exe" set PATH=C:\Program Files\Git\usr\bin;%PATH%

REM CMake's file(DOWNLOAD ...) uses libcurl, which on Windows ships without a
REM CA bundle. Without CMAKE_TLS_CAINFO the 3rdparty-fetch HTTPS pulls fail
REM with "Peer certificate cannot be authenticated". Strawberry Perl includes
REM Mozilla's bundle; use it if available.
if not defined CMAKE_TLS_CAINFO if exist "C:\Strawberry\perl\vendor\lib\Mozilla\CA\cacert.pem" set CMAKE_TLS_CAINFO=C:/Strawberry/perl/vendor/lib/Mozilla/CA/cacert.pem

:parse_args
if "%1"=="" goto :start
if /i "%1"=="debug" (
    set CONFIG=Debug
    shift
    goto :parse_args
)
if /i "%1"=="release" (
    set CONFIG=Release
    shift
    goto :parse_args
)
if /i "%1"=="clean" (
    set CLEAN=1
    shift
    goto :parse_args
)
if /i "%1"=="configure" (
    set CONFIGURE_ONLY=1
    shift
    goto :parse_args
)
shift
goto :parse_args

:start
REM Use current directory as project root (called from project root via make)
set PROJECT_ROOT=%CD%\

REM Convert to lowercase for build dir
set CONFIG_LOWER=%CONFIG%
if /i "%CONFIG%"=="Debug" set CONFIG_LOWER=debug
if /i "%CONFIG%"=="Release" set CONFIG_LOWER=release

set BUILD_DIR=%PROJECT_ROOT%build-windows-ytrace-%CONFIG_LOWER%

echo Yetty Windows Build
echo   Project Root: %PROJECT_ROOT%
echo   Build Dir: %BUILD_DIR%
echo   Config: %CONFIG%
echo.

REM Setup MSVC environment if cl.exe is not already in PATH
where cl.exe >nul 2>&1
if %errorlevel% neq 0 (
    echo Setting up MSVC environment...
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 2>nul
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 2>nul
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 2>nul
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 2>nul
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" x64 2>nul
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" x64 2>nul
    ) else (
        echo ERROR: Could not find Visual Studio vcvarsall.bat
        echo Install VS Build Tools: winget install Microsoft.VisualStudio.2022.BuildTools
        exit /b 1
    )
)

REM Verify compiler
where cl.exe >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: cl.exe not found after MSVC setup
    exit /b 1
)

REM Clean if requested
if %CLEAN%==1 (
    if exist "%BUILD_DIR%" (
        echo Cleaning build directory...
        rmdir /s /q "%BUILD_DIR%"
    )
)

REM Configure if needed
if not exist "%BUILD_DIR%\build.ninja" (
    if not exist "%BUILD_DIR%\*.sln" (
        echo Configuring CMake...

        REM Optional extra cmake args (e.g. -DCMAKE_TLS_CAINFO=...)
        set CMAKE_EXTRA_ARGS=
        if defined CMAKE_TLS_CAINFO set CMAKE_EXTRA_ARGS=-DCMAKE_TLS_CAINFO=%CMAKE_TLS_CAINFO%

        REM Check for Ninja
        where ninja >nul 2>&1
        if !errorlevel!==0 (
            cmake -B "%BUILD_DIR%" -G "Ninja" -DCMAKE_BUILD_TYPE=%CONFIG% -DWEBGPU_BACKEND=dawn !CMAKE_EXTRA_ARGS! "%PROJECT_ROOT%"
        ) else (
            cmake -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DWEBGPU_BACKEND=dawn !CMAKE_EXTRA_ARGS! "%PROJECT_ROOT%"
        )

        if !errorlevel! neq 0 (
            echo CMake configuration failed
            exit /b 1
        )
    )
)

REM Exit if configure-only
if %CONFIGURE_ONLY%==1 (
    echo Configuration complete.
    exit /b 0
)

REM Build
echo Building...
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel
if %errorlevel% neq 0 (
    echo Build failed
    exit /b 1
)

echo.
echo Build complete!

if exist "%BUILD_DIR%\yetty.exe" (
    echo Executable: %BUILD_DIR%\yetty.exe
) else if exist "%BUILD_DIR%\%CONFIG%\yetty.exe" (
    echo Executable: %BUILD_DIR%\%CONFIG%\yetty.exe
) else (
    echo WARNING: yetty.exe not found in expected locations
)

endlocal
