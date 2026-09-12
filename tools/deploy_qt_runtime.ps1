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

function Resolve-QtTool([string]$Name) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    if (-not [string]::IsNullOrWhiteSpace($env:QT_ROOT_DIR)) {
        $candidate = Join-Path $env:QT_ROOT_DIR ('bin\\' + $Name)
        if (Test-Path -LiteralPath $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    return $null
}

$windeploy = Resolve-QtTool 'windeployqt.exe'
if (-not $windeploy) { throw 'windeployqt.exe was not found on PATH or under QT_ROOT_DIR\\bin.' }
$qmake = Resolve-QtTool 'qmake.exe'
if (-not $qmake) { throw 'qmake.exe was not found on PATH or under QT_ROOT_DIR\\bin.' }

Write-Host "Deploying Qt runtime with: $windeploy"
& $windeploy --release --force --no-translations --compiler-runtime --dir $dist $exe
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

# The user's known-good portable build contains the complete runtime/plugin set
# below. GitHub's runner has Qt on PATH, which can otherwise hide omissions that
# only show up on a clean end-user machine. Query the installed Qt kit directly
# and make the portable folder match that proven distribution rather than only
# checking the few DLLs needed for CI startup.
$qtBins = (& $qmake -query QT_INSTALL_BINS).Trim()
$qtPlugins = (& $qmake -query QT_INSTALL_PLUGINS).Trim()
if (-not $qtBins -or -not (Test-Path -LiteralPath $qtBins)) {
    throw "qmake returned an invalid QT_INSTALL_BINS path: $qtBins"
}
if (-not $qtPlugins -or -not (Test-Path -LiteralPath $qtPlugins)) {
    throw "qmake returned an invalid QT_INSTALL_PLUGINS path: $qtPlugins"
}

function Copy-RequiredFile([string]$Source, [string]$Destination, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Destination)) {
        if (-not (Test-Path -LiteralPath $Source)) {
            throw "Required portable runtime file is missing from the installed toolchain: $Label`nExpected source: $Source"
        }
        $parent = Split-Path -Parent $Destination
        if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
        Write-Host "Adding portable runtime file: $Label"
        Copy-Item -LiteralPath $Source -Destination $Destination -Force
    }
}

$qtDlls = @(
    'Qt6Core.dll',
    'Qt6Gui.dll',
    'Qt6Network.dll',
    'Qt6Svg.dll',
    'Qt6Widgets.dll',
    'opengl32sw.dll'
)
foreach ($dll in $qtDlls) {
    Copy-RequiredFile (Join-Path $qtBins $dll) (Join-Path $dist $dll) $dll
}

$pluginFiles = @(
    'generic\qtuiotouchplugin.dll',
    'iconengines\qsvgicon.dll',
    'imageformats\qgif.dll',
    'imageformats\qico.dll',
    'imageformats\qjpeg.dll',
    'imageformats\qsvg.dll',
    'networkinformation\qnetworklistmanager.dll',
    'platforms\qwindows.dll',
    'styles\qmodernwindowsstyle.dll',
    'tls\qcertonlybackend.dll',
    'tls\qschannelbackend.dll'
)
foreach ($relative in $pluginFiles) {
    Copy-RequiredFile (Join-Path $qtPlugins $relative) (Join-Path $dist $relative) $relative
}

# BO3 Shader Studio's known-good portable build also ships the legacy D3D
# compiler and DXC runtime beside the EXE. Locate them from the Windows SDK / PATH
# instead of relying on the runner's system directories at runtime.
function Find-FirstExisting([string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $null
}

function Find-ToolSibling([string]$CommandName, [string]$SiblingName) {
    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($command) {
        $candidate = Join-Path (Split-Path -Parent $command.Source) $SiblingName
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

$dxcCompiler = Find-FirstExisting @(
    (Find-ToolSibling 'dxc.exe' 'dxcompiler.dll'),
    (Find-ToolSibling 'dxcompiler.dll' 'dxcompiler.dll')
)
$dxcDxil = Find-FirstExisting @(
    (Find-ToolSibling 'dxc.exe' 'dxil.dll'),
    (Find-ToolSibling 'dxil.dll' 'dxil.dll')
)

$windowsSdkRoots = @(
    $env:WindowsSdkDir,
    ${env:ProgramFiles(x86)} + '\Windows Kits\10',
    $env:ProgramFiles + '\Windows Kits\10'
) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and (Test-Path -LiteralPath $_) }

if (-not $dxcCompiler -or -not $dxcDxil) {
    foreach ($sdkRoot in $windowsSdkRoots) {
        if (-not $dxcCompiler) {
            $hit = Get-ChildItem -LiteralPath $sdkRoot -Filter 'dxcompiler.dll' -File -Recurse -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -match '\\x64\\' } |
                Sort-Object FullName -Descending |
                Select-Object -First 1
            if ($hit) { $dxcCompiler = $hit.FullName }
        }
        if (-not $dxcDxil) {
            $hit = Get-ChildItem -LiteralPath $sdkRoot -Filter 'dxil.dll' -File -Recurse -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -match '\\x64\\' } |
                Sort-Object FullName -Descending |
                Select-Object -First 1
            if ($hit) { $dxcDxil = $hit.FullName }
        }
        if ($dxcCompiler -and $dxcDxil) { break }
    }
}

