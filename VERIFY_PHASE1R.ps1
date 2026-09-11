$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$main = Get-Content -Raw (Join-Path $root "src\main_window.cpp")
$renderer = Get-Content -Raw (Join-Path $root "src\preview_renderer.cpp")
$lighting = Get-Content -Raw (Join-Path $root "resources\shaders\ape_deferred_lighting.hlsl")

$checks = @(
    @{ Text = $main; Pattern = '2\.1099775f'; Name = 'captured Day sun/exposure ratio' },
    @{ Text = $main; Pattern = '0\.2500001f'; Name = 'captured Day global-probe/exposure ratio' },
    @{ Text = $main; Pattern = '0\.771301925f, 1\.01348603f, 1\.53983426f'; Name = 'captured avgGlobalProbeColor' },
    @{ Text = $main; Pattern = 'Phase 1r captured light energy \+ depth shadow'; Name = 'runtime Phase 1r fingerprint' },
    @{ Text = $renderer; Pattern = 'DXGI_FORMAT_R16_TYPELESS'; Name = 'single typeless hardware shadow depth resource' },
    @{ Text = $renderer; Pattern = 'DXGI_FORMAT_D16_UNORM'; Name = 'D16 shadow DSV' },
    @{ Text = $renderer; Pattern = 'DXGI_FORMAT_R16_UNORM'; Name = 'R16 shadow SRV' },
    @{ Text = $renderer; Pattern = 'SlopeScaledDepthBias = 1\.0f'; Name = 'shadow raster slope bias' },
    @{ Text = $renderer; Pattern = 'context_->PSSetShader\(nullptr, nullptr, 0\)'; Name = 'depth-only shadow pass' },
    @{ Text = $renderer; Pattern = '0\.0040f'; Name = 'receiver normal bias' },
    @{ Text = $lighting; Pattern = 'previewApeGlobalProbeAverage'; Name = 'captured global probe average shader input' },
    @{ Text = $lighting; Pattern = 'SampleApeSunShadow\(worldPosition, N, NdotL\)'; Name = 'normal-aware shadow receiver' },
    @{ Text = $lighting; Pattern = 'capturedAverageRadiance \* 3\.14159265'; Name = 'captured probe floor' }
)

foreach ($check in $checks) {
    if ($check.Text -notmatch $check.Pattern) {
        throw "Phase 1r verification failed: $($check.Name)"
    }
}

if ($renderer -match 'ClearRenderTargetView\(apeShadowRTV_') {
    throw "Phase 1r verification failed: obsolete separate R16 shadow color target is still active"
}
if ($main -match '1\.70f, 0\.135f, 3\.6f, 1\.05f') {
    throw "Phase 1r verification failed: obsolete hand-tuned Day probe/sun calibration is still present"
}

Write-Host "Phase 1r verification: PASS" -ForegroundColor Green
Write-Host "Captured Day light/probe energy and the hardware depth-shadow recovery path are installed." -ForegroundColor Cyan
