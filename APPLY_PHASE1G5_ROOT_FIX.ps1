param(
    [switch]$RemoveAccidentalNestedCopy
)

$ErrorActionPreference = 'Stop'
$root = (Get-Location).Path

if (-not (Test-Path -LiteralPath (Join-Path $root 'BO3HLSLPreviewer.pro'))) {
    throw "Run this from the repository root (the folder that contains BO3HLSLPreviewer.pro). Current folder: $root"
}

$source = Join-Path $root 'src\preview_renderer.cpp'
$qrc = Join-Path $root 'resources\learning.qrc'
$shader = Join-Path $root 'resources\shaders\ape_deferred_lighting.hlsl'

if (-not (Test-Path -LiteralPath $source)) { throw "Missing root src\preview_renderer.cpp" }
if (-not (Test-Path -LiteralPath $qrc)) { throw "Missing root resources\learning.qrc" }
if (-not (Test-Path -LiteralPath $shader)) { throw "Missing root resources\shaders\ape_deferred_lighting.hlsl" }

$expectedPreviewSha = 'a478b8706cd896e374936c2440a58a19c0313173659190c71fb47bbb8505bc9b'
$actualPreviewSha = (Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash.ToLowerInvariant()
if ($actualPreviewSha -ne $expectedPreviewSha) {
    throw "Root preview_renderer.cpp is not the Phase 1g.5 fixed file. Expected SHA256 $expectedPreviewSha, got $actualPreviewSha"
}

$sourceText = Get-Content -LiteralPath $source -Raw
if ($sourceText -match 'static\s+const\s+char\s*\*\s*deferredLightPsSource\s*=\s*R"\(') {
    throw 'The old ~16.6 KiB deferred HLSL raw string is still embedded in ROOT src\preview_renderer.cpp.'
}
if ($sourceText -notmatch ':/preview/ape_deferred_lighting\.hlsl') {
    throw 'Root preview_renderer.cpp does not load the external APE lighting shader resource.'
}

$qrcText = Get-Content -LiteralPath $qrc -Raw
if ($qrcText -notmatch 'alias="ape_deferred_lighting\.hlsl"') {
    throw 'Root resources\learning.qrc does not register ape_deferred_lighting.hlsl.'
}

$nested = Join-Path $root 'BO3_HLSL_Previewer'
if (Test-Path -LiteralPath $nested) {
    Write-Warning "Accidental nested project copy exists: $nested"
    if ($RemoveAccidentalNestedCopy) {
        Remove-Item -LiteralPath $nested -Recurse -Force
        Write-Host 'Removed accidental nested BO3_HLSL_Previewer folder.' -ForegroundColor Yellow
    } else {
        Write-Host 'After verifying this is the accidental patch copy, remove it with:' -ForegroundColor Yellow
        Write-Host '  .\APPLY_PHASE1G5_ROOT_FIX.ps1 -RemoveAccidentalNestedCopy' -ForegroundColor Yellow
    }
}

Write-Host ''
Write-Host 'Phase 1g.5 ROOT verification: PASS' -ForegroundColor Green
Write-Host "preview_renderer.cpp SHA256: $actualPreviewSha"
Write-Host 'Deferred HLSL is externalized through the Qt resource bundle.'
Write-Host 'The MSVC C2026 source that CI kept compiling is no longer present in the root translation unit.'
