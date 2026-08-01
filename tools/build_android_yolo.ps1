[CmdletBinding()]
param(
    [ValidateSet('arm64-v8a', 'armeabi-v7a', 'x86', 'x86_64')]
    [string[]]$Abi = @('arm64-v8a', 'armeabi-v7a', 'x86', 'x86_64'),
    # Keep CMake/Ninja work directories local so Windows does not start cmd.exe in a UNC path.
    [string]$BuildCacheRoot = '',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$androidRoot = Join-Path $repositoryRoot 'engines/android'
$sdkPropertiesPath = Join-Path $androidRoot 'local.properties'
$yoloProjectDirectory = Join-Path $androidRoot 'yolo'
$outputRoot = Join-Path $androidRoot 'optional-libs/yolo'
$ncnnSourceDirectory = Join-Path $androidRoot 'third_party/ncnn'
if ([string]::IsNullOrWhiteSpace($BuildCacheRoot)) {
    $BuildCacheRoot = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'xiaoyv-build-cache/yolo-cmake-3.22.1-ndk25'
}

if (-not (Test-Path -LiteralPath (Join-Path $ncnnSourceDirectory 'CMakeLists.txt'))) {
    & (Join-Path $repositoryRoot 'tools/prepare_android_yolo_source.ps1')
    if ($LASTEXITCODE -ne 0) {
        throw 'Failed to prepare the fixed NCNN source required for YOLO.'
    }
}

if (-not (Test-Path -LiteralPath $sdkPropertiesPath)) {
    throw "Android SDK configuration is missing: $sdkPropertiesPath"
}

$sdkLine = Get-Content -LiteralPath $sdkPropertiesPath -Encoding utf8 |
    Where-Object { $_ -match '^sdk\.dir=' } |
    Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($sdkLine)) {
    throw "local.properties does not define sdk.dir: $sdkPropertiesPath"
}
$sdkDirectory = $sdkLine -replace '^sdk\.dir=', ''
$sdkDirectory = ($sdkDirectory -replace '\\:', ':') -replace '\\\\', '\\'
if (-not (Test-Path -LiteralPath $sdkDirectory)) {
    throw "Android SDK directory does not exist: $sdkDirectory"
}

$ndkDirectory = if ($env:ANDROID_NDK_ROOT -and (Test-Path -LiteralPath $env:ANDROID_NDK_ROOT)) {
    $env:ANDROID_NDK_ROOT
} elseif ($env:ANDROID_NDK_HOME -and (Test-Path -LiteralPath $env:ANDROID_NDK_HOME)) {
    $env:ANDROID_NDK_HOME
} else {
    $numericNdkDirectories = Get-ChildItem -LiteralPath (Join-Path $sdkDirectory 'ndk') -Directory |
        Where-Object { $_.Name -match '^\d+(\.\d+)+$' } |
        Sort-Object { [version]$_.Name } -Descending
    $selectedNdk = $numericNdkDirectories | Select-Object -First 1 -ExpandProperty FullName
    if ([string]::IsNullOrWhiteSpace($selectedNdk)) {
        Get-ChildItem -LiteralPath (Join-Path $sdkDirectory 'ndk') -Directory |
            Sort-Object Name -Descending |
            Select-Object -First 1 -ExpandProperty FullName
    } else {
        $selectedNdk
    }
}
if ([string]::IsNullOrWhiteSpace($ndkDirectory)) {
    throw "Android NDK was not found. Install it under $sdkDirectory\\ndk or set ANDROID_NDK_ROOT."
}
$toolchainFile = Join-Path $ndkDirectory 'build/cmake/android.toolchain.cmake'
if (-not (Test-Path -LiteralPath $toolchainFile)) {
    throw "Android NDK toolchain file does not exist: $toolchainFile"
}
$llvmStripExecutable = Join-Path $ndkDirectory 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe'
if (-not (Test-Path -LiteralPath $llvmStripExecutable)) {
    throw "Android NDK llvm-strip was not found: $llvmStripExecutable"
}

