$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$shader = Join-Path $root "resources\shaders\ape_deferred_lighting.hlsl"
$renderer = Join-Path $root "src\preview_renderer.cpp"
$rendererHeader = Join-Path $root "src\preview_renderer.h"
$main = Join-Path $root "src\main_window.cpp"
$note = Join-Path $root "PATCH_NOTES_PHASE1W.md"

$errors = @()
foreach ($f in @($shader,$renderer,$rendererHeader,$main,$note)) {
    if (!(Test-Path $f)) { $errors += "Missing $f" }
}

if ($errors.Count -eq 0) {
    $s = Get-Content $shader -Raw
    $r = Get-Content $renderer -Raw
    $h = Get-Content $rendererHeader -Raw
    $m = Get-Content $main -Raw

    if ($m -notmatch 'float2\(13\.0, 13\.0\)') { $errors += "Captured stock Gloss 13 GBuffer input not found" }
    if ($m -match 'flatTangentNormal, 1\.0, float2\(6\.0, 6\.0\)') { $errors += "Old Gloss 6 deferred default is still present" }
    if ($r -notmatch 'kApeMatchVerticalFovDegrees\s*=\s*39\.43048821f') { $errors += "Captured 39.430488-degree APE projection not found" }
    if ($r -notmatch 'XMMatrixPerspectiveFovLH\(materialFov, aspect') { $errors += "Geometry pass is not using shared material projection" }
    if ($r -notmatch 'std::tan\(MaterialPreviewVerticalFovRadians\(\) \* 0\.5f\)') { $errors += "Deferred ray path is not using shared material projection" }
    if ($m -notmatch 'SetCameraDistance\(4\.89094f\)') { $errors += "Captured APE sphere framing distance not found" }
    if ($h -notmatch 'void SetCameraDistance\(float distance\);') { $errors += "Direct camera-distance API not found" }
    if ($s -notmatch 'gbuffer1\.Load\(int3\(pixelCoord, 0\)\)') { $errors += "APE-style unfiltered NormalGloss load not found" }
    if ($s -notmatch 'depthTexture\.Load\(int3\(pixelCoord, 0\)\)') { $errors += "APE-style unfiltered depth load not found" }

    # Protect the already verified Phase 1s/1v math from accidental regression.
    if ($s -notmatch 'specNoFresnel\s*=\s*\(alpha2 \* apeSunSpecScale \* NdotL\)') { $errors += "Phase 1v NdotL direct-specular numerator regressed" }
    if ($r -notmatch 'XMVectorSet\(-apeY, apeZ, apeX, 0\.0f\)') { $errors += "Phase 1v APE->Studio world-frame conversion regressed" }
    if ($m -notmatch 'RotateCamera\(0\.0f, 22\.5f\)') { $errors += "Captured 22.5-degree APE orbit regressed" }
    if ($s -notmatch 'shadowTerm = 1\.0;') { $errors += "Phase 1s shadow-free baseline regressed" }
    if ($m -notmatch 'Phase 1w captured Gloss 13 \+ exact APE projection') { $errors += "Phase 1w runtime fingerprint not found" }
}

if ($errors.Count -gt 0) {
    Write-Host "Phase 1w verification: FAIL" -ForegroundColor Red
    $errors | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host "Phase 1w verification: PASS" -ForegroundColor Green
Write-Host "Captured Gloss 13, exact 39.430488-degree APE projection, 4.89094-radius framing, and unfiltered GBuffer reads are installed." -ForegroundColor Green
