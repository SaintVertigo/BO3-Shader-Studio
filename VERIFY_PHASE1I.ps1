$ErrorActionPreference = 'Stop'

if (-not (Test-Path '.\BO3HLSLPreviewer.pro')) {
    throw 'Run this from the real BO3_HLSL_Previewer repo root (the folder that contains BO3HLSLPreviewer.pro).'
}

$expectedMain = 'bd191bb328b98fb2532b119c939be644f982ba7637f9dc7562260d1930f8a888'
$path = '.\src\main_window.cpp'
if (-not (Test-Path $path)) { throw "Missing Phase 1i file: $path" }
$actual = (Get-FileHash -Algorithm SHA256 $path).Hash.ToLowerInvariant()
if ($actual -ne $expectedMain) {
    throw "Phase 1i hash mismatch: $path`nExpected: $expectedMain`nActual:   $actual"
}

$main = Get-Content $path -Raw
$checks = @(
    'Shift\+Left rotates the APE sun in world space',
    'lightDragging_ = shiftLeft',
    'manualSunDirection = manualLookdev \|\| profile == MaterialPreviewProfile::ApeMatch',
    'explicit world-space sun-direction override',
    'APE lighting preset restored'
)
foreach ($pattern in $checks) {
    if ($main -notmatch $pattern) { throw "Phase 1i wiring check failed: $pattern" }
}

Write-Host ''
Write-Host 'Phase 1i verification: PASS' -ForegroundColor Green
Write-Host 'APE Match sun direction is manually adjustable again while remaining independent of camera orbit/dolly.'
Write-Host ''
