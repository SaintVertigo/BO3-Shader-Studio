$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$shader = Join-Path $root "resources\shaders\ape_deferred_lighting.hlsl"
$renderer = Join-Path $root "src\preview_renderer.cpp"
$main = Join-Path $root "src\main_window.cpp"
$beginner = Join-Path $root "src\beginner_shader_builder.cpp"
$note = Join-Path $root "PATCH_NOTES_PHASE1Y.md"

$errors = @()
foreach ($f in @($shader,$renderer,$main,$beginner,$note)) {
    if (!(Test-Path $f)) { $errors += "Missing $f" }
}

if ($errors.Count -eq 0) {
    $s = Get-Content $shader -Raw
    $r = Get-Content $renderer -Raw
    $m = Get-Content $main -Raw
    $b = Get-Content $beginner -Raw

    # Phase 1y probe-direction recovery and framing.
    if ($s -notmatch 'float3 RecoverApeProbeDirectionalContrast\(') { $errors += "APE probe directional-contrast recovery missing" }
    if ($s -notmatch 'apeDirectionalContrast\s*=\s*1\.75') { $errors += "Capture-calibrated 1.75 probe contrast not installed" }
    if ($s -notmatch 'probeMean\s*=\s*max\(previewEnvironmentAmbient\.rgb') { $errors += "Probe contrast is not centered on the source mean" }
    if ($s -notmatch 'env\s*=\s*RecoverApeProbeDirectionalContrast\(env\)') { $errors += "APE env-spec path is not using recovered directional contrast" }
    if ($m -notmatch 'SetCameraDistance\(5\.25000f\)') { $errors += "APE 5.25-radius Reset framing not installed" }
    if ($m -notmatch 'Phase 1y probe contrast \+ APE framing') { $errors += "Phase 1y runtime fingerprint missing" }

    # Preserve Phase 1x normal recovery.
    if ($r -notmatch '"BO3_STUDIO_PREVIEW",\s*"1"') { $errors += "Studio preview-only shader define regressed" }
    if ($s -notmatch 'float3 ResolveApePreviewNormal\(') { $errors += "Phase 1x APE reference normal resolver regressed" }
    if ($s -notmatch 'return worldPosition \* rsqrt\(radiusSq\)') { $errors += "Phase 1x outward radial normal regressed" }
    if ($s -notmatch 'float3 N = ResolveApePreviewNormal') { $errors += "Phase 1x Final Lit normal path regressed" }
    if ($m -notmatch 'bo3NormalFrontFace = 1u') { $errors += "Phase 1x custom material preview front-face isolation regressed" }
    if ($b -notmatch 'beginnerNormalFrontFace = 1u') { $errors += "Phase 1x Beginner preview front-face isolation regressed" }

    # Preserve already capture-verified Phase 1s/1v/1w behavior.
    if ($s -notmatch 'specNoFresnel\s*=\s*\(alpha2 \* apeSunSpecScale \* NdotL\)') { $errors += "Phase 1v direct-specular numerator regressed" }
    if ($r -notmatch 'XMVectorSet\(-apeY, apeZ, apeX, 0\.0f\)') { $errors += "Phase 1v APE-to-Studio world frame regressed" }
    if ($r -notmatch 'kApeMatchVerticalFovDegrees\s*=\s*39\.43048821f') { $errors += "Phase 1w captured projection regressed" }
    if ($m -notmatch 'float2\(13\.0, 13\.0\)') { $errors += "Phase 1w captured Gloss 13 regressed" }
    if ($s -notmatch 'gbuffer1\.Load\(int3\(pixelCoord, 0\)\)') { $errors += "Phase 1w unfiltered GBuffer read regressed" }
    if ($s -notmatch 'shadowTerm = 1\.0;') { $errors += "Phase 1s shadow-free reference baseline regressed" }
}

if ($errors.Count -gt 0) {
    Write-Host "Phase 1y verification: FAIL" -ForegroundColor Red
    $errors | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host "Phase 1y verification: PASS" -ForegroundColor Green
Write-Host "APE probe directional contrast and 5.25-radius framing are installed; Phase 1x hotspot/normal recovery and Phase 1s/1v/1w lighting math are preserved." -ForegroundColor Green
