param(
    [Parameter(Mandatory=$true)][string]$Version,
    [Parameter(Mandatory=$true)][string]$DisplayVersion,
    [Parameter(Mandatory=$true)][ValidateSet('stable','tester')][string]$Channel,
    [Parameter(Mandatory=$true)][string]$Repository,
    [string]$DistDir = 'dist',
    [string]$OutputDir = 'release_artifacts',
    [string]$NotesFile = '',
    [switch]$UpdateOnly,
    [switch]$LeanPayload
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$dist = (Resolve-Path (Join-Path $root $DistDir)).Path
$out = Join-Path $root $OutputDir

# Always start clean so old bridge/legacy assets can never leak into a new
# release when this script is also used locally.
if (Test-Path -LiteralPath $out) {
    Remove-Item -LiteralPath $out -Recurse -Force
}
New-Item -ItemType Directory -Path $out -Force | Out-Null

$notes = ''
if ($NotesFile) {
    $notesPath = if ([IO.Path]::IsPathRooted($NotesFile)) { $NotesFile } else { Join-Path $root $NotesFile }
    if (Test-Path -LiteralPath $notesPath) { $notes = Get-Content -LiteralPath $notesPath -Raw }
}
if (-not $notes.Trim()) {
    $notes = "BO3 Shader Studio $DisplayVersion"
}

# Stamp runtime metadata. `version` is intentionally an internal monotonic
# SemVer used only for update ordering; `displayVersion` is what users see.
$versionPath = Join-Path $dist 'version.json'
$versionJson = if (Test-Path -LiteralPath $versionPath) {
    Get-Content -LiteralPath $versionPath -Raw | ConvertFrom-Json
} else {
    [pscustomobject]@{ product='BO3 HLSL Previewer'; updateFormat=1 }
}

# Keep the format-1 manifest identity for compatibility with the already-shipped
# 0.1 updater. It is internal only; all visible branding is BO3 Shader Studio.
$versionJson.product = 'BO3 HLSL Previewer'
$versionJson.version = $Version
if (-not ($versionJson.PSObject.Properties.Name -contains 'displayProduct')) { $versionJson | Add-Member -NotePropertyName displayProduct -NotePropertyValue 'BO3 Shader Studio' }
else { $versionJson.displayProduct = 'BO3 Shader Studio' }
if (-not ($versionJson.PSObject.Properties.Name -contains 'displayVersion')) { $versionJson | Add-Member -NotePropertyName displayVersion -NotePropertyValue $DisplayVersion }
else { $versionJson.displayVersion = $DisplayVersion }
$versionJson.updateFormat = 1
if (-not ($versionJson.PSObject.Properties.Name -contains 'githubRepository')) { $versionJson | Add-Member -NotePropertyName githubRepository -NotePropertyValue $Repository }
else { $versionJson.githubRepository = $Repository }
if (-not ($versionJson.PSObject.Properties.Name -contains 'updateAssetPrefix')) { $versionJson | Add-Member -NotePropertyName updateAssetPrefix -NotePropertyValue 'BO3_Shader_Studio_Update' }
else { $versionJson.updateAssetPrefix = 'BO3_Shader_Studio_Update' }
if (-not ($versionJson.PSObject.Properties.Name -contains 'defaultUpdateChannel')) { $versionJson | Add-Member -NotePropertyName defaultUpdateChannel -NotePropertyValue $Channel }
else { $versionJson.defaultUpdateChannel = $Channel }
$versionJson | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $versionPath -Encoding UTF8

$updateName = 'BO3_Shader_Studio_Update.zip'
$fullName = 'BO3_Shader_Studio.zip'
$updatePath = Join-Path $out $updateName
$fullPath = Join-Path $out $fullName
$staging = Join-Path $env:TEMP ("BO3ShaderStudio_Package_" + [Guid]::NewGuid().ToString('N'))

try {
    New-Item -ItemType Directory -Path $staging -Force | Out-Null
    $payload = Join-Path $staging 'payload'
    New-Item -ItemType Directory -Path $payload -Force | Out-Null

    if ($LeanPayload) {
        # The updater copies only files present in payload and leaves every other
        # installed file alone. Automatic tester builds therefore do not need to
        # resend Qt's ~50 MB runtime for ordinary C++/shader/data fixes.
        #
        # Keep all BO3 Shader Studio-owned runtime content in the lean package so
        # source, sample, preset, template and compatibility edits still update.
        $leanItems = @(
            'BO3HLSLPreviewer.exe',
            'version.json',
            'bo3_compat',
            'shaders',
            'presets',
            'ui',
            'export_templates',
            'tests'
        )
        foreach ($item in $leanItems) {
            $source = Join-Path $dist $item
            if (Test-Path -LiteralPath $source) {
                Copy-Item -LiteralPath $source -Destination $payload -Recurse -Force
            }
        }
        if (-not (Test-Path -LiteralPath (Join-Path $payload 'BO3HLSLPreviewer.exe'))) {
            throw 'Lean tester payload is missing BO3HLSLPreviewer.exe.'
        }
    }
    else {
        Copy-Item -Path (Join-Path $dist '*') -Destination $payload -Recurse -Force
    }

    $manifest = [ordered]@{
        product = 'BO3 HLSL Previewer'
        displayProduct = 'BO3 Shader Studio'
        format = 1
        version = $Version
        displayVersion = $DisplayVersion
        channel = $Channel
        repository = $Repository
        notes = $notes.Trim()
        createdUtc = [DateTime]::UtcNow.ToString('o')
        payloadKind = if ($LeanPayload) { 'lean-tester' } else { 'full-runtime' }
    }
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $staging 'update_manifest.json') -Encoding UTF8

    $updateCompression = if ($LeanPayload) { 'Fastest' } else { 'Optimal' }
    Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $updatePath -CompressionLevel $updateCompression

    if (-not $UpdateOnly) {
        Compress-Archive -Path (Join-Path $dist '*') -DestinationPath $fullPath -CompressionLevel Optimal
    }
}
finally {
    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
}

$hashTargets = @($updatePath)
if (-not $UpdateOnly) { $hashTargets += $fullPath }
foreach ($path in $hashTargets) {
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -LiteralPath ($path + '.sha256') -Value ("$hash  " + [IO.Path]::GetFileName($path)) -Encoding ASCII
}

Set-Content -LiteralPath (Join-Path $out 'release_notes.md') -Value $notes -Encoding UTF8
Write-Host "Created: $updatePath"
if ($UpdateOnly) {
    Write-Host 'Automatic tester mode: skipped duplicate full-distribution ZIP.'
}
else {
    Write-Host "Created: $fullPath"
}
if ($LeanPayload) {
    Write-Host 'Automatic tester mode: update ZIP contains only BO3 Shader Studio-owned runtime files; Qt runtime DLLs are not resent.'
}
