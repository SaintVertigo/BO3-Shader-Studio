$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$shader = Join-Path $root "resources\shaders\ape_deferred_lighting.hlsl"
$renderer = Join-Path $root "src\preview_renderer.cpp"
$main = Join-Path $root "src\main_window.cpp"
$beginner = Join-Path $root "src\beginner_shader_builder.cpp"
$note = Join-Path $root "PATCH_NOTES_PHASE1X.md"

$errors = @()
foreach ($f in @($shader,$renderer,$main,$beginner,$note)) {
    if (!(Test-Path $f)) { $errors += "Missing $f" }
}

if ($errors.Count -eq 0) {
    $s = Get-Content $shader -Raw
    $r = Get-Content $renderer -Raw
    $m = Get-Content $main -Raw
    $b = Get-Content $beginner -Raw

    if ($r -notmatch '"BO3_STUDIO_PREVIEW",\s*"1"') { $errors += "Studio preview-only shader define not installed" }
    if ($r -notmatch 'case PreviewMesh::Sphere:\s*previewMeshKind\s*=\s*1\.0f') { $errors += "Sphere mesh-kind signal not installed" }
    if ($s -notmatch 'float3 ResolveApePreviewNormal\(') { $errors += "APE reference normal resolver missing" }
    if ($s -notmatch 'profile\s*==\s*0\s*&&\s*meshKind\s*==\s*1') { $errors += "APE Match Sphere radial-normal gate missing" }
    if ($s -notmatch 'ReconstructPreviewWorldPosition\(uv, depth, viewRay\)') { $errors += "Radial normal is not using reconstructed surface position" }
    if ($s -notmatch 'return worldPosition \* rsqrt\(radiusSq\)') { $errors += "Outward radial sphere normal normalization missing" }
    if ($s -notmatch 'debugNormal = ResolveApePreviewNormal') { $errors += "Normal inspector is not using the resolved reference normal" }
    if ($s -notmatch 'float3 N = ResolveApePreviewNormal') { $errors += "Final BRDF is not using the resolved reference normal" }
    if ($m -notmatch 'bo3NormalFrontFace = 1u') { $errors += "Custom material preview front-face isolation missing" }
    if ($b -notmatch 'beginnerNormalFrontFace = 1u') { $errors += "Beginner material preview front-face isolation missing" }

    # Preserve already capture-verified Phase 1s/1v/1w behavior.
    if ($s -notmatch 'specNoFresnel\s*=\s*\(alpha2 \* apeSunSpecScale \* NdotL\)') { $errors += "Phase 1v direct-specular numerator regressed" }
    if ($r -notmatch 'XMVectorSet\(-apeY, apeZ, apeX, 0\.0f\)') { $errors += "Phase 1v APE-to-Studio world frame regressed" }
    if ($r -notmatch 'kApeMatchVerticalFovDegrees\s*=\s*39\.43048821f') { $errors += "Phase 1w captured projection regressed" }
    if ($m -notmatch 'float2\(13\.0, 13\.0\)') { $errors += "Phase 1w captured Gloss 13 regressed" }
    if ($s -notmatch 'gbuffer1\.Load\(int3\(pixelCoord, 0\)\)') { $errors += "Phase 1w unfiltered GBuffer read regressed" }
    if ($s -notmatch 'shadowTerm = 1\.0;') { $errors += "Phase 1s shadow-free reference baseline regressed" }
    if ($m -notmatch 'Phase 1x APE sphere normal recovery') { $errors += "Phase 1x runtime fingerprint missing" }
}

if ($errors.Count -gt 0) {
    Write-Host "Phase 1x verification: FAIL" -ForegroundColor Red
    $errors | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host "Phase 1x verification: PASS" -ForegroundColor Green
Write-Host "APE Match Sphere radial reference normals and preview-only front-face isolation are installed; Phase 1s/1v/1w lighting math is preserved." -ForegroundColor Green
