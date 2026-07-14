# 文件用途：调用 xiaoyv_pack.exe 生成 .alpkg 脚本包；缺失时打包器会创建 alpkg.json。
param(
    [Parameter(Mandatory = $true)]
    [string] $ProjectDirectory,

    [string] $OutputPath
)

$ErrorActionPreference = 'Stop'
$toolDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$binaryPath = Join-Path $toolDirectory 'build\xiaoyv_pack.exe'

if (-not (Test-Path -LiteralPath $binaryPath)) {
    & (Join-Path $toolDirectory '构建打包器.ps1')
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    & $binaryPath $ProjectDirectory
} else {
    & $binaryPath $ProjectDirectory $OutputPath
}
exit $LASTEXITCODE
