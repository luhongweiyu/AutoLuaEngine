[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$sourceDirectory = Join-Path $repositoryRoot 'engines/android/third_party/ncnn'
$sourceTag = '20260526'
$sourceUrl = 'https://github.com/Tencent/ncnn.git'

if (Test-Path -LiteralPath (Join-Path $sourceDirectory 'CMakeLists.txt')) {
    Write-Host "Reusing NCNN source: $sourceDirectory"
    exit 0
}
if (Test-Path -LiteralPath $sourceDirectory) {
    throw "NCNN source directory is incomplete: $sourceDirectory. Remove it manually, then retry."
}

$gitExecutable = (Get-Command git.exe -ErrorAction SilentlyContinue).Source
if ([string]::IsNullOrWhiteSpace($gitExecutable)) {
    throw 'Git was not found. Install Git, then rerun this script.'
}

New-Item -ItemType Directory -Path (Split-Path -Parent $sourceDirectory) -Force | Out-Null
& $gitExecutable clone --depth 1 --branch $sourceTag $sourceUrl $sourceDirectory
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $sourceDirectory 'CMakeLists.txt'))) {
    throw "Failed to prepare NCNN $sourceTag from $sourceUrl"
}
Write-Host "Prepared NCNN source: $sourceDirectory ($sourceTag)"
