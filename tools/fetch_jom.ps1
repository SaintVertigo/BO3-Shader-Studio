param(
    [Parameter(Mandatory=$true)][string]$OutputDir
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$version = '1.1.7'
$url = 'https://download.qt.io/official_releases/jom/jom_1_1_7.zip'
$expectedSha256 = '4c8af345586a9a08fbfd2f613fcac748226d91a75627aa3581b297dd513046fe'

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$jomExe = Join-Path $OutputDir 'jom.exe'
if (Test-Path -LiteralPath $jomExe) {
    Write-Host "Parallel make ready: jom $version"
    exit 0
}

$zipPath = Join-Path $OutputDir 'jom.zip'
Write-Host "Fetching parallel make helper: jom $version"
Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $zipPath

$actualSha256 = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualSha256 -ne $expectedSha256) {
    throw "jom SHA-256 mismatch. Expected $expectedSha256, got $actualSha256."
}

$extractDir = Join-Path $OutputDir 'extract'
if (Test-Path -LiteralPath $extractDir) {
    Remove-Item -LiteralPath $extractDir -Recurse -Force
}
Expand-Archive -LiteralPath $zipPath -DestinationPath $extractDir -Force

$found = Get-ChildItem -LiteralPath $extractDir -Filter 'jom.exe' -File -Recurse | Select-Object -First 1
if (-not $found) {
    throw 'jom.exe was not present in the verified Qt archive.'
}
Copy-Item -LiteralPath $found.FullName -Destination $jomExe -Force
Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $extractDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "Parallel make ready: jom $version"
