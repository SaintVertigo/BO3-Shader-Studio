$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Renderer = Join-Path $Root "src\preview_renderer.cpp"
$Header = Join-Path $Root "src\preview_renderer.h"
$Window = Join-Path $Root "src\main_window.cpp"
$Beginner = Join-Path $Root "src\beginner_shader_builder.cpp"
$Deferred = Join-Path $Root "resources\shaders\ape_deferred_lighting.hlsl"
$Notes = Join-Path $Root "PATCH_NOTES_PHASE1AB.md"

foreach ($p in @($Renderer,$Header,$Window,$Beginner,$Deferred,$Notes)) {
    if (!(Test-Path $p)) { throw "Missing Phase 1ab file: $p" }
}

$r = Get-Content $Renderer -Raw
$h = Get-Content $Header -Raw
$w = Get-Content $Window -Raw
$b = Get-Content $Beginner -Raw
$d = Get-Content $Deferred -Raw

$checks = @(
    @{ ok = $r.Contains("TransformApeReferenceMeshToStudio"); msg = "APE reference mesh frame transform missing" },
    @{ ok = $r.Contains("v.position = {-p[1], p[2], p[0]}"); msg = "APE -> Studio position transform missing" },
    @{ ok = $r.Contains("std::swap(imported.indices[i + 1], imported.indices[i + 2])"); msg = "reference-mesh winding correction missing" },
    @{ ok = $h.Contains("SetApeProbeRotationDegrees"); msg = "separate baked-probe rotation API missing" },
    @{ ok = $w.Contains("r.SetCameraDistance(4.38000f)"); msg = "4.38-radius fixed Reset camera missing" },
    @{ ok = $w.Contains("setMinimumSize(1, 1)"); msg = "1x1 preview collapse floor missing" },
    @{ ok = $w.Contains("172.75f"); msg = "Day visible-sky 172.75-degree orientation missing" },
    @{ ok = $w.Contains("r.SetApeProbeRotationDegrees(134.75f)"); msg = "Day baked-probe 134.75-degree orientation missing" },
    @{ ok = $d.Contains("RecoverApeProbeDirectionalContrast(float3 probeSample, float ndotv)"); msg = "view-dependent APE probe contrast recovery missing" },
    @{ ok = $d.Contains("lerp(2.18, 2.38, grazing * grazing)"); msg = "calibrated probe range missing" },
    @{ ok = $d.Contains("env = RecoverApeProbeDirectionalContrast(env, NdotV)"); msg = "probe contrast call does not use NdotV" }
)

foreach ($c in $checks) { if (!$c.ok) { throw $c.msg } }

# Guard the known-good Phase 1x hotspot/reference-normal path against accidental regression.
# Phase 1x's resolver is named ResolveApePreviewNormal (not ResolveApeReferenceNormal).
$phase1xChecks = @(
    @{ ok = $r -match '"BO3_STUDIO_PREVIEW",\s*"1"'; msg = "Phase 1x Studio preview-only shader define regressed" },
    @{ ok = $r -match 'case PreviewMesh::Sphere:\s*previewMeshKind\s*=\s*1\.0f'; msg = "Phase 1x sphere mesh-kind signal regressed" },
    @{ ok = $d -match 'float3 ResolveApePreviewNormal\('; msg = "Phase 1x radial normal resolver regressed" },
    @{ ok = $d -match 'profile\s*==\s*0\s*&&\s*meshKind\s*==\s*1'; msg = "Phase 1x APE Match sphere radial-normal gate regressed" },
    @{ ok = $d -match 'ReconstructPreviewWorldPosition\(uv, depth, viewRay\)'; msg = "Phase 1x reconstructed-position normal path regressed" },
    @{ ok = $d -match 'return worldPosition \* rsqrt\(radiusSq\)'; msg = "Phase 1x outward radial normalization regressed" },
    @{ ok = $d -match 'debugNormal\s*=\s*ResolveApePreviewNormal'; msg = "Phase 1x Normal inspector path regressed" },
    @{ ok = $d -match 'float3 N\s*=\s*ResolveApePreviewNormal'; msg = "Phase 1x Final Lit normal path regressed" },
    @{ ok = $w -match 'bo3NormalFrontFace\s*=\s*1u'; msg = "Phase 1x custom-material preview front-face isolation regressed" },
    @{ ok = $b -match 'beginnerNormalFrontFace\s*=\s*1u'; msg = "Phase 1x beginner preview front-face isolation regressed" }
)
foreach ($c in $phase1xChecks) { if (!$c.ok) { throw $c.msg } }

if (!$d.Contains("float lod = 5.0 * (1.0 - saturate(lightingGloss))")) { throw "captured probe LOD regressed" }
if (!$d.Contains("Bo3LightingGlossToAlpha")) { throw "captured direct-spec gloss conversion regressed" }

Write-Host "Phase 1ab verification: PASS" -ForegroundColor Green
Write-Host "APE mesh/UV frame, separate Day sky+probe yaw, fixed 4.38-radius camera, calibrated probe range, and near-unconstrained preview resize are installed."
