#requires -Version 7.0
# Run from an x64 MSVC developer shell. No installer, windeployqt, signing
# service, plugins, or desktop build is used. Keep the entire ZIP together.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDir,
    [Parameter(Mandatory)][string]$QtPath,
    [Parameter(Mandatory)][string]$VcpkgPrefix, # installed/<target-triplet>, not the host prefix
    [string]$OutDir = $BuildDir
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell on Windows.' }
$name = 'AutoSlidesExtractorHelper'
$root = Split-Path $PSScriptRoot -Parent
$build = (Get-Item -LiteralPath $BuildDir).FullName
$qt = (Get-Item -LiteralPath $QtPath).FullName
$prefix = (Get-Item -LiteralPath $VcpkgPrefix).FullName
$triplet = Split-Path $prefix -Leaf
$exe = Join-Path $build "$name.exe"
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { $exe = Join-Path $build "Release/$name.exe" }
$exe = (Get-Item -LiteralPath $exe).FullName
$dumpbin = (Get-Command dumpbin.exe -CommandType Application -ErrorAction Stop).Source
if (-not $env:VCToolsRedistDir) { throw 'Missing VCToolsRedistDir; initialize the x64 MSVC developer environment.' }
$crt = @(Get-ChildItem -LiteralPath "$env:VCToolsRedistDir/x64" -Directory -Filter 'Microsoft.VC*.CRT')
if ($crt.Count -ne 1) { throw 'Expected one x64 Microsoft.VC*.CRT runtime in VCToolsRedistDir.' }
$cmake = Get-Content -Raw -LiteralPath "$root/CMakeLists.txt"
if ($cmake -notmatch '(?m)^project\(AutoSlidesExtractor\s+VERSION\s+(\d+\.\d+\.\d+)\b') { throw 'Cannot read project version.' }
$version = $Matches[1]

# vcpkg's FFmpeg port installs exactly one license, selected from configure's
# result. Never redistribute a desktop GPL or nonfree FFmpeg under this notice.
if ((Get-Content -Raw -LiteralPath "$prefix/share/ffmpeg/copyright") -notmatch '(?m)^\s*GNU LESSER GENERAL PUBLIC LICENSE\s*$') {
    throw 'Expected the LGPL FFmpeg from the helper manifest.'
}
$owners = @{}
$lists = Get-ChildItem -LiteralPath "$(Split-Path $prefix -Parent)/vcpkg/info" -Filter "*_${triplet}.list" -File
foreach ($list in $lists) {
    $port = $list.Name.Split('_')[0]
    foreach ($entry in (Get-Content -LiteralPath $list.FullName)) {
        if ($entry -notmatch ('^' + [regex]::Escape($triplet) + '/bin/([^/]+\.dll)$')) { continue }
        $dll = $Matches[1]
        if ($owners.ContainsKey($dll)) { throw "Multiple vcpkg owners for $dll" }
        $owners[$dll] = $port
    }
}

# Explicit Windows components, not arbitrary files found in System32/PATH.
# In particular the MSVC redistributable might be installed on the CI runner
# but not the target machine; import it app-locally from the VC redist only.
$osNames = @'
advapi32 authz avrt bcrypt cfgmgr32 combase comctl32 comdlg32 crypt32 cryptbase d2d1 d3d9 d3d11 d3d12
d3dcompiler_47 dbghelp dcomp dnsapi dwmapi dwrite dxcore dxgi dxva2 gdi32 imm32 iphlpapi kernel32
kernelbase mf mfplat mfreadwrite mpr msacm32 msvcrt msvfw32 ncrypt netapi32 netutils normaliz ntdll
ole32 oleaut32 pdh powrprof propsys psapi rpcrt4 runtimeobject secur32 setupapi shell32 shlwapi shcore
synchronization ucrtbase user32 userenv usp10 uxtheme version winhttp wininet winmm wintrust wintypes
ws2_32 wsock32 wtsapi32
'@
$osDlls = @($osNames -split '\s+' | ForEach-Object { "$_.dll" }) + 'winspool.drv'

