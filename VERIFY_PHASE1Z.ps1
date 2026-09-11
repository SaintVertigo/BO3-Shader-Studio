$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$renderer = Join-Path $root 'src\preview_renderer.cpp'
$window = Join-Path $root 'src\main_window.cpp'
$hlsl = Join-Path $root 'resources\shaders\ape_deferred_lighting.hlsl'
$notes = Join-Path $root 'PATCH_NOTES_PHASE1Z.md'
$errors = @()
foreach ($p in @($renderer,$window,$hlsl,$notes)) {
    if (-not (Test-Path $p)) { $errors += "Missing $p" }
}
if ($errors.Count -eq 0) {
    $r = Get-Content $renderer -Raw
    $m = Get-Content $window -Raw
    $h = Get-Content $hlsl -Raw
    if ($h -notmatch 'float3\s+StudioToApeEnvironmentFrame\(') { $errors += 'Full APE environment-frame conversion missing' }
    if ($h -notmatch 'float3\s*\(\s*d\.z\s*,\s*d\.y\s*,\s*-d\.x\s*\)') { $errors += 'Studio->APE sampling-frame mapping is not installed' }
    if ($h -match 'ApplyApeEnvironmentHandedness') { $errors += 'Legacy X-only environment transform still present' }
    if ($r -notmatch 'environmentRotationDegrees_\s*-\s*yawDeltaDegrees') { $errors += 'Relative visible-sky yaw coupling missing' }
    if ($r -match '90\.0f\s*-\s*lightYawDegrees_') { $errors += 'Legacy absolute visible-sky yaw override still present' }
    if ($m -notmatch '134\.75f') { $errors += 'Captured Day environment/probe base orientation missing' }
    if ($m -notmatch 'SetCameraDistance\(4\.85000f\)') { $errors += 'APE viewport-relative 4.85-radius Reset framing missing' }
    if ($m -notmatch 'Phase 1z environment frame \+ viewport scale') { $errors += 'Phase 1z runtime fingerprint missing' }
    if ($h -notmatch 'ResolveApePreviewNormal') { $errors += 'Phase 1x normal recovery regressed' }
    if ($h -notmatch 'RecoverApeProbeDirectionalContrast') { $errors += 'Phase 1y probe contrast recovery regressed' }
    if ($h -notmatch 'apeDirectionalContrast\s*=\s*1\.75') { $errors += 'Phase 1y directional contrast value changed unexpectedly' }
    if ($h -notmatch 'specNoFresnel\s*=\s*\(alpha2\s*\*\s*apeSunSpecScale\s*\*\s*NdotL\)') { $errors += 'Verified direct-specular numerator regressed' }
}
if ($errors.Count -gt 0) {
    Write-Host 'Phase 1z verification: FAIL' -ForegroundColor Red
    $errors | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}
Write-Host 'Phase 1z verification: PASS' -ForegroundColor Green
Write-Host 'Full APE environment frame, captured 134.75-degree Day base orientation, and 4.85-radius viewport scaling are installed; Phase 1x/1y lighting fixes are preserved.' -ForegroundColor Cyan
