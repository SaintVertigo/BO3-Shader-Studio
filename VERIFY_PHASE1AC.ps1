$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$renderer = Get-Content (Join-Path $root "src\preview_renderer.cpp") -Raw
$window = Get-Content (Join-Path $root "src\main_window.cpp") -Raw
$importer = Get-Content (Join-Path $root "src\model_import.cpp") -Raw
$deferred = Get-Content (Join-Path $root "resources\shaders\ape_deferred_lighting.hlsl") -Raw

$checks = @(
    @{ ok = $importer.Contains('corner.uv={first.x,first.y};'); msg = 'BO3 XMODEL_BIN runtime UV preservation missing' },
    @{ ok = $renderer.Contains('apeProceduralSphereUvFallback'); msg = 'APE procedural sphere UV fallback correction missing' },
    @{ ok = $renderer.Contains('apeProceduralSphereUvFallback ? -materialUvScaleV_ : materialUvScaleV_'); msg = 'APE fallback V mirror missing' },
    @{ ok = $window.Contains('r.SetCameraDistance(4.83000f)'); msg = '4.83-radius fixed Reset camera missing' },
    @{ ok = -not $window.Contains('preview_->setMinimumSize(48, 24)'); msg = 'legacy 48x24 Preview resize clamp still present' },
    @{ ok = $window.Contains('preview_->setMinimumSize(1, 1)'); msg = '1x1 Preview safety floor missing' },
    @{ ok = $deferred -match 'VisibleSkyDirectionToEquirect\(ray\),\s*0\.35'; msg = 'APE visible-sky mip-linear LOD 0.35 missing' },
    @{ ok = $deferred.Contains('lerp(2.58, 3.07, grazing * grazing)'); msg = 'final APE probe directional-range calibration missing' },
    @{ ok = $deferred -match 'float3 ResolveApePreviewNormal\('; msg = 'Phase 1x radial normal resolver regressed' },
    @{ ok = $deferred -match 'float3 N\s*=\s*ResolveApePreviewNormal'; msg = 'Phase 1x Final Lit normal path regressed' },
    @{ ok = $deferred -match 'debugNormal\s*=\s*ResolveApePreviewNormal'; msg = 'Phase 1x Normal inspector path regressed' },
    @{ ok = $deferred.Contains('d = float3(d.z, d.y, -d.x);'); msg = 'Phase 1z APE environment frame regressed' },
    @{ ok = $window.Contains('r.SetApeProbeRotationDegrees(134.75f)'); msg = 'captured Day probe yaw regressed' },
    @{ ok = $window -match '"Day".*172\.75f'; msg = 'visible Day sky yaw regressed' },
    @{ ok = $renderer.Contains('kApeMatchVerticalFovDegrees = 39.43048821f'); msg = 'captured APE vertical FOV regressed' }
)

foreach ($check in $checks) {
    if (-not $check.ok) { throw $check.msg }
}

Write-Host "Phase 1ac verification: PASS" -ForegroundColor Green
Write-Host "Native APE UV parity, 4.83-radius fixed camera, final probe range, mip-filtered sky, and unconstrained Preview resize are installed; Phase 1x hotspot recovery is preserved." -ForegroundColor Cyan
