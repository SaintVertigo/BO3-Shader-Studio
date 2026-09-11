$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$expected = @{
    "src\preview_renderer.cpp" = "edbeb1514dd34046c96beaf11674fd6628e8a975470cd69fdd83601b21f11710"
    "src\main_window.cpp" = "f4e4489cfa5af9b080783afea77354c1e959c545450f373e649181977638a922"
    "resources\shaders\ape_deferred_lighting.hlsl" = "9eaa6ca0c6ec6ae122370f42d054e87debc3d9798b3a457528c3ec6b29908b15"
    "resources\learning.qrc" = "10c8dc4d0922609535c9fdda3693ceccaeadfb70f322b03246800c26865dcba2"
    "resources\preview\ape_env_brdf_rg8.bin" = "e405360f8603ccd7458b29747027a51eacfa221c85243f50f065a5fccb9ef410"
    "PATCH_NOTES_PHASE1O.md" = "63c07dad414b70d778537a419c161b7ac1587b84cdde824a51aebc9476114877"
}

foreach ($relative in $expected.Keys) {
    if (-not (Test-Path $relative)) {
        throw "Phase 1o verification failed: missing $relative"
    }
    $actual = (Get-FileHash -Algorithm SHA256 $relative).Hash.ToLowerInvariant()
    if ($actual -ne $expected[$relative]) {
        throw "Phase 1o verification failed: hash mismatch for $relative"
    }
}

$renderer = Get-Content -Raw "src\preview_renderer.cpp"
$shader = Get-Content -Raw "resources\shaders\ape_deferred_lighting.hlsl"
$main = Get-Content -Raw "src\main_window.cpp"
$qrc = Get-Content -Raw "resources\learning.qrc"

$rendererChecks = @(
    "CreateApeReflectionProbeFromLatLong",
    "CreateApeEnvironmentBrdf",
    "CreateApeShadowResources",
    "RenderApeShadowMap",
    "180.0f - lightYawDegrees_",
    "reflectionProbeMipCount_",
    "apeShadowWorldViewProj_",
    "DXGI_FORMAT_R16_UNORM",
    "D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT",
    "PreviewApeShadowMatrix : register(b0)",
    "VSSetConstantBuffers(0, 1, &cb)"
)
foreach ($text in $rendererChecks) {
    if (-not $renderer.Contains($text)) {
        throw "Phase 1o renderer wiring check failed: $text"
    }
}


if ($renderer.Contains("PreviewApeShadowMatrix : register(b14)") -or $renderer.Contains("VSSetConstantBuffers(14, 1, &cb)")) {
    throw "Phase 1o D3D11 cbuffer slot regression: invalid b14 shadow binding detected"
}

$shaderChecks = @(
    "Texture2D previewReflectionProbe : register(t7)",
    "previewApeEnvBrdf : register(t8)",
    "previewApeShadowMap : register(t9)",
    "5.0 * (1.0 - saturate(gloss))",
    "7.712947",
    "19.311527",
    "SampleApeSunShadow",
    "filtered * filtered * filtered",
    "sqrt(max(2.0 / (cosinePower + 2.0), 1e-10))"
)
foreach ($text in $shaderChecks) {
    if (-not $shader.Contains($text)) {
        throw "Phase 1o shader wiring check failed: $text"
    }
}

if (-not $main.Contains("0.947151f") -or -not $main.Contains("0.887882f") -or -not $main.Contains("r.AdjustCameraFov(4.25f)")) {
    throw "Phase 1o APE Day/camera calibration check failed"
}
if (-not $qrc.Contains("ape_env_brdf_rg8.bin")) {
    throw "Phase 1o qresource check failed"
}
if ((Get-Item "resources\preview\ape_env_brdf_rg8.bin").Length -ne 8192) {
    throw "Phase 1o BRDF LUT size check failed"
}

Write-Host "Phase 1o.1 verification: PASS" -ForegroundColor Green
Write-Host "Captured APE lighting is installed and the shadow pass now uses a valid D3D11 constant-buffer slot." -ForegroundColor Cyan
Write-Host "The baked probe is structurally capture-matched; exact six-face APE probe texels still require a full cubemap extraction." -ForegroundColor DarkGray
