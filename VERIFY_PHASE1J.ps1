$ErrorActionPreference = 'Stop'

if (-not (Test-Path '.\BO3HLSLPreviewer.pro')) {
    throw 'Run this from the real BO3_HLSL_Previewer repo root (the folder that contains BO3HLSLPreviewer.pro).'
}

$expected = @{
    '.\src\preview_renderer.cpp' = '2f3043593c681656dc9d33c1b6925f97231f01cb3c5bfacd92a236187e3d8d79'
    '.\src\main_window.cpp' = '8a96bb3d1680ecca68bfbbdc6a3b0b09808850378cc978ae45a0d5104ae23388'
    '.\resources\shaders\ape_deferred_lighting.hlsl' = '698373678ad343e869d51d637f859520f568c14e431c8788db3e511d7b8cb930'
    '.\docs\APE_REVERSE_ENGINEERING_FINDINGS.md' = 'a81209fcefe5a797be892001fda7cd84131ad5dfbebb5b06d82f28777a3522ab'
}

foreach ($entry in $expected.GetEnumerator()) {
    if (-not (Test-Path $entry.Key)) { throw "Missing Phase 1j file: $($entry.Key)" }
    $actual = (Get-FileHash -Algorithm SHA256 $entry.Key).Hash.ToLowerInvariant()
    if ($actual -ne $entry.Value) {
        throw "Phase 1j hash mismatch: $($entry.Key)`nExpected: $($entry.Value)`nActual:   $actual"
    }
}

$renderer = Get-Content '.\src\preview_renderer.cpp' -Raw
$lighting = Get-Content '.\resources\shaders\ape_deferred_lighting.hlsl' -Raw
$main = Get-Content '.\src\main_window.cpp' -Raw

$rendererChecks = @(
    'const UINT maxPreviewWidth = 8192u',
    'DXGI_FORMAT_R16G16B16A16_FLOAT',
    'CreateHalfFloatTextureSRV',
    'environmentDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP',
    'SampleLevel\(previewSampler, DirectionToEquirect\(i\.skyDirection\.xyz\), 0\.0\)',
    'composeSamplers\[2\] = \{ sampler_\.Get\(\), environmentSampler_\.Get\(\) \}'
)
foreach ($pattern in $rendererChecks) {
    if ($renderer -notmatch $pattern) { throw "Phase 1j renderer wiring check failed: $pattern" }
}

$lightingChecks = @(
    'SamplerState previewEnvironmentSampler : register\(s1\)',
    'float Bo3GlossToRoughness',
    'float cosinePower = exp2\(glossValue\)',
    'float roughness = Bo3GlossToRoughness\(gloss\)',
    'float lodFraction = saturate\(sqrt\(roughness\)\)',
    'probeBalance, 0\.72'
)
foreach ($pattern in $lightingChecks) {
    if ($lighting -notmatch $pattern) { throw "Phase 1j lighting wiring check failed: $pattern" }
}

$mainChecks = @(
    'LoadEnvironmentCubemapFaces\(faces, error, 4096, 2048\)',
    '1\.65f, 0\.060f, 3\.6f, 1\.05f',
    'Shift\+Left rotates the APE sun in world space',
    'lightDragging_ = shiftLeft'
)
foreach ($pattern in $mainChecks) {
    if ($main -notmatch $pattern) { throw "Phase 1j APE preset/control check failed: $pattern" }
}

Write-Host ''
Write-Host 'Phase 1j verification: PASS' -ForegroundColor Green
Write-Host 'Native/4K HDR sky fidelity, seam-safe sampling, compact BO3 gloss lobe, and restrained APE probe specular are installed.'
Write-Host 'Phase 1i manual sun-direction controls are also still present.'
Write-Host ''
