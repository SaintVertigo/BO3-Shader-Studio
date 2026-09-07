param(
    [Parameter(Mandatory=$true)][string]$OutputDir
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$version = 'v1.0.8'
$files = @(
    @{ Name = 'tinyexr.h'; Url = 'https://raw.githubusercontent.com/syoyo/tinyexr/v1.0.8/tinyexr.h' },
    @{ Name = 'miniz.h';   Url = 'https://raw.githubusercontent.com/syoyo/tinyexr/v1.0.8/deps/miniz/miniz.h' },
    @{ Name = 'miniz.c';   Url = 'https://raw.githubusercontent.com/syoyo/tinyexr/v1.0.8/deps/miniz/miniz.c' }
)

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

foreach ($file in $files) {
    $dest = Join-Path $OutputDir $file.Name
    $needDownload = $true

    if (Test-Path -LiteralPath $dest) {
        $len = (Get-Item -LiteralPath $dest).Length
        if ($len -gt 1024) {
            $needDownload = $false
        } else {
            Remove-Item -LiteralPath $dest -Force -ErrorAction SilentlyContinue
        }
    }

    if ($needDownload) {
        Write-Host ("Fetching EXR support: " + $file.Name)
        Invoke-WebRequest -UseBasicParsing -Uri $file.Url -OutFile $dest
    }

    if (-not (Test-Path -LiteralPath $dest)) {
        throw ("Missing dependency after download: " + $file.Name)
    }
}

$tiny = Join-Path $OutputDir 'tinyexr.h'
$content = Get-Content -LiteralPath $tiny -Raw
if ($content -match 'exr_reader\.hh') {
    throw 'Downloaded TinyEXR header is the newer multi-header API, not pinned v1.0.8.'
}
if ($content -notmatch 'TINYEXR_H_') {
    throw 'tinyexr.h validation failed.'
}

$marker = Join-Path $OutputDir ("tinyexr_" + $version + ".installed")
Set-Content -LiteralPath $marker -Value $version -Encoding Ascii
Write-Host ("EXR support ready: TinyEXR " + $version)
