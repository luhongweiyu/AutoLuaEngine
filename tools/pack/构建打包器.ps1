# 文件用途：构建 Windows 侧 xiaoyv_pack.exe，供 VS Code 和命令行调用。
param(
    [switch] $Release
)

$ErrorActionPreference = 'Stop'
$toolDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDirectory = Join-Path $toolDirectory 'build'
$configuration = if ($Release) { 'Release' } else { 'Debug' }

cmake -S $toolDirectory -B $buildDirectory -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=$configuration
cmake --build $buildDirectory --config $configuration

$binaryPath = Join-Path $buildDirectory 'xiaoyv_pack.exe'
if (-not (Test-Path -LiteralPath $binaryPath)) {
    throw "未找到构建结果：$binaryPath"
}

Write-Output "打包器已构建：$binaryPath"
