[CmdletBinding()]
param(
    [ValidateSet('arm64-v8a', 'armeabi-v7a', 'x86', 'x86_64')]
    [string[]]$Abi = @('arm64-v8a', 'x86_64'),
    [string]$GradleCacheRoot = (Join-Path $env:USERPROFILE '.gradle/caches/modules-2/files-2.1'),
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$optionalLibrariesRoot = Join-Path $repositoryRoot 'engines/android/optional-libs'

function Find-Aar {
    param(
        [Parameter(Mandatory = $true)][string]$GroupPath,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$FilePrefix
    )

    $directory = Join-Path $GradleCacheRoot "$GroupPath/$Version"
    $archive = Get-ChildItem -LiteralPath $directory -Filter "$FilePrefix-$Version.aar" -File -Recurse -ErrorAction SilentlyContinue |
        Sort-Object FullName |
        Select-Object -First 1 -ExpandProperty FullName
    if ([string]::IsNullOrWhiteSpace($archive)) {
        throw "Android dependency archive was not found: $FilePrefix $Version under $directory. Run the Android Gradle dependency task first."
    }
    return $archive
}

function Export-AarEntry {
    param(
        [Parameter(Mandatory = $true)][string]$ArchivePath,
        [Parameter(Mandatory = $true)][string]$EntryPath,
        [Parameter(Mandatory = $true)][string]$DestinationPath
    )

    $archive = [System.IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
        $entry = $archive.GetEntry($EntryPath)
        if ($null -eq $entry) {
            throw "Entry $EntryPath was not found in $ArchivePath"
        }
        if (Test-Path -LiteralPath $DestinationPath) {
            if ((Get-Item -LiteralPath $DestinationPath).Length -eq $entry.Length -and -not $Force) {
                Write-Host "Reusing optional runtime: $DestinationPath"
                return
            }
            if (-not $Force) {
                throw "Optional runtime cache differs from the dependency archive: $DestinationPath. Pass -Force to replace it."
            }
        }

        $destinationDirectory = Split-Path -Parent $DestinationPath
        New-Item -ItemType Directory -Force -Path $destinationDirectory | Out-Null
        $temporaryPath = "$DestinationPath.extracting"
        if (Test-Path -LiteralPath $temporaryPath) {
            Remove-Item -LiteralPath $temporaryPath -Force
        }
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $temporaryPath, $true)
        Move-Item -LiteralPath $temporaryPath -Destination $DestinationPath -Force
        Write-Host "Prepared optional runtime: $DestinationPath"
    } finally {
        $archive.Dispose()
    }
}

Add-Type -AssemblyName System.IO.Compression.FileSystem

$openCvAar = Find-Aar -GroupPath 'org.opencv/opencv' -Version '4.9.0' -FilePrefix 'opencv'
$onnxRuntimeAar = Find-Aar -GroupPath 'com.microsoft.onnxruntime/onnxruntime-android' -Version '1.18.0' -FilePrefix 'onnxruntime-android'

foreach ($currentAbi in $Abi) {
    $openCvOutputDirectory = Join-Path $optionalLibrariesRoot "opencv/$currentAbi"
    Export-AarEntry -ArchivePath $openCvAar -EntryPath "jni/$currentAbi/libc++_shared.so" `
        -DestinationPath (Join-Path $openCvOutputDirectory 'libc++_shared.so')
    Export-AarEntry -ArchivePath $openCvAar -EntryPath "jni/$currentAbi/libopencv_java4.so" `
        -DestinationPath (Join-Path $openCvOutputDirectory 'libopencv_java4.so')

    $ocrOutputDirectory = Join-Path $optionalLibrariesRoot "rapidocr/$currentAbi"
    Export-AarEntry -ArchivePath $onnxRuntimeAar -EntryPath "jni/$currentAbi/libonnxruntime.so" `
        -DestinationPath (Join-Path $ocrOutputDirectory 'libonnxruntime.so')
}
