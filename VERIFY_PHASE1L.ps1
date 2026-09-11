$ErrorActionPreference = 'Stop'

if (-not (Test-Path '.\BO3HLSLPreviewer.pro')) {
    throw 'Run this from the real BO3_HLSL_Previewer repo root (the folder that contains BO3HLSLPreviewer.pro).'
}

$expected = @{
    '.\src\main_window.cpp' = '558db5a842c479995a5776149db8a0871861935d2ac65c68eb3b1174be8d5df9'
    '.\src\preview_renderer.cpp' = 'c0ba57fea68312b0ac0abbb039a21bdc0ec161a469bc4eefd4f0e20ae8d5493f'
    '.\resources\shaders\ape_deferred_lighting.hlsl' = 'c3090d8a78965f11cb2ba4dc71531640b95d3e3c3e54ce17ea34435bed6d9554'
    '.\docs\APE_REVERSE_ENGINEERING_FINDINGS.md' = '7208c19e70846353c29240ca2d8fd2bd5974ab21dbfd442160b50ba102b30fa0'
    '.\PATCH_NOTES_PHASE1L.md' = 'dd714f96fb4dce51f00555ed1ecea6d8f4658ff7890fe4b81f2451842e697385'
    '.\PHASE1L_README.txt' = 'a2089543c5925bfb97b4074892190e62181cafd8bf7130eed87bae66a9d6ef87'
}

foreach ($entry in $expected.GetEnumerator()) {
    if (-not (Test-Path $entry.Key)) { throw "Missing Phase 1l file: $($entry.Key)" }
    $actual = (Get-FileHash -Algorithm SHA256 $entry.Key).Hash.ToLowerInvariant()
    if ($actual -ne $entry.Value) {
        throw "Phase 1l hash mismatch: $($entry.Key)`nExpected: $($entry.Value)`nActual:   $actual"
    }
}

$lighting = Get-Content '.\resources\shaders\ape_deferred_lighting.hlsl' -Raw
$renderer = Get-Content '.\src\preview_renderer.cpp' -Raw
$main = Get-Content '.\src\main_window.cpp' -Raw
$core = Get-Content '.\bo3_compat\shaders_stable\gfxcore\hlslcoredefines.h' -Raw
$hdr = Get-Content '.\bo3_compat\shaders_stable\lib\hdrold.hlsl' -Raw

$lightingChecks = @(
    'float3 halfVectorRaw = L \+ V',
    'bool validHalfVector = halfVectorLengthSq > 1e-8',
    'float cosinePower = exp2\(glossValue\)',
    'float normalizedLobe = \(\(cosinePower \+ 8\.0\) / \(8\.0 \* 3\.14159265\)\) \* cosineLobe',
    'float apeDielectricLodFraction = saturate\(0\.34 \+ \(1\.0 - gloss\) \* 0\.16\)',
    'float3 processedEnv = lerp\(compressedEnv, probeAverage, 0\.16 \* dielectricWeight\)',
    'float stockDielectricEnergy = lerp\(1\.0, 0\.82, dielectricWeight\)',
    '3\.14159265 \* 0\.60',
    'probeBalance, 0\.30',
    'float mappedLuminance = luminance / \(0\.78 \+ luminance\)',
    'SampleLevel\(previewEnvironmentSampler, DirectionToEquirect\(ray\), 0\.35\)'
)
foreach ($pattern in $lightingChecks) {
    if ($lighting -notmatch $pattern) { throw "Phase 1l lighting wiring check failed: $pattern" }
}

$rendererChecks = @(
    'float3 ApplyApeEnvironmentCurve',
    'float mappedLuminance = luminance / \(0\.78 \+ luminance\)',
    'SampleLevel\(previewSampler, DirectionToEquirect\(i\.skyDirection\.xyz\), 0\.35\)',
    'D3D11_FILTER_MIN_MAG_MIP_LINEAR',
    'environmentDesc\.AddressU = D3D11_TEXTURE_ADDRESS_WRAP',
    'environmentDesc\.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP'
)
foreach ($pattern in $rendererChecks) {
    if ($renderer -notmatch $pattern) { throw "Phase 1l renderer wiring check failed: $pattern" }
}

$sourceEvidenceChecks = @(
    @($core, 'float specScale;'),
    @($core, 'float globalProbeExposure;'),
    @($core, 'float3 avgGlobalProbeColor;'),
    @($core, 'float exposure; // 0x64 -> 0x68'),
    @($core, 'float3 avgCubeColor; // 0x68 -> 0x74'),
    @($hdr, 'float3 colorClamped = color \* gScene\.invExposure;')
)
foreach ($pair in $sourceEvidenceChecks) {
    if ($pair[0] -notmatch $pair[1]) { throw "Recovered ToolsGfx source evidence check failed: $($pair[1])" }
}

$forbidden = @(
    'float3 H = normalize\(L \+ V\)',
    'lerp\(probeAverage, compressedEnv, 0\.32\)',
    'float stockDielectricEnergy = lerp\(1\.0, 0\.38, dielectricWeight\)',
    'mode == 2 \|\| profile == 0',
    'DirectionToEquirect\(i\.skyDirection\.xyz\), 0\.0\)'
)
foreach ($pattern in $forbidden) {
    if ($lighting -match $pattern -or $renderer -match $pattern) {
        throw "Phase 1l old parity path is still present: $pattern"
    }
}

$mainChecks = @(
    '0\.135f, 3\.6f, 1\.05f',
    'native/4K HDR sky \+ BO3 cosine-power sun \+ processed APE probe GI',
    'Shift\+Left rotates the APE sun in world space',
    'LoadEnvironmentCubemapFaces\(faces, error, 4096, 2048\)'
)
foreach ($pattern in $mainChecks) {
    if ($main -notmatch $pattern) { throw "Phase 1l retained-feature check failed: $pattern" }
}

Write-Host ''
Write-Host 'Phase 1l verification: PASS' -ForegroundColor Green
Write-Host 'BO3 cosine-power direct specular, safe half vectors, restored probe directionality/Fresnel, and lower-contrast APE display calibration are installed.'
Write-Host 'Native HDR sky fidelity, manual APE sun controls, and the corrected BO3 gloss unpacking are retained.'
Write-Host ''
