$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$renderer = Get-Content (Join-Path $root "src\preview_renderer.cpp") -Raw
$header = Get-Content (Join-Path $root "src\preview_renderer.h") -Raw
$main = Get-Content (Join-Path $root "src\main_window.cpp") -Raw
$deferred = Get-Content (Join-Path $root "resources\shaders\ape_deferred_lighting.hlsl") -Raw
$importer = Get-Content (Join-Path $root "src\model_import.cpp") -Raw

$checks = @(
    @{ ok = $deferred -match 'float3 ResolveApePreviewNormal\('; msg = 'Phase 1x radial normal resolver regressed' },
    @{ ok = $deferred -match 'float3 N\s*=\s*ResolveApePreviewNormal'; msg = 'Phase 1x Final Lit normal path regressed' },
    @{ ok = $deferred -match 'debugNormal\s*=\s*ResolveApePreviewNormal'; msg = 'Phase 1x Normal inspector path regressed' },

    @{ ok = $importer -match 'corner\.uv=\{first\.x,first\.y\}'; msg = 'Phase 1ac runtime XMODEL UV parity regressed' },
    @{ ok = $main -match 'SetCameraDistance\(4\.83000f\)'; msg = 'Phase 1ac fixed Reset camera regressed' },
    @{ ok = $main -match 'preview_->setMinimumSize\(1,\s*1\)'; msg = 'Phase 1aa/1ac Preview resize floor regressed' },

    @{ ok = $deferred -match 'float3 StudioToApeVisibleSkyFrame'; msg = 'Phase 1ad visible-sky frame is missing' },
    @{ ok = $deferred -match 'd\s*=\s*float3\(d\.z,\s*d\.y,\s*d\.x\)'; msg = 'Phase 1ad visible sky is still mirrored' },
    @{ ok = $deferred -match 'float3 StudioToApeProbeFrame'; msg = 'Phase 1ad baked-probe frame is missing' },
    @{ ok = $deferred -match 'd\s*=\s*float3\(d\.z,\s*d\.y,\s*-d\.x\)'; msg = 'Captured baked-probe frame regressed' },
    @{ ok = $renderer -match 'd\s*=\s*float3\(d\.z,\s*d\.y,\s*d\.x\)'; msg = 'Forward APE visible sky is still mirrored' },

    @{ ok = $deferred -match 'lerp\(3\.30,\s*4\.05'; msg = 'Phase 1ad side-probe contrast calibration is missing' },
    @{ ok = $deferred -match 'previewApeDirectCalibration'; msg = 'Phase 1ad direct diffuse/spec split is missing' },
    @{ ok = $renderer -match 'apeDirectCalibration'; msg = 'CPU direct calibration upload is missing' },
    @{ ok = $renderer -match 'ByteWidth\s*=\s*400'; msg = 'Deferred-light cbuffer size was not expanded for Phase 1ad' },

    @{ ok = $main -match '"Night".*2\.80f.*3\.80f,\s*0\.180f,\s*0\.050f,\s*0\.85f,\s*1\.0f,\s*28\.0f'; msg = 'Phase 1ad Night calibration is missing' },
    @{ ok = $main -match '"Day".*2\.1099775f,\s*0\.2500001f,\s*1\.0f,\s*1\.0f'; msg = 'Capture-derived Day direct split regressed' }
)

foreach ($check in $checks) {
    if (-not $check.ok) { throw $check.msg }
}

Write-Host "Phase 1ad verification: PASS" -ForegroundColor Green
Write-Host "Visible sky handedness, side-probe range, and Night direct/indirect balance are installed; Phase 1x hotspot and Phase 1ac UV parity are preserved."
