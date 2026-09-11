$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$shader = Join-Path $root "resources\shaders\ape_deferred_lighting.hlsl"
$main = Join-Path $root "src\main_window.cpp"
$note = Join-Path $root "PATCH_NOTES_PHASE1U.md"

$errors = @()
if (!(Test-Path $shader)) { $errors += "Missing $shader" }
if (!(Test-Path $main)) { $errors += "Missing $main" }
if (!(Test-Path $note)) { $errors += "Missing $note" }

if ($errors.Count -eq 0) {
    $s = Get-Content $shader -Raw
    $m = Get-Content $main -Raw
    if ($s -notmatch 'specNoFresnel\s*=\s*\(alpha2 \* apeSunSpecScale\)') { $errors += "Correct captured specular numerator not found" }
    if ($s -match 'specNoFresnel\s*=\s*\(alpha2 \* NdotL') { $errors += "Old Phase 1t NdotL numerator is still present" }
    if ($s -notmatch 'float visL = NdotL \* oneMinusK \+ visibilityK;') { $errors += "Captured visL term not found" }
    if ($s -notmatch 'shadowTerm = 1\.0;') { $errors += "Phase 1s shadow-free baseline not found" }
    if ($m -notmatch 'Phase 1u captured direct sun \+ exact specular') { $errors += "Phase 1u runtime fingerprint not found" }
}

if ($errors.Count -gt 0) {
    Write-Host "Phase 1u verification: FAIL" -ForegroundColor Red
    $errors | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host "Phase 1u verification: PASS" -ForegroundColor Green
Write-Host "Captured APE direct-specular numerator is instruction-faithful; Phase 1s direct diffuse remains intact." -ForegroundColor Green