function Get-Imports([string]$binary) {
    $lines = & $dumpbin /nologo /dependents $binary
    if ($LASTEXITCODE -ne 0) { throw "dumpbin /dependents failed: $binary" }
    $reading = $false
    $count = 0
    foreach ($line in $lines) {
        $text = $line.Trim()
        if ($text -match '^Image has the following (delay load )?dependencies:$') { $reading = $true; continue }
        if ($text -eq 'Summary') { break }
        if (-not $reading -or -not $text) { continue }
        if ($text -notmatch '^[\w.-]+\.(dll|drv)$' -or $text -match '^(con|prn|aux|nul|com[1-9]|lpt[1-9])\.') {
            throw "Unrecognized import '$text' in $binary"
        }
        $count++
        $text
    }
    if (-not $count) { throw "No imports parsed in $binary" }
}

$out = (New-Item -ItemType Directory -Force -Path $OutDir).FullName
$work = (New-Item -ItemType Directory -Path (Join-Path $out ('.helper-package-' + [guid]::NewGuid()))).FullName
$stage = Join-Path $work $name
$archive = "$name-$version-Windows-x64.zip"
$oldLang = $env:VSLANG
try {
    $env:VSLANG = '1033' # parse the English dumpbin format; fail rather than skip unfamiliar imports
    $licenses = (New-Item -ItemType Directory -Path "$stage/licenses" -Force).FullName
    Copy-Item -LiteralPath "$root/LICENSE" -Destination "$licenses/LICENSE-MIT.txt"
    Copy-Item -LiteralPath "$root/licenses/LGPL-3.0.txt", "$root/licenses/GPL-3.0.txt" -Destination $licenses
    # The kit SBOM (if provided) includes third-party attributions within Qt.
    if (Test-Path -LiteralPath "$qt/sbom" -PathType Container) {
        Get-ChildItem -LiteralPath "$qt/sbom" -Filter 'qtbase-*.spdx' -File |
            Where-Object Name -Match '^qtbase-\d+\.\d+\.\d+\.spdx$' | Copy-Item -Destination $licenses
    }
    Copy-Item -LiteralPath $exe -Destination $stage
    $queue = [Collections.Generic.Queue[string]]::new()
    $queue.Enqueue("$name.exe")
    $bundled = @{}
    while ($queue.Count) {
        $file = $queue.Dequeue()
        $binary = Join-Path $stage $file
        $headers = (& $dumpbin /nologo /headers $binary) -join "`n"
        if ($LASTEXITCODE -ne 0 -or $headers -notmatch '(?im)^\s*8664 machine \(x64\)') { throw "Not an x64 PE: $file" }
        if ($file -eq "$name.exe" -and $headers -notmatch '(?im)^\s*3 subsystem \(Windows CUI\)') { throw 'Helper must be a console executable.' }
        foreach ($dll in (Get-Imports $binary)) {
            if ($dll -match '^(api-ms-win-|ext-ms-win-)' -or $dll -in $osDlls) { continue }
            if ($bundled.ContainsKey($dll)) { continue }
            if ($dll -eq 'Qt6Core.dll') {
                $src = "$qt/bin/$dll"
                $owner = 'Qt Core'
            } elseif ($dll -match '^(vcruntime|msvcp|concrt)14[0-9][\w]*\.dll$') {
                $src = "$($crt[0].FullName)/$dll"
                $owner = 'Microsoft Visual C++ Redistributable'
            } elseif ($owners.ContainsKey($dll)) {
                $owner = $owners[$dll]
                if ($owner -notin @('ffmpeg', 'opencv4', 'libpng', 'zlib') -or
                    ($owner -eq 'opencv4' -and $dll -notmatch '^opencv_(core|imgproc|imgcodecs)\d*\.dll$') -or
                    ($owner -eq 'ffmpeg' -and $dll -notmatch '^(avcodec|avformat|avutil|swscale|swresample)(-\d+)?\.dll$')) {
                    throw "Unexpected helper import $dll (port $owner)"
                }
                $src = "$prefix/bin/$dll"
                $copyright = Get-Item -LiteralPath "$prefix/share/$owner/copyright"
                if (-not $copyright.Length) { throw "Empty copyright for $owner" }
                Copy-Item -LiteralPath $copyright.FullName -Destination "$licenses/$owner.txt" -Force
            } else {
                throw "Unresolved import '$dll' required by '$file'; not in helper inputs."
            }
            Copy-Item -LiteralPath $src -Destination (Join-Path $stage $dll)
            $bundled[$dll] = $owner
            $queue.Enqueue($dll)
        }
    }
    foreach ($required in ('Qt6Core', 'opencv_core', 'opencv_imgproc', 'opencv_imgcodecs', 'avformat', 'avcodec', 'avutil', 'swscale')) {
        if (-not ($bundled.Keys -match ('^' + $required + '[-\d]*\.dll$'))) { throw "Missing required $required DLL." }
    }
    foreach ($port in ('libpng', 'zlib')) {
        if ($port -notin $bundled.Values) { throw "Missing dynamic $port from the PNG helper profile." }
    }
    $inventory = ($bundled.Keys | Sort-Object | ForEach-Object { "  $_ : $($bundled[$_])" }) -join "`n"
    $qtVersion = (Get-Item -LiteralPath "$stage/Qt6Core.dll").VersionInfo.ProductVersion
    @"
AutoSlides Extractor Helper $version - Windows x64

The helper executable is not Authenticode-signed by this build. The helper
source is licensed under MIT (LICENSE-MIT.txt). No Windows system DLLs are
redistributed. The following runtime libraries are included beside the EXE:
$inventory

Qt Core $qtVersion is dynamically linked under LGPLv3 (LGPL-3.0.txt,
incorporating GPL-3.0.txt). FFmpeg is dynamically linked under the license
selected by this build (ffmpeg.txt). You may replace the LGPL libraries with
compatible modified versions and reverse engineer the combined work to debug
such modifications. See <port>.txt for the notices of the bundled vcpkg DLLs.
Microsoft Visual C++ runtime files are redistributed under Visual Studio's
Microsoft software license terms; they are not licensed under MIT or LGPL.

Helper source and build recipes: https://github.com/bit-admin/AutoSlides-Extractor
Qt sources: https://download.qt.io/archive/qt/
Qt Core attributions: https://doc.qt.io/qt-6/qtcore-attribution.html
FFmpeg sources: https://ffmpeg.org/download.html
OpenCV sources: https://github.com/opencv/opencv
Port versions and patches: https://github.com/microsoft/vcpkg/tree/2026.07.29/ports
MSVC redistributable terms: https://visualstudio.microsoft.com/license-terms/
"@ | Set-Content -LiteralPath "$licenses/NOTICE.txt" -Encoding utf8

    # Exercise the staged console EXE without Qt or vcpkg available on PATH.
    $oldPath = $env:PATH
    Push-Location -LiteralPath $stage
    try {
        $env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
        $reply = & ".\$name.exe" --version --json
        if ($LASTEXITCODE -ne 0) { throw "Staged helper exited $LASTEXITCODE." }
        $record = $reply | ConvertFrom-Json
        if ($record.event -ne 'version' -or $record.appVersion -ne $version) { throw 'Unexpected helper version response.' }
    } finally {
        $env:PATH = $oldPath
        Pop-Location
    }
    $zip = Join-Path $work $archive
    [IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [IO.Compression.CompressionLevel]::Optimal, $true)
    Move-Item -LiteralPath $zip -Destination (Join-Path $out $archive) -Force
    Write-Host "Packaged $out/$archive"
} finally {
    $env:VSLANG = $oldLang
    Remove-Item -LiteralPath $work -Recurse -Force
}
