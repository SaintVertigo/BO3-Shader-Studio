$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

function Assert-FileHash([string]$rel, [string]$expected) {
    $path = Join-Path $root $rel
    if (-not (Test-Path $path)) { throw "Phase 1n missing file: $rel" }
    $actual = (Get-FileHash -Algorithm SHA256 $path).Hash.ToLowerInvariant()
    if ($actual -ne $expected.ToLowerInvariant()) {
        throw "Phase 1n hash mismatch: $rel`nExpected: $expected`nActual:   $actual"
    }
}

function Assert-Contains([string]$rel, [string]$text) {
    $path = Join-Path $root $rel
    $content = Get-Content -Raw -LiteralPath $path
    if (-not $content.Contains($text)) {
        throw "Phase 1n wiring check failed in $rel`: $text"
    }
}

function Assert-NotContains([string]$rel, [string]$text) {
    $path = Join-Path $root $rel
    $content = Get-Content -Raw -LiteralPath $path
    if ($content.Contains($text)) {
        throw "Phase 1n stale Phase 1m wiring remains in $rel`: $text"
    }
}

Assert-FileHash "src\preview_renderer.cpp" "853f18209bfbbe0465ebf2b98fd99944ca8823eb934b9f68399483d50343e9fc"
Assert-FileHash "src\main_window.cpp" "12066495ba2d285f75d2e38fbb5d2d4a6366c5ce4e48c0da577e9d43c95fdc2e"
Assert-FileHash "resources\shaders\ape_deferred_lighting.hlsl" "668ee0a47f3a0011fa69bdedb0e6160e06c7ff0b34cab02125b0686e8448cc02"
Assert-FileHash "docs\APE_REVERSE_ENGINEERING_FINDINGS.md" "f40875584f7b1f80d5b558e1ec7af1f0491d06b9808c7bfc40641722a1541594"

Assert-Contains "src\preview_renderer.cpp" "apeProbeRotationDegrees_ = environmentRotationDegrees_;"
Assert-Contains "src\preview_renderer.cpp" "environmentRotationDegrees_ - yawDeltaDegrees"
Assert-Contains "src\preview_renderer.cpp" "apeProbeRotationDegrees_ * (3.14159265358979323846f / 180.0f)"
Assert-NotContains "src\preview_renderer.cpp" "environmentPitchDegrees_"
Assert-NotContains "src\preview_renderer.cpp" "float pitch = previewApeSettings.w;"

Assert-Contains "resources\shaders\ape_deferred_lighting.hlsl" "float3 RotateVisibleSkyDirection(float3 direction)"
Assert-Contains "resources\shaders\ape_deferred_lighting.hlsl" "float3 RotateBakedProbeDirection(float3 direction)"
Assert-Contains "resources\shaders\ape_deferred_lighting.hlsl" "VisibleSkyDirectionToEquirect(ray)"
Assert-Contains "resources\shaders\ape_deferred_lighting.hlsl" "BakedProbeDirectionToEquirect(direction)"
Assert-Contains "resources\shaders\ape_deferred_lighting.hlsl" "float3 d = RotateBakedProbeDirection(direction);"
Assert-NotContains "resources\shaders\ape_deferred_lighting.hlsl" "float pitch = previewApeSettings.w;"

Assert-Contains "src\main_window.cpp" 'makeSliderRow("Light pitch", -180, 180, lightPitchSlider_, lightPitchValue_);'
Assert-Contains "src\main_window.cpp" "Horizontal light movement also yaws the visible sky; vertical movement does not pitch it, and the baked material probe stays fixed."
Assert-Contains "docs\APE_REVERSE_ENGINEERING_FINDINGS.md" "split APE sun, visible sky, and baked probe orientation"

Write-Host "Phase 1n verification: PASS" -ForegroundColor Green
Write-Host "Sun pitch is continuous; only light yaw moves the visible sky; material reflection/diffuse probe orientation stays baked to the APE preset." -ForegroundColor Cyan
