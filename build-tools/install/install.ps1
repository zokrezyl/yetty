<#
    yetty installer bootstrap for Windows.

    Downloads the yetty Windows release from the latest GitHub release, unpacks
    it, and runs the self-contained `yinstall.exe` installer. `yinstall` carries
    the whole yetty product (the terminal, the companion tools, shaders, fonts
    and config) as an embedded payload and unpacks each piece to the right
    location on first run.

    Usage (PowerShell):
        irm https://yetty.dev/install.ps1 | iex

    Environment overrides (set before the pipe, since `iex` cannot take params):
        $env:YETTY_VERSION      release tag to install (e.g. yetty-0.2.46). Default: latest.
        $env:YETTY_REPO         owner/repo to download from. Default: zokrezyl/yetty.
        $env:YETTY_INSTALL_ARGS extra args forwarded to yinstall (e.g. "--verbose --force").

    This script is written to be safe to run via `irm ... | iex`: it has no
    param() block (which iex cannot populate) and is driven entirely by the
    environment overrides above.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# The large release download renders far faster with the progress bar off — the
# Invoke-WebRequest progress meter is pathologically slow on big files.
$ProgressPreference = 'SilentlyContinue'

# Older Windows PowerShell defaults to TLS 1.0/1.1, which GitHub rejects.
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
} catch {
    # .NET Core / PowerShell 7 negotiates TLS automatically; ignore if the
    # legacy ServicePointManager knob is unavailable.
}

function Write-Log { param([string]$Message) Write-Host "yetty-install: $Message" }
function Die       { param([string]$Message) throw "yetty-install: error: $Message" }

$repo    = if ($env:YETTY_REPO)    { $env:YETTY_REPO }    else { 'zokrezyl/yetty' }
$version = if ($env:YETTY_VERSION) { $env:YETTY_VERSION } else { 'latest' }

# The Windows desktop release ships a single x64 archive.
$asset = 'yetty-windows.zip'

# "latest" uses the version-independent redirect so the script never needs
# updating per release; a pinned tag uses the direct path.
$url = if ($version -eq 'latest') {
    "https://github.com/$repo/releases/latest/download/$asset"
} else {
    "https://github.com/$repo/releases/download/$version/$asset"
}

$workdir = Join-Path ([System.IO.Path]::GetTempPath()) ("yetty-install-" + [System.Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $workdir -Force | Out-Null

try {
    $archive = Join-Path $workdir $asset

    Write-Log "downloading $asset ($version) from $repo"
    Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing

    Write-Log 'unpacking installer'
    $extractDir = Join-Path $workdir 'extracted'
    Expand-Archive -Path $archive -DestinationPath $extractDir -Force

    # yinstall.exe may sit at the archive root or one level down (the release
    # stages the runtime under a yetty-windows/ folder); find it either way.
    $installer = Get-ChildItem -Path $extractDir -Filter 'yinstall.exe' -Recurse -File |
        Select-Object -First 1
    if (-not $installer) {
        Die "installer 'yinstall.exe' not found inside $asset"
    }

    Write-Log 'running yinstall'
    # Forward any pass-through args (e.g. --verbose, --force). yinstall prints
    # its own log, including where each component landed and any PATH advice.
    if ($env:YETTY_INSTALL_ARGS) {
        $forwarded = $env:YETTY_INSTALL_ARGS -split '\s+' | Where-Object { $_ -ne '' }
        & $installer.FullName @forwarded
    } else {
        & $installer.FullName
    }
    if ($LASTEXITCODE -ne 0) {
        Die "yinstall exited with code $LASTEXITCODE"
    }
} finally {
    # Best-effort cleanup; a cleanup failure must not mask an install error.
    Remove-Item -Path $workdir -Recurse -Force -ErrorAction SilentlyContinue
}
