param(
    [Parameter(Mandatory=$true)][string]$Version,
    [Parameter(Mandatory=$true)][ValidateSet('stable','tester')][string]$Channel,
    [Parameter(Mandatory=$true)][string]$Repository,
    [string]$DistDir = 'dist',
    [string]$OutputDir = 'release_artifacts',
    [string]$NotesFile = ''
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$dist = (Resolve-Path (Join-Path $root $DistDir)).Path
$out = Join-Path $root $OutputDir
New-Item -ItemType Directory -Path $out -Force | Out-Null

$notes = ''
if ($NotesFile) {
    $notesPath = if ([IO.Path]::IsPathRooted($NotesFile)) { $NotesFile } else { Join-Path $root $NotesFile }
    if (Test-Path -LiteralPath $notesPath) { $notes = Get-Content -LiteralPath $notesPath -Raw }
}
if (-not $notes.Trim()) {
    $notes = "BO3 HLSL Previewer $Version ($Channel channel)"
}

# Stamp the runtime metadata that the application reads after installation.
$versionPath = Join-Path $dist 'version.json'
$versionJson = if (Test-Path -LiteralPath $versionPath) {
    Get-Content -LiteralPath $versionPath -Raw | ConvertFrom-Json
} else {
    [pscustomobject]@{ product='BO3 HLSL Previewer'; updateFormat=1 }
}
$versionJson.product = 'BO3 HLSL Previewer'
$versionJson.version = $Version
$versionJson.updateFormat = 1
if (-not ($versionJson.PSObject.Properties.Name -contains 'githubRepository')) { $versionJson | Add-Member -NotePropertyName githubRepository -NotePropertyValue $Repository }
else { $versionJson.githubRepository = $Repository }
if (-not ($versionJson.PSObject.Properties.Name -contains 'updateAssetPrefix')) { $versionJson | Add-Member -NotePropertyName updateAssetPrefix -NotePropertyValue 'BO3_HLSL_Previewer_Update_' }
else { $versionJson.updateAssetPrefix = 'BO3_HLSL_Previewer_Update_' }
if (-not ($versionJson.PSObject.Properties.Name -contains 'defaultUpdateChannel')) { $versionJson | Add-Member -NotePropertyName defaultUpdateChannel -NotePropertyValue $Channel }
else { $versionJson.defaultUpdateChannel = $Channel }
$versionJson | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $versionPath -Encoding UTF8

$safeVersion = $Version -replace '[^0-9A-Za-z._-]', '_'
$updateName = "BO3_HLSL_Previewer_Update_$safeVersion.zip"
$fullName = "BO3_HLSL_Previewer_$safeVersion.zip"
$updatePath = Join-Path $out $updateName
$fullPath = Join-Path $out $fullName
$staging = Join-Path $env:TEMP ("BO3HLSLPreviewer_Package_" + [Guid]::NewGuid().ToString('N'))

try {
    New-Item -ItemType Directory -Path $staging -Force | Out-Null
    $payload = Join-Path $staging 'payload'
    New-Item -ItemType Directory -Path $payload -Force | Out-Null
    Copy-Item -Path (Join-Path $dist '*') -Destination $payload -Recurse -Force

    $manifest = [ordered]@{
        product = 'BO3 HLSL Previewer'
        format = 1
        version = $Version
        channel = $Channel
        repository = $Repository
        notes = $notes.Trim()
        createdUtc = [DateTime]::UtcNow.ToString('o')
    }
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $staging 'update_manifest.json') -Encoding UTF8

    Remove-Item -LiteralPath $updatePath -Force -ErrorAction SilentlyContinue
    Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $updatePath -CompressionLevel Optimal

    Remove-Item -LiteralPath $fullPath -Force -ErrorAction SilentlyContinue
    Compress-Archive -Path (Join-Path $dist '*') -DestinationPath $fullPath -CompressionLevel Optimal
}
finally {
    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
}

foreach ($path in @($updatePath, $fullPath)) {
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -LiteralPath ($path + '.sha256') -Value ("$hash  " + [IO.Path]::GetFileName($path)) -Encoding ASCII
}

Set-Content -LiteralPath (Join-Path $out 'release_notes.md') -Value $notes -Encoding UTF8
Write-Host "Created: $updatePath"
Write-Host "Created: $fullPath"
