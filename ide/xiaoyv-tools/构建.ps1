param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$QtRoot = "D:\soft\Qt\6.8.3\msvc2022_64"
)

# 文件用途：配置 Visual Studio C++ 环境，构建、测试并部署可独立运行的小鱼抓图取色器。
$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildRoot = Join-Path $projectRoot "build"
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "没有找到 Visual Studio Installer 的 vswhere.exe"
}
if (-not (Test-Path -LiteralPath (Join-Path $QtRoot "lib\cmake\Qt6\Qt6Config.cmake"))) {
    throw "没有找到 Qt 6 SDK：$QtRoot"
}

$visualStudio = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if ([string]::IsNullOrWhiteSpace($visualStudio)) {
    throw "没有找到带 C++ 工具链的 Visual Studio 2022"
}

$developerShell = Join-Path $visualStudio "Common7\Tools\VsDevCmd.bat"
$configure = "cmake -S `"$projectRoot`" -B `"$buildRoot`" -G Ninja " +
    "-DCMAKE_BUILD_TYPE=$Configuration -DCMAKE_PREFIX_PATH=`"$QtRoot`""
$build = "cmake --build `"$buildRoot`" --config $Configuration"
$test = "set `"PATH=$QtRoot\bin;%PATH%`" && " +
    "ctest --test-dir `"$buildRoot`" -C $Configuration --output-on-failure"
$deploy = "`"$QtRoot\bin\windeployqt.exe`" --no-translations " +
    "`"$buildRoot\xiaoyv_tools.exe`""

& cmd.exe /d /s /c "`"`"$developerShell`" -arch=x64 -host_arch=x64 && $configure && $build && $test && $deploy`""
if ($LASTEXITCODE -ne 0) {
    throw "小鱼抓图取色器构建失败，退出码：$LASTEXITCODE"
}

Write-Host "构建完成：$buildRoot\xiaoyv_tools.exe"
