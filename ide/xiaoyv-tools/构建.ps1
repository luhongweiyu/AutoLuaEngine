param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$QtRoot = "",
    [string]$VisualStudioRoot = "",
    [switch]$WithTests
)

# 文件用途：配置 Visual Studio C++ 环境并构建、部署小鱼抓图取色器；仅在指定 WithTests 时运行测试。
$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildRoot = Join-Path $projectRoot "build"
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
$workspaceDrive = [System.IO.Path]::GetPathRoot($projectRoot)

if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    $QtRoot = Join-Path $workspaceDrive "soft\Qt\6.8.3\msvc2022_64"
}
if (-not (Test-Path -LiteralPath (Join-Path $QtRoot "lib\cmake\Qt6\Qt6Config.cmake"))) {
    throw "Qt 6 SDK not found: $QtRoot"
}

if ([string]::IsNullOrWhiteSpace($VisualStudioRoot)) {
    $portableVisualStudio = Join-Path $workspaceDrive "soft\MicrosoftVisualStudio\2022\Community"
    if (Test-Path -LiteralPath (Join-Path $portableVisualStudio "VC\Auxiliary\Build\vcvars64.bat")) {
        $VisualStudioRoot = $portableVisualStudio
    } elseif (Test-Path -LiteralPath $vswhere) {
        $VisualStudioRoot = & $vswhere -latest -products * -version "[17.0,18.0)" `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
    }
}
if ([string]::IsNullOrWhiteSpace($VisualStudioRoot) -or
        -not (Test-Path -LiteralPath (Join-Path $VisualStudioRoot "VC\Auxiliary\Build\vcvars64.bat"))) {
    throw "Visual Studio 2022 with the C++ toolchain was not found"
}

$developerShell = Join-Path $VisualStudioRoot "VC\Auxiliary\Build\vcvars64.bat"
$cmakeBin = Join-Path $VisualStudioRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$cmakeExecutable = Join-Path $cmakeBin "cmake.exe"
$ctestExecutable = Join-Path $cmakeBin "ctest.exe"
if (-not (Test-Path -LiteralPath $cmakeExecutable) -or
        -not (Test-Path -LiteralPath $ctestExecutable)) {
    throw "The Visual Studio 2022 CMake tools are not installed"
}
$buildTests = if ($WithTests) { "ON" } else { "OFF" }
$refreshCache = $false
$cachePath = Join-Path $buildRoot "CMakeCache.txt"
if (Test-Path -LiteralPath $cachePath) {
    $cacheLines = Get-Content -LiteralPath $cachePath
    $cachedHome = ($cacheLines |
        Where-Object { $_ -like "CMAKE_HOME_DIRECTORY:INTERNAL=*" } |
        Select-Object -First 1) -replace "^[^=]*=", ""
    $cachedCompiler = ($cacheLines |
        Where-Object { $_ -like "CMAKE_CXX_COMPILER:FILEPATH=*" } |
        Select-Object -First 1) -replace "^[^=]*=", ""
    $normalizedProjectRoot = $projectRoot.Replace("\", "/").TrimEnd("/")
    $normalizedCachedHome = if ([string]::IsNullOrWhiteSpace($cachedHome)) {
        ""
    } else {
        $cachedHome.Replace("\", "/").TrimEnd("/")
    }
    $compilerMissing = [string]::IsNullOrWhiteSpace($cachedCompiler) -or
        -not (Test-Path -LiteralPath $cachedCompiler)
    $refreshCache = $normalizedCachedHome -ne $normalizedProjectRoot -or
        $compilerMissing
}
$freshArgument = if ($refreshCache) { "--fresh " } else { "" }
$configure = "`"$cmakeExecutable`" $freshArgument-S `"$projectRoot`" -B `"$buildRoot`" -G Ninja " +
    "-DCMAKE_BUILD_TYPE=$Configuration -DCMAKE_PREFIX_PATH=`"$QtRoot`" " +
    "-DXIAOYV_BUILD_TESTS=$buildTests"
$build = "`"$cmakeExecutable`" --build `"$buildRoot`" --config $Configuration"
$test = "set `"PATH=$QtRoot\bin;%PATH%`" && " +
    "`"$ctestExecutable`" --test-dir `"$buildRoot`" -C $Configuration --output-on-failure"
$deploy = "`"$QtRoot\bin\windeployqt.exe`" --no-translations " +
    "`"$buildRoot\xiaoyv_tools.exe`""

$pipeline = "$configure && $build"
if ($WithTests) {
    $pipeline += " && $test"
}
$pipeline += " && $deploy"

& cmd.exe /d /s /c "`"set VSCMD_SKIP_SENDTELEMETRY=1 && call `"$developerShell`" && $pipeline`""
if ($LASTEXITCODE -ne 0) {
    throw "Xiaoyv Tools build failed with exit code $LASTEXITCODE"
}

Write-Host "Build complete: $buildRoot\xiaoyv_tools.exe"