if (-not $dxcCompiler) { throw 'Could not locate dxcompiler.dll in the Windows SDK/toolchain.' }
if (-not $dxcDxil) { throw 'Could not locate dxil.dll in the Windows SDK/toolchain.' }
Copy-RequiredFile $dxcCompiler (Join-Path $dist 'dxcompiler.dll') 'dxcompiler.dll'
Copy-RequiredFile $dxcDxil (Join-Path $dist 'dxil.dll') 'dxil.dll'

$d3dCompiler = $null
foreach ($sdkRoot in $windowsSdkRoots) {
    $hit = Get-ChildItem -LiteralPath $sdkRoot -Filter 'd3dcompiler_47.dll' -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($hit) { $d3dCompiler = $hit.FullName; break }
}
if (-not $d3dCompiler) {
    $systemCandidate = Join-Path $env:WINDIR 'System32\d3dcompiler_47.dll'
    if (Test-Path -LiteralPath $systemCandidate) { $d3dCompiler = $systemCandidate }
}
if (-not $d3dCompiler) { throw 'Could not locate d3dcompiler_47.dll.' }
Copy-RequiredFile $d3dCompiler (Join-Path $dist 'd3dcompiler_47.dll') 'd3dcompiler_47.dll'

# Match the existing working portable archive by shipping Microsoft's x64 VC++
# redistributable installer as a fallback for machines without the runtime.
$vcRedist = $null

# vcvars64.bat runs inside the compile cmd step, and environment changes from
# that process do not survive into this separate GitHub Actions PowerShell step.
# Resolve Visual Studio independently so VC redist discovery cannot depend on
# VCToolsRedistDir/VSINSTALLDIR leaking across process boundaries.
$vsRoot = $null
$vswhereCandidates = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
    (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and (Test-Path -LiteralPath $_) }
foreach ($vswhere in $vswhereCandidates) {
    $resolved = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null | Select-Object -First 1)
    if (-not [string]::IsNullOrWhiteSpace($resolved)) {
        $vsRoot = $resolved.Trim()
        break
    }
}

$vcRoots = @(
    $env:VCToolsRedistDir,
    $(if ($env:VSINSTALLDIR) { Join-Path $env:VSINSTALLDIR 'VC\Redist\MSVC' } else { $null }),
    $(if ($vsRoot) { Join-Path $vsRoot 'VC\Redist\MSVC' } else { $null })
) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and (Test-Path -LiteralPath $_) } | Select-Object -Unique

foreach ($vcRoot in $vcRoots) {
    $hit = Get-ChildItem -LiteralPath $vcRoot -Filter 'vc_redist.x64.exe' -File -Recurse -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($hit) { $vcRedist = $hit.FullName; break }
}
if (-not $vcRedist) { throw 'Could not locate vc_redist.x64.exe in the Visual Studio redist tree.' }
Copy-RequiredFile $vcRedist (Join-Path $dist 'vc_redist.x64.exe') 'vc_redist.x64.exe'

$required = @(
    'BO3HLSLPreviewer.exe',
    'Qt6Core.dll',
    'Qt6Gui.dll',
    'Qt6Network.dll',
    'Qt6Svg.dll',
    'Qt6Widgets.dll',
    'd3dcompiler_47.dll',
    'dxcompiler.dll',
    'dxil.dll',
    'opengl32sw.dll',
    'generic\qtuiotouchplugin.dll',
    'iconengines\qsvgicon.dll',
    'imageformats\qgif.dll',
    'imageformats\qico.dll',
    'imageformats\qjpeg.dll',
    'imageformats\qsvg.dll',
    'networkinformation\qnetworklistmanager.dll',
    'platforms\qwindows.dll',
    'styles\qmodernwindowsstyle.dll',
    'tls\qcertonlybackend.dll',
    'tls\qschannelbackend.dll',
    'vc_redist.x64.exe'
)
$missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $dist $_)) })
if ($missing.Count -gt 0) {
    throw ('Portable runtime deployment is incomplete: ' + ($missing -join ', '))
}

Write-Host 'Portable runtime deployment matches the known-good local distribution:'
$required | ForEach-Object { Write-Host "  $_" }
