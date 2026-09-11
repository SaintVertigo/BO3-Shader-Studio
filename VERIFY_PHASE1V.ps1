$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$shader = Join-Path $root "resources\shaders\ape_deferred_lighting.hlsl"
$renderer = Join-Path $root "src\preview_renderer.cpp"
$main = Join-Path $root "src\main_window.cpp"
$note = Join-Path $root "PATCH_NOTES_PHASE1V.md"

$errors = @()
foreach ($f in @($shader,$renderer,$main,$note)) { if (!(Test-Path $f)) { $errors += "Missing $f" } }

if ($errors.Count -eq 0) {
    $s = Get-Content $shader -Raw
    $r = Get-Content $renderer -Raw
    $m = Get-Content $main -Raw
    if ($s -notmatch 'specNoFresnel\s*=\s*\(alpha2 \* apeSunSpecScale \* NdotL\)') { $errors += "Captured NdotL specular numerator not found" }
    if ($r -notmatch 'XMVectorSet\(-apeY, apeZ, apeX, 0\.0f\)') { $errors += "APE->Studio world-frame conversion not found" }
    if ($r -notmatch 'CurrentPreviewLightDirection\(\)') { $errors += "Central light-direction helper not found" }
    if ($m -notmatch 'RotateCamera\(0\.0f, 22\.5f\)') { $errors += "Captured 22.5-degree APE camera reference not found" }
    if ($m -notmatch 'Phase 1v captured direct sun \+ coordinate-aligned specular') { $errors += "Phase 1v runtime fingerprint not found" }
    if ($s -notmatch 'shadowTerm = 1\.0;') { $errors += "Phase 1s shadow-free baseline not preserved" }
}

if ($errors.Count -gt 0) {
    Write-Host "Phase 1v verification: FAIL" -ForegroundColor Red
    $errors | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host "Phase 1v verification: PASS" -ForegroundColor Green
Write-Host "APE world-frame transform, 22.5-degree reference camera, and instruction-faithful direct specular are installed." -ForegroundColor Green
