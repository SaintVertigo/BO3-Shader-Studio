$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$renderer = Get-Content (Join-Path $root "src\preview_renderer.cpp") -Raw
$header = Get-Content (Join-Path $root "src\preview_renderer.h") -Raw
$main = Get-Content (Join-Path $root "src\main_window.cpp") -Raw
$deferred = Get-Content (Join-Path $root "resources\shaders\ape_deferred_lighting.hlsl") -Raw
$importer = Get-Content (Join-Path $root "src\model_import.cpp") -Raw
$versionJson = Get-Content (Join-Path $root "version.json") -Raw | ConvertFrom-Json
$referencePath = Join-Path $root "presets\ape_stock_reference.json"
if (-not (Test-Path -LiteralPath $referencePath)) { throw "0.2 APE stock reference manifest is missing" }
$reference = Get-Content $referencePath -Raw | ConvertFrom-Json

$checks = @(
    # Visible release line
    @{ ok = ([string]$versionJson.displayVersion -eq '0.2'); msg = 'Visible application version is not 0.2' },
    @{ ok = $main -match 'displayVersion_\s*=\s*"0\.2"'; msg = 'Main-window 0.2 fallback version is missing' },

    # Shipped APE model set from code.gdt
    @{ ok = $header -match 'Cylinder\s*=\s*3' -and $header -match 'Monkey\s*=\s*4'; msg = 'APE Cylinder/Monkey preview mesh enums are missing' },
    @{ ok = $main -match 'p7_ape_preview_plane_LOD0\.XMODEL_BIN'; msg = 'Official APE plane XMODEL_BIN path is missing' },
    @{ ok = $main -match 'ape_preview_cylinder\.XMODEL_BIN'; msg = 'Official APE cylinder XMODEL_BIN path is missing' },
    @{ ok = $main -match 'ape_preview_monkey\.XMODEL_BIN'; msg = 'Official APE monkey XMODEL_BIN path is missing' },
    @{ ok = $renderer -match 'apeCylinderMesh_' -and $renderer -match 'apeMonkeyMesh_'; msg = 'Renderer storage for native APE Cylinder/Monkey is missing' },
    @{ ok = $main -match 'share/raw/' -and $main -match 'resolveApeAssetPath'; msg = 'APE asset-root resolver no longer accepts share/raw layouts' },

    # SSI / GDT source truth
    @{ ok = $main -match 'srgbToLinear' -and $main -match '0\.8941f' -and $main -match '0\.9764f' -and $main -match '0\.7681509943f' -and $main -match '0\.7912983684f'; msg = 'Exact stock SSI colorSRGB inputs or linear conversion are missing' },
    @{ ok = $main -match 'kApeVisibleSkyGdtYawOffset\s*=\s*99\.0f'; msg = 'Captured +99 degree visible-sky GDT frame offset is missing' },
    @{ ok = $main -match 'kApeProbeGdtYawOffset\s*=\s*59\.75f'; msg = 'Captured +59.75 degree baked-probe GDT frame offset is missing' },
    @{ ok = $main -match '"sky_hdr"\s*,\s*0\.0f\s*,\s*1097\.5f\s*,\s*8000\.0f\s*,\s*10\.1000052244f'; msg = 'Morning stock sky-material values are missing' },
    @{ ok = $main -match '"sky_latlong_hdr"\s*,\s*75\.0f\s*,\s*2048\.0f\s*,\s*65000\.0f\s*,\s*13\.5f'; msg = 'Day stock sky-material values are missing' },
    @{ ok = $main -match '"sky_latlong_hdr"\s*,\s*110\.0f\s*,\s*107\.63f\s*,\s*65000\.0f\s*,\s*10\.15f'; msg = 'Sunset stock sky-material values are missing' },
    @{ ok = $main -match '"sky_hdr"\s*,\s*70\.0f\s*,\s*0\.75f\s*,\s*8000\.0f\s*,\s*4\.4f'; msg = 'Night stock sky-material values are missing' },
    @{ ok = $main -match 'skyScaleRGB.*obsolete' -or $main -match 'skyScaleRgbGdt.*obsolete'; msg = 'skyScaleRGB obsolete-source warning is missing' },
    @{ ok = $reference.environments.Count -eq 4; msg = 'APE stock reference manifest does not contain four environments' },
    @{ ok = $reference.referenceMaterial.absoluteGlossDomainMax -eq 17; msg = 'Reference manifest no longer records the BO3 0..17 gloss domain' },
    @{ ok = $reference.referenceMaterial.glossRangeMax -eq 13; msg = 'Reference manifest no longer records t7_script_wall primary gloss max 13' },

    # Existing high-value parity invariants
    @{ ok = $deferred -match 'float3 ResolveApePreviewNormal\('; msg = 'Phase 1x radial normal resolver regressed' },
    @{ ok = $deferred -match 'profile\s*==\s*0\s*&&\s*meshKind\s*==\s*1'; msg = 'Phase 1x sphere-only gate regressed' },
    @{ ok = $deferred -match 'ReconstructPreviewWorldPosition\(uv,\s*depth,\s*viewRay\)' -and $deferred -match 'return worldPosition \* rsqrt\(radiusSq\)'; msg = 'Phase 1x radial normal reconstruction regressed' },
    @{ ok = $deferred -match 'debugNormal\s*=\s*ResolveApePreviewNormal' -and $deferred -match 'float3 N\s*=\s*ResolveApePreviewNormal'; msg = 'Phase 1x inspector/final-lit call sites regressed' },
    @{ ok = $importer -match 'corner\.uv=\{first\.x,first\.y\}'; msg = 'Native BO3 XMODEL_BIN runtime UV parity regressed' },
    @{ ok = $renderer -match 'apeProceduralSphereUvFallback' -and $renderer -match '\?\s*-materialUvScaleV_\s*:\s*materialUvScaleV_'; msg = 'Procedural APE sphere V compensation regressed' },
    @{ ok = $renderer -match '39\.43048821f'; msg = 'Captured APE vertical FOV regressed' },
    @{ ok = $main -match 'SetCameraDistance\(4\.83000f\)'; msg = 'Fixed APE Reset camera distance regressed' },
    @{ ok = $deferred -match 'd\s*=\s*float3\(d\.z,\s*d\.y,\s*d\.x\)' -and $deferred -match 'd\s*=\s*float3\(d\.z,\s*d\.y,\s*-d\.x\)'; msg = 'Phase 1ad visible-sky/probe handedness split regressed' },
    @{ ok = $deferred -match 'lerp\(3\.30,\s*4\.05'; msg = 'Phase 1ad side-probe range calibration regressed' },
    @{ ok = $main -match '"Night".*2\.80f.*3\.80f,\s*0\.180f,\s*0\.050f,\s*0\.85f,\s*1\.0f,\s*28\.0f'; msg = 'Successful Phase 1ad Night direct/indirect calibration regressed' },
    @{ ok = $renderer -match '\(13\.0f\s*/\s*17\.0f\)'; msg = 't7_script_wall Gloss 13 on BO3 0..17 domain regressed' }
)

foreach ($check in $checks) {
    if (-not $check.ok) { throw $check.msg }
}

Write-Host "BO3 Shader Studio 0.2 verification: PASS" -ForegroundColor Green
Write-Host "GDT/SSI stock presets, native APE reference geometry, 0..17 gloss-domain handling, and the known-good 1ad/1x parity paths are installed."
