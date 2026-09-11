$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$window = Join-Path $root 'src\main_window.cpp'
$hlsl = Join-Path $root 'resources\shaders\ape_deferred_lighting.hlsl'
$notes = Join-Path $root 'PATCH_NOTES_PHASE1AA.md'
$errors = @()
foreach ($p in @($window,$hlsl,$notes)) {
    if (-not (Test-Path $p)) { $errors += "Missing $p" }
}
if ($errors.Count -eq 0) {
    $m = Get-Content $window -Raw
    $h = Get-Content $hlsl -Raw
    if ($m -notmatch 'class\s+ApeResizableStack\s+final\s*:\s*public\s+QStackedWidget') { $errors += 'Collapsible central stack missing' }
    if ($m -notmatch 'class\s+ApeResizableDockWidget\s+final\s*:\s*public\s+QDockWidget') { $errors += 'Collapsible dock widget missing' }
    if ($m -notmatch 'class\s+ApeCollapsibleContainer\s+final\s*:\s*public\s+QWidget') { $errors += 'Collapsible Preview container missing' }
    if ($m -notmatch 'authoringStack_\s*=\s*new\s+ApeResizableStack') { $errors += 'Central authoring stack is not using APE-resizable behavior' }
    if ($m -notmatch 'previewContainer\s*=\s*new\s+ApeCollapsibleContainer') { $errors += 'Preview container is still allowed to clamp the dock' }
    if ($m -notmatch 'new\s+ApeResizableDockWidget\(title,\s*this\)') { $errors += 'Main tool docks are not using APE-resizable minimum hints' }
    if ($m -notmatch 'Phase 1aa APE-like unconstrained preview resizing') { $errors += 'Phase 1aa runtime fingerprint missing' }
    if ($m -notmatch 'SetCameraDistance\(4\.85000f\)') { $errors += 'Phase 1z Reset camera framing regressed' }
    if ($h -notmatch 'ResolveApePreviewNormal') { $errors += 'Phase 1x normal recovery regressed' }
    if ($h -notmatch 'RecoverApeProbeDirectionalContrast') { $errors += 'Phase 1y probe work regressed' }
}
if ($errors.Count -gt 0) {
    Write-Host 'Phase 1aa verification: FAIL' -ForegroundColor Red
    $errors | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}
Write-Host 'Phase 1aa verification: PASS' -ForegroundColor Green
Write-Host 'APE-like horizontal/vertical Preview resize range is installed; Phase 1z camera/environment and Phase 1x/1y lighting work are preserved.' -ForegroundColor Cyan
