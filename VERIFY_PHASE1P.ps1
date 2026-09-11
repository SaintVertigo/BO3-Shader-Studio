$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$expected = @{
    "src\preview_renderer.cpp" = "BCEDF11152EB147418966DDE83BFE3605C0EEC3F00F40D5821BB171E4319E87E"
    "resources\shaders\ape_deferred_lighting.hlsl" = "05CE89793FD58DB2D7C2D4F6893D523E60E6E49DE605E032F45A26EA268CE5A5"
    "docs\APE_REVERSE_ENGINEERING_FINDINGS.md" = "8E59564238B9303959121B7D0D61207DE32738493AC968E29120202AF3FDBDFF"
    "resources\preview\ape_env_brdf_rg8.bin" = "E405360F8603CCD7458B29747027A51EACFA221C85243F50F065A5FCCB9EF410"
}

foreach ($relative in $expected.Keys) {
    $path = Join-Path $root $relative
    if (-not (Test-Path $path)) {
        throw "Phase 1p file missing: $relative"
    }
    $actual = (Get-FileHash -Algorithm SHA256 $path).Hash.ToUpperInvariant()
    if ($actual -ne $expected[$relative]) {
        throw "Phase 1p hash mismatch: $relative"
    }
}

$hlsl = Get-Content (Join-Path $root "resources\shaders\ape_deferred_lighting.hlsl") -Raw
$cpp  = Get-Content (Join-Path $root "src\preview_renderer.cpp") -Raw

$hlslChecks = @(
    "TextureCube previewReflectionProbe : register(t7)",
    "DecodeBo3LightingGlossSignal",
    "0.00146627566",
    "2.00982332",
    "5.0 * (1.0 - saturate(lightingGloss))",
    "SampleApeEnvBrdf(NdotV, lightingGloss)",
    "(1.0 - specColor) * dfg.x + specColor * dfg.y"
)
foreach ($needle in $hlslChecks) {
    if (-not $hlsl.Contains($needle)) {
        throw "Phase 1p HLSL wiring check failed: $needle"
    }
}

$cppChecks = @(
    "constexpr UINT probeSize = 256",
    "constexpr UINT probeMipLevels = 7",
    "D3D11_RESOURCE_MISC_TEXTURECUBE",
    "D3D11_SRV_DIMENSION_TEXTURECUBE",
    "ImportanceSampleCosinePower",
    "neutralNormal{128, 128, 0, 255}",
    "tangentXY = sampledNormal.xy * 1.9921875 - 1.0"
)
foreach ($needle in $cppChecks) {
    if (-not $cpp.Contains($needle)) {
        throw "Phase 1p probe wiring check failed: $needle"
    }
}

if ($cpp.Contains("GenerateMips(reflectionProbeSRV_")) {
    throw "Phase 1p regression: APE reflection probe still uses GenerateMips()."
}

Write-Host "Phase 1p verification: PASS" -ForegroundColor Green
Write-Host "Captured BO3 neutral-normal convention, APE packed gloss, EnvBRDF split-sum weighting, and native 256x256x6x7 cube-probe wiring are installed." -ForegroundColor Cyan