$preferredCmake = Join-Path $sdkDirectory 'cmake/3.22.1/bin/cmake.exe'
$cmakeExecutable = if (Test-Path -LiteralPath $preferredCmake) { $preferredCmake } else { $null }
if ([string]::IsNullOrWhiteSpace($cmakeExecutable)) {
    $cmakeCandidates = Get-ChildItem -LiteralPath (Join-Path $sdkDirectory 'cmake') -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName 'bin/cmake.exe' }
    $cmakeExecutable = $cmakeCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($cmakeExecutable)) {
    $cmakeExecutable = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
}
if ([string]::IsNullOrWhiteSpace($cmakeExecutable)) {
    throw "Android SDK CMake was not found. Install CMake 3.22.1 or add cmake.exe to PATH."
}
$ninjaExecutable = Join-Path (Split-Path -Parent $cmakeExecutable) 'ninja.exe'
if (-not (Test-Path -LiteralPath $ninjaExecutable)) {
    $ninjaExecutable = (Get-Command ninja.exe -ErrorAction SilentlyContinue).Source
}
if ([string]::IsNullOrWhiteSpace($ninjaExecutable)) {
    throw "Ninja was not found next to CMake or on PATH."
}

# Only native implementation and CMake inputs invalidate a reusable SO. Documentation such as ncnn/UPSTREAM.md
# must not make every APK build re-enter CMake or look like a native rebuild.
$sourceDirectories = @(
    $yoloProjectDirectory,
    (Join-Path $androidRoot 'app/src/main/cpp/yolo'),
    $ncnnSourceDirectory
)
$sourceExtensions = @('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.inl', '.s', '.asm', '.cmake')
$sourceFiles = $sourceDirectories |
    ForEach-Object { Get-ChildItem -LiteralPath $_ -File -Recurse } |
    Where-Object {
        $_.Name -eq 'CMakeLists.txt' -or $sourceExtensions -contains $_.Extension.ToLowerInvariant()
    } |
    Select-Object -ExpandProperty FullName
$latestSourceTime = (Get-Item -LiteralPath $sourceFiles |
    Measure-Object LastWriteTime -Maximum).Maximum

foreach ($currentAbi in $Abi) {
    $outputDirectory = Join-Path $outputRoot $currentAbi
    $libraryPath = Join-Path $outputDirectory 'libxiaoyv_yolo.so'
    if (-not $Force -and (Test-Path -LiteralPath $libraryPath) -and
            (Get-Item -LiteralPath $libraryPath).LastWriteTime -ge $latestSourceTime) {
        Write-Host "Reusing existing YOLO SO: $libraryPath"
        continue
    }

    $buildDirectory = Join-Path $BuildCacheRoot $currentAbi
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null
    & $cmakeExecutable -S $yoloProjectDirectory -B $buildDirectory -G Ninja `
        "-DCMAKE_MAKE_PROGRAM=$ninjaExecutable" `
        "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile" `
        "-DANDROID_ABI=$currentAbi" `
        '-DANDROID_PLATFORM=android-23' `
        '-DANDROID_STL=c++_static' `
        '-DCMAKE_BUILD_TYPE=Release' `
        "-DXIAOYV_YOLO_OUTPUT_DIR=$outputDirectory"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to configure YOLO SO: $currentAbi"
    }
    & $cmakeExecutable --build $buildDirectory --target xiaoyv_yolo --config Release
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $libraryPath)) {
        throw "Failed to build YOLO SO: $currentAbi"
    }
    # Android CMake's Release preset still retains DWARF in this environment. Keep the reusable
    # delivery SO lean while preserving the dynamic symbols needed by JNI and the system linker.
    & $llvmStripExecutable --strip-unneeded $libraryPath
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to strip debug symbols from YOLO SO: $currentAbi"
    }
    Write-Host "Built YOLO SO: $libraryPath"
}
