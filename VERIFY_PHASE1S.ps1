$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

$hlslPath = Join-Path $root "resources\shaders\ape_deferred_lighting.hlsl"
$cppPath  = Join-Path $root "src\preview_renderer.cpp"
$hdrPath  = Join-Path $root "src\preview_renderer.h"
$mainPath = Join-Path $root "src\main_window.cpp"

foreach ($p in @($hlslPath, $cppPath, $hdrPath, $mainPath)) {
    if (!(Test-Path $p)) { throw "Phase 1s verification failed: missing $p" }
}

$hlsl = Get-Content $hlslPath -Raw
$cpp  = Get-Content $cppPath -Raw
$hdr  = Get-Content $hdrPath -Raw
$main = Get-Content $mainPath -Raw

if ($hlsl -notmatch 'float3\s+L\s*=\s*normalize\s*\(\s*previewLightDirIntensity\.xyz\s*\)') {
    throw "Phase 1s verification failed: captured +wldDir sun convention is missing."
}
if ($hlsl -notmatch 'diffuse\s*=\s*albedo\s*\*\s*NdotL\s*;') {
    throw "Phase 1s verification failed: capture-derived direct diffuse equation is missing."
}
if ($hlsl -notmatch 'shadowTerm\s*=\s*1\.0\s*;') {
    throw "Phase 1s verification failed: unshadowed APE direct-sun baseline is missing."
}
if ($cpp -notmatch 'data\.apeShadowParams\s*=\s*\{1\.0f\s*/\s*1024\.0f,\s*2\.0f\s*/\s*65535\.0f,\s*0\.0f') {
    throw "Phase 1s verification failed: guessed APE shadow lookup is still enabled."
}
if ($cpp -match '(?m)^\s*RenderApeShadowMap\(\);\s*$') {
    throw "Phase 1s verification failed: guessed single-camera APE shadow pass is still active."
}
if ($hdr -notmatch 'SetApeGlobalProbeAverageColor' -or $cpp -notmatch 'PreviewRenderer::SetApeGlobalProbeAverageColor') {
    throw "Phase 1s verification failed: Phase 1r.1 PreviewRenderer API hotfix is missing."
}
if ($main -notmatch 'Phase 1s captured direct sun') {
    throw "Phase 1s verification failed: runtime fingerprint is missing."
}

Write-Host "Phase 1s verification: PASS" -ForegroundColor Green
Write-Host "Captured APE direct-sun equation is active and the incomplete t40-less shadow approximation is disabled." -ForegroundColor Cyan
