param(
    [string]$Executable = 'dist\BO3HLSLPreviewer.exe',
    [string]$DistDir = 'dist'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$exe = if ([IO.Path]::IsPathRooted($Executable)) { $Executable } else { Join-Path $root $Executable }
$dist = if ([IO.Path]::IsPathRooted($DistDir)) { $DistDir } else { Join-Path $root $DistDir }

if (-not (Test-Path -LiteralPath $exe)) {
    throw "Release executable not found: $exe"
}
New-Item -ItemType Directory -Path $dist -Force | Out-Null

$windeploy = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
if (-not $windeploy) { throw 'windeployqt.exe is not on PATH.' }
$qmake = Get-Command qmake.exe -ErrorAction SilentlyContinue
if (-not $qmake) { throw 'qmake.exe is not on PATH.' }

Write-Host "Deploying Qt runtime with: $($windeploy.Source)"
& $windeploy.Source --release --force --no-translations --compiler-runtime --dir $dist $exe
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

# GitHub's Qt install is also on PATH. That can hide a broken portable package:
# the EXE may run during CI even when the release ZIP does not contain Qt. Query
# the kit directly and repair the small set of startup-critical files if a
# windeployqt regression ever omits one.
$qtBins = (& $qmake.Source -query QT_INSTALL_BINS).Trim()
$qtPlugins = (& $qmake.Source -query QT_INSTALL_PLUGINS).Trim()
if (-not $qtBins -or -not (Test-Path -LiteralPath $qtBins)) {
    throw "qmake returned an invalid QT_INSTALL_BINS path: $qtBins"
}
if (-not $qtPlugins -or -not (Test-Path -LiteralPath $qtPlugins)) {
    throw "qmake returned an invalid QT_INSTALL_PLUGINS path: $qtPlugins"
}

$requiredDlls = @('Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6Network.dll')
foreach ($dll in $requiredDlls) {
    $target = Join-Path $dist $dll
    if (-not (Test-Path -LiteralPath $target)) {
        $source = Join-Path $qtBins $dll
        if (-not (Test-Path -LiteralPath $source)) {
            throw "Required Qt runtime DLL is missing from both deployment and Qt kit: $dll"
        }
        Write-Warning "windeployqt did not emit $dll; copying it directly from the Qt kit."
        Copy-Item -LiteralPath $source -Destination $target -Force
    }
}

$platformDir = Join-Path $dist 'platforms'
$qwindows = Join-Path $platformDir 'qwindows.dll'
if (-not (Test-Path -LiteralPath $qwindows)) {
    $source = Join-Path $qtPlugins 'platforms\qwindows.dll'
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Required Qt platform plugin is missing from the Qt kit: $source"
    }
    New-Item -ItemType Directory -Path $platformDir -Force | Out-Null
    Write-Warning 'windeployqt did not emit platforms\qwindows.dll; copying it directly from the Qt kit.'
    Copy-Item -LiteralPath $source -Destination $qwindows -Force
}

$required = @(
    'BO3HLSLPreviewer.exe',
    'Qt6Core.dll',
    'Qt6Gui.dll',
    'Qt6Widgets.dll',
    'Qt6Network.dll',
    'platforms\qwindows.dll'
)
$missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $dist $_)) })
if ($missing.Count -gt 0) {
    throw ('Portable Qt deployment is incomplete: ' + ($missing -join ', '))
}

Write-Host 'Portable Qt deployment verified:'
$required | ForEach-Object { Write-Host "  $_" }
