$ErrorActionPreference = 'Stop'

if (-not (Test-Path '.\BO3HLSLPreviewer.pro')) {
    throw 'Run this from the real BO3_HLSL_Previewer repo root (the folder that contains BO3HLSLPreviewer.pro).'
}

$expected = @{
    'src\main_window.cpp' = 'f79678e29dd8c364561ba3d6f779643114b11abc93dfd3306d57bbabca632071'
    'src\preview_renderer.cpp' = '4ff217ab3eeba5c8fb4761a682888356e952aa5306b8bcdd0d07c1497d8bd61b'
    'resources\shaders\ape_deferred_lighting.hlsl' = '158be92be1fd993f530c99fc2f9a2eca769f7949a13271d7f28299229cbf0e48'
    'resources\learning.qrc' = 'd64c5d199360d2ae343621a2590cd030cf2b03058e4684019bf421bf5d4d1b58'
}

foreach ($path in $expected.Keys) {
    if (-not (Test-Path $path)) { throw "Missing Phase 1h file: $path" }
    $actual = (Get-FileHash -Algorithm SHA256 $path).Hash.ToLowerInvariant()
    if ($actual -ne $expected[$path]) {
        throw "Phase 1h hash mismatch: $path`nExpected: $($expected[$path])`nActual:   $actual"
    }
}

$qrc = Get-Content '.\resources\learning.qrc' -Raw
if ($qrc -notmatch 'ape_deferred_lighting\.hlsl') {
    throw 'learning.qrc is not registering ape_deferred_lighting.hlsl.'
}

$main = Get-Content '.\src\main_window.cpp' -Raw
$shader = Get-Content '.\resources\shaders\ape_deferred_lighting.hlsl' -Raw
if ($main -notmatch 'setResetRequestedCallback\(\[this\]\{ resetPreviewView\(\); \}\)') {
    throw 'Full APE Reset callback wiring is missing.'
}
if ($main -notmatch 'cameraDollying_') {
    throw 'APE dolly navigation wiring is missing.'
}
if ($shader -notmatch 'float combined = exp2\(-17\.0 \* encoded\)') {
    throw 'BO3 logarithmic gloss decode fix is missing.'
}

Write-Host ''
Write-Host 'Phase 1h verification: PASS' -ForegroundColor Green
Write-Host 'APE Match now keeps the SSI light fixed while navigating, restores the full APE preset on Reset, and uses the BO3 logarithmic gloss packing contract.'
Write-Host ''
