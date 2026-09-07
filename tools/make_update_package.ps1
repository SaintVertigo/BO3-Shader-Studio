param(
    [Parameter(Mandatory=$true)][string]$PayloadFolder,
    [Parameter(Mandatory=$true)][string]$Version,
    [string]$Notes = '',
    [string]$Output = ''
)
$ErrorActionPreference = 'Stop'
$payload = (Resolve-Path -LiteralPath $PayloadFolder).Path
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path (Get-Location) ("BO3HLSLPreviewer_Update_" + $Version + '.zip')
}
$staging = Join-Path $env:TEMP ("BO3HLSLPreviewer_Package_" + [Guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $staging -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $staging 'payload') -Force | Out-Null
    Get-ChildItem -LiteralPath $payload -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $staging 'payload') -Recurse -Force
    }
    @{
        product = 'BO3 HLSL Previewer'
        format = 1
        version = $Version
        notes = $Notes
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $staging 'update_manifest.json') -Encoding UTF8
    if (Test-Path -LiteralPath $Output) { Remove-Item -LiteralPath $Output -Force }
    Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $Output -CompressionLevel Optimal
    Write-Host "Created $Output"
}
finally {
    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
}
