$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$header = Get-Content -Raw (Join-Path $root "src\preview_renderer.h")
$renderer = Get-Content -Raw (Join-Path $root "src\preview_renderer.cpp")
$main = Get-Content -Raw (Join-Path $root "src\main_window.cpp")

$checks = @(
    @{ Text = $header; Pattern = 'void SetApeGlobalProbeAverageColor\(float r, float g, float b\);'; Name = 'public captured-probe-color declaration' },
    @{ Text = $header; Pattern = 'void ResetApeGlobalProbeAverageColorToEnvironment\(\);'; Name = 'public probe-reset declaration' },
    @{ Text = $renderer; Pattern = 'void PreviewRenderer::SetApeGlobalProbeAverageColor\(float r, float g, float b\)'; Name = 'captured-probe-color wrapper' },
    @{ Text = $renderer; Pattern = 'impl_->SetApeGlobalProbeAverageColor\(r, g, b\);'; Name = 'captured-probe-color forwarding' },
    @{ Text = $renderer; Pattern = 'void PreviewRenderer::ResetApeGlobalProbeAverageColorToEnvironment\(\)'; Name = 'probe-reset wrapper' },
    @{ Text = $renderer; Pattern = 'impl_->ResetApeGlobalProbeAverageColorToEnvironment\(\);'; Name = 'probe-reset forwarding' },
    @{ Text = $main; Pattern = 'r\.SetApeGlobalProbeAverageColor\(0\.771301925f, 1\.01348603f, 1\.53983426f\);'; Name = 'Phase 1r call site' }
)

foreach ($check in $checks) {
    if ($check.Text -notmatch $check.Pattern) {
        throw "Phase 1r.1 verification failed: $($check.Name)"
    }
}

Write-Host "Phase 1r.1 verification: PASS" -ForegroundColor Green
Write-Host "PreviewRenderer now publicly exposes the captured APE global-probe color controls used by Phase 1r." -ForegroundColor Cyan
