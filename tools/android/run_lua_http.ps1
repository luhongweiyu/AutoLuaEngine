param(
    [string]$AdbPath = "D:\soft\Android\Sdk\platform-tools\adb.exe",
    [string]$Code = "print('hello from pc')",
    [string]$FilePath = "",
    [string]$ConnectionHost = "127.0.0.1",
    [int]$Port = 18380,
    [int]$RemotePort = 18380,
    [switch]$NoAdbForward,
    [switch]$ShowLogs
)

$ErrorActionPreference = "Stop"

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [scriptblock]$Command
    )

    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "命令执行失败，退出码：$LASTEXITCODE"
    }
}

if (-not $NoAdbForward) {
    Invoke-Checked { & $AdbPath @("forward", "tcp:$Port", "tcp:$RemotePort") | Out-Null }
}

if ($FilePath -ne "") {
    $resolvedFilePath = Resolve-Path -LiteralPath $FilePath
    $Code = Get-Content -LiteralPath $resolvedFilePath -Raw -Encoding UTF8
}

function Invoke-EngineJsonRpc {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Method,

        [hashtable]$Params = @{}
    )

    $request = @{
        jsonrpc = "2.0"
        id = 1
        method = $Method
        params = $Params
    } | ConvertTo-Json -Depth 5

    Invoke-RestMethod `
        -Uri "http://$ConnectionHost`:$Port/jsonrpc" `
        -Method Post `
        -ContentType "application/json; charset=utf-8" `
        -Body $request
}

function Assert-JsonRpcSuccess {
    param(
        [Parameter(Mandatory = $true)]
        $Response,

        [Parameter(Mandatory = $true)]
        [string]$Action
    )

    if ($null -ne $Response.error) {
        throw "$Action JSON-RPC 失败：$($Response.error.message)"
    }
}

$logStartId = 0
if ($ShowLogs) {
    $beforeLogs = Invoke-EngineJsonRpc `
        -Method "log.drain" `
        -Params @{
            afterId = 0
        }
    Assert-JsonRpcSuccess -Response $beforeLogs -Action "读取初始日志"
    $logStartId = [int]$beforeLogs.result.lastId
}

$request = @{
    jsonrpc = "2.0"
    id = 1
    method = "script.run"
    params = @{
        language = "lua"
        code = $Code
    }
} | ConvertTo-Json -Depth 5

$runResult = Invoke-RestMethod `
    -Uri "http://$ConnectionHost`:$Port/jsonrpc" `
    -Method Post `
    -ContentType "application/json; charset=utf-8" `
    -Body $request

$runResult
Assert-JsonRpcSuccess -Response $runResult -Action "运行脚本"

if ($runResult.result.status -ne "finished") {
    throw "运行脚本失败：$($runResult.result.message)"
}

if ($ShowLogs) {
    $afterLogs = Invoke-EngineJsonRpc `
        -Method "log.drain" `
        -Params @{
            afterId = $logStartId
        }
    Assert-JsonRpcSuccess -Response $afterLogs -Action "读取脚本日志"

    foreach ($entry in $afterLogs.result.entries) {
        "[log#$($entry.id)] $($entry.message)"
    }
}
