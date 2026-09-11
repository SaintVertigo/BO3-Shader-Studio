$ErrorActionPreference = 'Stop'

if (-not (Test-Path '.\BO3HLSLPreviewer.pro')) {
    throw 'Run this from the real BO3_HLSL_Previewer repo root (the folder that contains BO3HLSLPreviewer.pro).'
}

$expected = @{
    '.\src\main_window.cpp' = 'e7a88c485e06ad78e9f6321a4b4a605ab3f45b9824931d1cf15e8649f2d6b083'
    '.\resources\shaders\ape_deferred_lighting.hlsl' = '2c88b0f6694d5b63633675a9161e26c52aa1fdb5be6827589827098cc11630f4'
    '.\docs\APE_REVERSE_ENGINEERING_FINDINGS.md' = '883bdd5ff53f6f9ba940b1c9ab4dada2d0fdf3117d820e3e2b773d78c54c0083'
    '.\PATCH_NOTES_PHASE1K.md' = '6e68721154f7cbebf47d43f800649b0d6b65ddc87c4a5db5d3e9645ab246c353'
}

foreach ($entry in $expected.GetEnumerator()) {
    if (-not (Test-Path $entry.Key)) { throw "Missing Phase 1k file: $($entry.Key)" }
    $actual = (Get-FileHash -Algorithm SHA256 $entry.Key).Hash.ToLowerInvariant()
    if ($actual -ne $entry.Value) {
        throw "Phase 1k hash mismatch: $($entry.Key)`nExpected: $($entry.Value)`nActual:   $actual"
    }
}

$lighting = Get-Content '.\resources\shaders\ape_deferred_lighting.hlsl' -Raw
$main = Get-Content '.\src\main_window.cpp' -Raw

$lightingChecks = @(
    'float dielectricWeight = 1\.0 - saturate\(\(reflectance - 0\.04\) \* 5\.0\)',
    'float apeDielectricLodFraction = saturate\(0\.52 \+ \(1\.0 - gloss\) \* 0\.18\)',
    'float compression = rcp\(1\.0 \+ max\(probeLuminance, 0\.0\) \* 0\.30\)',
    'lerp\(probeAverage, compressedEnv, 0\.32\)',
    'float stockDielectricEnergy = lerp\(1\.0, 0\.38, dielectricWeight\)',
    'float roughness = Bo3GlossToRoughness\(gloss\)'
)
foreach ($pattern in $lightingChecks) {
    if ($lighting -notmatch $pattern) { throw "Phase 1k lighting wiring check failed: $pattern" }
}

$forbidden = @(
    'float lodFraction = saturate\(sqrt\(roughness\)\);\s*float lod = lodFraction \* maxLod;'
)
foreach ($pattern in $forbidden) {
    if ($lighting -match $pattern) { throw "Phase 1k old raw-mirror probe path is still present: $pattern" }
}

$mainChecks = @(
    'native/4K HDR sky \+ SH9 diffuse \+ APE-local blurred spec probes',
    'Shift\+Left rotates the APE sun in world space',
    'LoadEnvironmentCubemapFaces\(faces, error, 4096, 2048\)'
)
foreach ($pattern in $mainChecks) {
    if ($main -notmatch $pattern) { throw "Phase 1k retained-feature check failed: $pattern" }
}

Write-Host ''
Write-Host 'Phase 1k verification: PASS' -ForegroundColor Green
Write-Host 'APE Match now decouples the sharp direct sun lobe from the broad processed reflection probe.'
Write-Host 'Phase 1j native/4K HDR sky fidelity and Phase 1i manual sun controls are retained.'
Write-Host ''
