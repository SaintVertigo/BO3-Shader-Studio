$ErrorActionPreference = 'Stop'

if (-not (Test-Path '.\BO3HLSLPreviewer.pro')) {
    throw 'Run this from the real BO3_HLSL_Previewer repo root (the folder that contains BO3HLSLPreviewer.pro).'
}

$expected = @{
    '.\src\main_window.cpp' = '32a304a5937387563e9b190b9304e241532eb8e12a104dbc41fdcc174ab7f394'
    '.\src\preview_renderer.cpp' = 'e1f2d57afb9fb7c90c0a8fab07265d10f2c30244fe53fabfa3772f2e8719df55'
    '.\resources\shaders\ape_deferred_lighting.hlsl' = '2776d95eac79e01ccbe3c441da2af71cdedc8b5112fa9b6f91a80e7c7c6755d5'
    '.\docs\APE_REVERSE_ENGINEERING_FINDINGS.md' = 'a4a5c7e3a16072aef138ef0b8d511840babb0ed4512d226de5f97a6dad35a305'
    '.\PATCH_NOTES_PHASE1M.md' = 'b93422eedfd6bc2fe8977fdda72e118b1161baa8730a3d3223572764a2472a91'
    '.\PHASE1M_README.txt' = 'd4d98582b4c292d3a43bb4f6c21a919ff68d38a997b887be72f58f806080985f'
}

foreach ($entry in $expected.GetEnumerator()) {
    if (-not (Test-Path $entry.Key)) { throw "Missing Phase 1m file: $($entry.Key)" }
    $actual = (Get-FileHash -Algorithm SHA256 $entry.Key).Hash.ToLowerInvariant()
    if ($actual -ne $entry.Value) {
        throw "Phase 1m hash mismatch: $($entry.Key)`nExpected: $($entry.Value)`nActual:   $actual"
    }
}

$renderer = Get-Content '.\src\preview_renderer.cpp' -Raw
$main = Get-Content '.\src\main_window.cpp' -Raw
$lighting = Get-Content '.\resources\shaders\ape_deferred_lighting.hlsl' -Raw
$notes = Get-Content '.\docs\APE_REVERSE_ENGINEERING_FINDINGS.md' -Raw

$rendererChecks = @(
    'environmentPitchDegrees_ = 0\.0f',
    'lightPitchDegrees_ = wrapSignedDegrees\(lightPitchDegrees_ \+ pitchDeltaDegrees\)',
    'environmentRotationDegrees_ - yawDeltaDegrees',
    'environmentPitchDegrees_ - pitchDeltaDegrees',
    'const float yawDelta = wrapSignedDegrees\(nextYaw - lightYawDegrees_\)',
    'const float pitchDelta = wrapSignedDegrees\(nextPitch - lightPitchDegrees_\)',
    'environmentPitchDegrees_ \* \(3\.14159265358979323846f / 180\.0f\)',
    'float pitch = previewApeSettings\.w',
    'd\.yz = float2\(d\.y \* cp - d\.z \* sp, d\.y \* sp \+ d\.z \* cp\)'
)
foreach ($pattern in $rendererChecks) {
    if ($renderer -notmatch $pattern) { throw "Phase 1m renderer wiring check failed: $pattern" }
}

$lightingChecks = @(
    'float pitch = previewApeSettings\.w',
    'd\.yz = float2\(d\.y \* cp - d\.z \* sp, d\.y \* sp \+ d\.z \* cp\)',
    'dark moving APE patch',
    'real APE viewport behavior',
    'float cosinePower = exp2\(glossValue\)',
    'float3 processedEnv = lerp\(compressedEnv, probeAverage, 0\.16 \* dielectricWeight\)'
)
foreach ($pattern in $lightingChecks) {
    if ($lighting -notmatch $pattern) { throw "Phase 1m lighting wiring check failed: $pattern" }
}

$mainChecks = @(
    'Light pitch", -180, 180',
    'sun and HDR environment move together and can pass over/under the asset continuously',
    'HDR environment together, while camera orbit remains independent',
    'const float elevation = 180\.0f - p\.ssiPitch',
    'r\.SetEnvironmentRotationDegrees\(p\.environmentRotation\)'
)
foreach ($pattern in $mainChecks) {
    if ($main -notmatch $pattern) { throw "Phase 1m UI/preset wiring check failed: $pattern" }
}

$forbidden = @(
    'lightPitchDegrees_ = std::clamp\(lightPitchDegrees_ \+ pitchDeltaDegrees, -89\.0f, 89\.0f\)',
    'lightPitchDegrees_ = std::clamp\(pitchDeg, -89\.0f, 89\.0f\)',
    'makeSliderRow\("Light pitch", -89, 89',
    'which is the moving\s+black circular defect visible in the comparison recording'
)
foreach ($pattern in $forbidden) {
    if ($renderer -match $pattern -or $main -match $pattern -or $lighting -match $pattern) {
        throw "Phase 1m old/stuck-light path is still present: $pattern"
    }
}

if ($notes -notmatch 'Phase 1m — APE light-rig motion correction') {
    throw 'Phase 1m reverse-engineering note is missing.'
}

Write-Host ''
Write-Host 'Phase 1m verification: PASS' -ForegroundColor Green
Write-Host 'APE Match now rotates sun + HDR environment as one rig and allows continuous over/under light rotation without the old +/-89 degree clamp.'
Write-Host 'The supplied dark APE sphere patch is retained as a parity reference rather than being misclassified as the mouse cursor.'
Write-Host ''
