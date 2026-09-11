$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$main = Get-Content -Raw (Join-Path $root "src\main_window.cpp")
$renderer = Get-Content -Raw (Join-Path $root "src\preview_renderer.cpp")

$checks = @(
    @{ Text = $main; Pattern = 'std::fmod\(p\.ssiYaw, 360\.0f\)'; Name = 'captured SSI yaw used directly' },
    @{ Text = $renderer; Pattern = '90\.0f - lightYawDegrees_'; Name = 'captured visible-sky yaw relationship' },
    @{ Text = $renderer; Pattern = '(8\.0f / 65535\.0f|DXGI_FORMAT_R16_TYPELESS)'; Name = 'shadow receiver/depth recovery' },
    @{ Text = $main; Pattern = 'Phase 1(q\.1 captured sun-axis fix|r captured light energy \+ depth shadow)'; Name = 'runtime Phase 1q.1-or-newer fingerprint' }
)

foreach ($check in $checks) {
    if ($check.Text -notmatch $check.Pattern) {
        throw "Phase 1q.1 verification failed: $($check.Name)"
    }
}

if ($main -match 'p\.ssiYaw \+ 90\.0f') {
    throw "Phase 1q.1 verification failed: obsolete +90 degree sun-yaw remap is still present"
}
if ($renderer -match '180\.0f - lightYawDegrees_') {
    throw "Phase 1q.1 verification failed: obsolete visible-sky yaw formula is still present"
}

Write-Host "Phase 1q.1 verification: PASS" -ForegroundColor Green
Write-Host "Captured sun yaw, 90-sunYaw sky motion, and direct-light self-shadow bias recovery are installed." -ForegroundColor Cyan
