<#
    yetty installer bootstrap for Windows.

    Downloads the yetty Windows release from the latest GitHub release, unpacks
    it, and runs the self-contained installer. The installer carries its
    payload embedded and unpacks each piece to the right location on first
    run. It comes in three sizes (see src/yetty/yinstall/README.md, "Variants"):
        min      yetty alone with shaders, raw fonts and config; the MSDF font
                 atlases are built from the raw fonts on the first start
        default  every yetty executable and tool, fonts + pre-generated
                 atlases, config, greeter and demo assets
        max      default plus the RISC-V VM runtime and QEMU

    Usage (PowerShell):
        irm https://yetty.dev/install.ps1 | iex
        irm https://yetty.dev/install-min.ps1 | iex
        irm https://yetty.dev/install-max.ps1 | iex

    install-min.ps1 / install-max.ps1 are this script with `$variantDefault`
    pinned (build-tools/install/make-variants.sh derives them).

    Environment overrides (set before the pipe, since `iex` cannot take params):
        $env:YETTY_VARIANT      min | default | max. Default: the script's pinned variant.
        $env:YETTY_VERSION      release tag to install (e.g. yetty-0.2.46). Default: latest.
        $env:YETTY_REPO         owner/repo to download from. Default: zokrezyl/yetty.
        $env:YETTY_INSTALL_ARGS extra args forwarded to the installer (e.g. "--verbose --force").

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

# The variant this script installs unless $env:YETTY_VARIANT says otherwise.
# make-variants.sh rewrites this one line for install-min.ps1 and
# install-max.ps1 — keep it on its own line, exactly this shape.
$variantDefault = 'default'

$repo    = if ($env:YETTY_REPO)    { $env:YETTY_REPO }    else { 'zokrezyl/yetty' }
$version = if ($env:YETTY_VERSION) { $env:YETTY_VERSION } else { 'latest' }
$variant = if ($env:YETTY_VARIANT) { $env:YETTY_VARIANT } else { $variantDefault }
if ($variant -notin @('min', 'default', 'max')) {
    Die "unknown variant '$variant' (have min, default, max)"
}

# The Windows desktop release ships one x64 archive per variant, each holding
# a single installer .exe. The default variant is the plain archive name, min
# and max carry a suffix.
$asset         = if ($variant -eq 'default') { 'yetty-windows.zip' } else { "yetty-windows-$variant.zip" }
$installerName = if ($variant -eq 'default') { 'yinstall.exe' }      else { "yinstall-$variant.exe" }

# Resolve "latest" to a concrete yetty-X.Y.Z tag. The repo publishes several
# release families (yetty-*, yos-web-*, yetty-rootfs-riscv-*) and GitHub's
# repo-wide "latest release" pointer belongs to whichever release published
# most recently — not necessarily a desktop one. So pick the highest
# yetty-X.Y.Z version from the release list; fall back to the repo-wide
# redirect only if the API is unreachable (e.g. rate-limited).
$resolvedTag = $null
if ($version -eq 'latest') {
    try {
        $releases = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases?per_page=100" -UseBasicParsing
        $resolvedTag = $releases |
            Where-Object { $_.tag_name -match '^yetty-\d+(\.\d+)+$' } |
            Sort-Object { [version]($_.tag_name -replace '^yetty-', '') } |
            Select-Object -Last 1 -ExpandProperty tag_name
        if ($resolvedTag) {
            Write-Log "latest desktop release is $resolvedTag"
        }
    } catch {
        Write-Log 'cannot list releases via the GitHub API; falling back to the repo-wide latest-release redirect'
    }
}

$url = if ($resolvedTag) {
    "https://github.com/$repo/releases/download/$resolvedTag/$asset"
} elseif ($version -eq 'latest') {
    "https://github.com/$repo/releases/latest/download/$asset"
} else {
    "https://github.com/$repo/releases/download/$version/$asset"
}

$workdir = Join-Path ([System.IO.Path]::GetTempPath()) ("yetty-install-" + [System.Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $workdir -Force | Out-Null

try {
    $archive = Join-Path $workdir $asset

    $displayVersion = if ($resolvedTag) { $resolvedTag } else { $version }
    Write-Log "downloading $asset ($displayVersion, $variant variant) from $repo"
    Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing

    Write-Log 'unpacking installer'
    $extractDir = Join-Path $workdir 'extracted'
    Expand-Archive -Path $archive -DestinationPath $extractDir -Force

    # The .exe may sit at the archive root or one level down; find it either
    # way.
    $installer = Get-ChildItem -Path $extractDir -Filter $installerName -Recurse -File |
        Select-Object -First 1
    if (-not $installer) {
        Die "installer '$installerName' not found inside $asset"
    }

    Write-Log "running $installerName"
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
