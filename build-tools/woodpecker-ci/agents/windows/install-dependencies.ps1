# install-dependencies.ps1 — winy Woodpecker agent setup.
#
# Brings a fresh Windows machine up to the same baseline that GitHub's
# windows-latest hosted runner provides, so the .woodpecker/*-windows.yml
# pipelines can run without per-job workarounds.
#
# Run from an *elevated* PowerShell:
#     Start-Process powershell -Verb RunAs
#     Set-ExecutionPolicy -Scope Process Bypass -Force
#     .\install-dependencies.ps1
#
# Idempotent: re-running skips anything already installed.

#Requires -RunAsAdministrator

$ErrorActionPreference = 'Stop'

function Section($msg) { Write-Host "`n=== $msg ===" -ForegroundColor Cyan }
function Info($msg)    { Write-Host "  $msg" -ForegroundColor Gray }
function Ok($msg)      { Write-Host "  ✓ $msg" -ForegroundColor Green }
function Skip($msg)    { Write-Host "  • $msg" -ForegroundColor DarkGray }

# ---------------------------------------------------------------------------
# Chocolatey — package manager used for the bulk of the install.
# ---------------------------------------------------------------------------
Section "Chocolatey"
if (Get-Command choco -ErrorAction SilentlyContinue) {
    Skip "already installed ($(choco --version))"
} else {
    Info "installing…"
    Set-ExecutionPolicy Bypass -Scope Process -Force
    [System.Net.ServicePointManager]::SecurityProtocol = `
        [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
    iex ((New-Object System.Net.WebClient).DownloadString( `
        'https://community.chocolatey.org/install.ps1'))
    # Pick up choco in the current shell.
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
                [Environment]::GetEnvironmentVariable('Path', 'User')
    Ok "installed"
}

# ---------------------------------------------------------------------------
# Build tools — match windows-latest's pre-installed toolchain.
#
# CMake, Ninja, Git, Git LFS already exist on windows-latest. NASM, GNU
# Make, and Strawberry Perl (for Mozilla CA cert + perl-based 3rdparty
# configures) are what the yetty GitHub workflow `choco install`s on top.
# ---------------------------------------------------------------------------
Section "Build tools (choco)"
$pkgs = @(
    'git',
    'git-lfs',
    'cmake',
    'ninja',
    'nasm',
    'make',
    'python',
    'strawberryperl'
)
foreach ($p in $pkgs) {
    $installed = choco list --local-only --limit-output --exact $p 2>$null
    if ($installed) {
        Skip "$p"
    } else {
        Info "installing $p…"
        choco install $p -y --no-progress | Out-Null
        Ok $p
    }
}

# Pick up newly-installed tools in the current shell so the meson pip
# install below sees Python's `pip`.
$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
            [Environment]::GetEnvironmentVariable('Path', 'User')

# ---------------------------------------------------------------------------
# Visual Studio Build Tools (MSVC) — chocolatey installs the bootstrapper,
# we hand it the workload list to keep the install size sane.
# ---------------------------------------------------------------------------
Section "Visual Studio 2022 Build Tools"
$vsBootstrap = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vs_installer.exe"
$vsExists = (Test-Path "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools") -or
            (Test-Path "${env:ProgramFiles(x86)}\Microsoft Visual Studio\18\BuildTools")
if ($vsExists) {
    Skip "VS Build Tools already present"
} else {
    Info "installing VS 2022 Build Tools (VCTools workload)…"
    choco install visualstudio2022buildtools -y --no-progress `
        --package-parameters '--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --quiet --wait' `
        | Out-Null
    Ok "VS Build Tools installed"
}

# ---------------------------------------------------------------------------
# Meson via pip — matches the yetty GitHub workflow's `pip install meson`.
# Some 3rdparty libs (dav1d, glib, etc.) use meson as their build system.
# ---------------------------------------------------------------------------
Section "Meson"
if (Get-Command meson -ErrorAction SilentlyContinue) {
    Skip "already installed"
} else {
    Info "pip install meson…"
    python -m pip install --upgrade pip | Out-Null
    python -m pip install meson | Out-Null
    Ok "meson installed"
}

# ---------------------------------------------------------------------------
# Machine-wide environment.
#
# MSYS2_PATH_TYPE=inherit: Git-for-Windows bash defaults to a stripped
# MSYS PATH inside `bash -c …` invocations, which makes cmake/ninja
# disappear even though cmd sees them fine. windows-latest's profile
# has this set; mirror it so .woodpecker/glfw-windows.yml etc. don't
# need step-level overrides.
# ---------------------------------------------------------------------------
Section "Environment"
$current = [Environment]::GetEnvironmentVariable('MSYS2_PATH_TYPE', 'Machine')
if ($current -eq 'inherit') {
    Skip "MSYS2_PATH_TYPE=inherit already set"
} else {
    [Environment]::SetEnvironmentVariable('MSYS2_PATH_TYPE', 'inherit', 'Machine')
    Ok "MSYS2_PATH_TYPE=inherit (machine)"
}

# ---------------------------------------------------------------------------
# Git config — make plugin-git's `git config --global --replace-all
# safe.directory <workspace>` a no-op rewrite of an already-correct value,
# eliminating the .gitconfig.lock race when multiple windows workflows
# trigger in parallel against the same user profile.
#
# Applied to both the LocalSystem account (if the agent runs as a service)
# and the current user — easier than guessing which one applies.
# ---------------------------------------------------------------------------
Section "Git config"
$gitConfigCmd = "git config --global --get-all safe.directory"
$currentSafeDirs = & cmd /c $gitConfigCmd 2>$null
if ($currentSafeDirs -match '^\*$') {
    Skip "safe.directory='*' already set"
} else {
    Info "git config --global --add safe.directory '*'"
    git config --global --add safe.directory '*'
    Ok "safe.directory='*' added"
}

# ---------------------------------------------------------------------------
# Restart the agent service so it picks up the new MSYS2_PATH_TYPE.
# The service name varies by install — try the common ones.
# ---------------------------------------------------------------------------
Section "Agent service"
$svc = Get-Service -Name 'woodpecker-agent', 'WoodpeckerAgent' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($svc) {
    Info "restarting $($svc.Name)…"
    Restart-Service $svc.Name -Force
    Ok "$($svc.Name) restarted"
} else {
    Skip "no woodpecker-agent service found (running standalone? restart it manually)"
}

# ---------------------------------------------------------------------------
# Summary — verify the tools the woodpecker pipelines call.
# ---------------------------------------------------------------------------
Section "Verification"
$tools = @('git', 'git-lfs', 'cmake', 'ninja', 'nasm', 'make', 'python', 'meson', 'perl')
foreach ($t in $tools) {
    $cmd = Get-Command $t -ErrorAction SilentlyContinue
    if ($cmd) {
        Ok ("{0,-10} {1}" -f $t, $cmd.Source)
    } else {
        Write-Host ("  ✗ {0,-10} NOT FOUND on PATH" -f $t) -ForegroundColor Red
    }
}

Write-Host "`nDone. Open a *new* shell so PATH/env changes take effect." -ForegroundColor Green
