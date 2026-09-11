$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$shader = Join-Path $root "resources\shaders\ape_deferred_lighting.hlsl"
$main = Join-Path $root "src\main_window.cpp"

foreach ($p in @($shader, $main)) {
    if (!(Test-Path $p)) { throw "Phase 1t verification failed: missing $p" }
}

$hlsl = Get-Content $shader -Raw
$ui = Get-Content $main -Raw

if ($hlsl -notmatch 'Phase 1t: literal direct-sun specular translation') {
    throw "Phase 1t verification failed: captured direct-specular block is missing."
}
if ($hlsl -notmatch 'sqrtAlpha' -or $hlsl -notmatch 'visibilityK') {
    throw "Phase 1t verification failed: captured sqrt(alpha) visibility mapping is missing."
}
if ($hlsl -notmatch 'specNoFresnel' -or $hlsl -notmatch '4\.0 \* visV \* visL \* dDenom \* dDenom') {
    throw "Phase 1t verification failed: captured no-PI direct-specular normalization is missing."
}
if ($hlsl -notmatch 'apeDiffuseLobe') {
    throw "Phase 1t verification failed: captured rough-diffuse correction is missing."
}
if ($hlsl -notmatch 'shadowTerm = 1\.0;') {
    throw "Phase 1t verification failed: Phase 1s unshadowed truth baseline was lost."
}
if ($ui -notmatch 'Phase 1t captured direct sun \+ specular') {
    throw "Phase 1t verification failed: runtime fingerprint is missing."
}

Write-Host "Phase 1t verification: PASS" -ForegroundColor Green
Write-Host "Captured APE direct specular + rough-diffuse instruction translation is installed; Phase 1s shadow-free baseline is preserved."
