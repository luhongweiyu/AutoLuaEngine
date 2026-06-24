param(
    [string]$AdbPath = "D:\soft\Android\Sdk\platform-tools\adb.exe"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$androidRoot = Join-Path $repoRoot "engines\android"
$apkPath = Join-Path $androidRoot "app\build\outputs\apk\debug\app-debug.apk"
$engineHost = "127.0.0.1"
$enginePort = 18380
$engineRemotePort = 18380

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

function Invoke-Adb {
    param(
        [Parameter(Mandatory = $true, Position = 0)]
        [string[]]$Args
    )

    & $AdbPath @Args
    if ($LASTEXITCODE -ne 0) {
        throw "adb 命令执行失败，退出码：$LASTEXITCODE"
    }
}

function Get-NodeCenterByResourceId {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ResourceId
    )

    $remotePath = "/sdcard/window.xml"
    $localPath = Join-Path $env:TEMP "autolua-window.xml"

    $dumpOutput = $null
    $dumpSucceeded = $false
    for ($i = 0; $i -lt 5; $i++) {
        $oldErrorActionPreference = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        $dumpOutput = & $AdbPath @("shell", "uiautomator", "dump", $remotePath) 2>&1
        $dumpExitCode = $LASTEXITCODE
        $ErrorActionPreference = $oldErrorActionPreference

        if ($dumpExitCode -eq 0) {
            $dumpSucceeded = $true
            break
        }
        Start-Sleep -Milliseconds 500
    }

    if (-not $dumpSucceeded) {
        throw "uiautomator dump 失败：$dumpOutput"
    }

    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $pullOutput = & $AdbPath @("pull", $remotePath, $localPath) 2>&1
    $ErrorActionPreference = $oldErrorActionPreference
    if ($LASTEXITCODE -ne 0) {
        throw "adb pull 失败：$pullOutput"
    }

    $xml = Get-Content -LiteralPath $localPath -Raw
    Remove-Item -LiteralPath $localPath -Force

    $escapedId = [regex]::Escape($ResourceId)
    $pattern = "resource-id=`"$escapedId`"[^>]*bounds=`"\[(\d+),(\d+)\]\[(\d+),(\d+)\]`""
    $match = [regex]::Match($xml, $pattern)
    if (-not $match.Success) {
        throw "未找到 UI 节点：$ResourceId"
    }

    $left = [int]$match.Groups[1].Value
    $top = [int]$match.Groups[2].Value
    $right = [int]$match.Groups[3].Value
    $bottom = [int]$match.Groups[4].Value

    return @{
        X = [int](($left + $right) / 2)
        Y = [int](($top + $bottom) / 2)
    }
}

function Invoke-TapByResourceId {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ResourceId
    )

    $center = Get-NodeCenterByResourceId $ResourceId
    Invoke-Adb @("shell", "input", "tap", "$($center.X)", "$($center.Y)")
}

function Invoke-EngineJsonRpc {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Method,

        [hashtable]$Params = @{}
    )

    Invoke-Adb @("forward", "tcp:$enginePort", "tcp:$engineRemotePort")

    $request = @{
        jsonrpc = "2.0"
        id = 1
        method = $Method
        params = $Params
    } | ConvertTo-Json -Depth 5

    Invoke-RestMethod `
        -Uri "http://$engineHost`:$enginePort/jsonrpc" `
        -Method Post `
        -ContentType "application/json; charset=utf-8" `
        -Body $request
}

Push-Location $androidRoot
try {
    $env:JAVA_HOME = "D:\soft\Android\Android Studio\jbr"
    $env:Path = "$env:JAVA_HOME\bin;$env:Path"

    Invoke-Checked { .\gradlew.bat assembleDebug }

    Invoke-Adb @("devices")
    Invoke-Adb @("logcat", "-c")
    Invoke-Adb @("install", "-r", $apkPath)
    Invoke-Adb @("shell", "am", "start", "-n", "com.autolua.engine/.MainActivity")

    Start-Sleep -Seconds 2

    Invoke-TapByResourceId "com.autolua.engine:id/button_run_lua"
    Start-Sleep -Seconds 2

    Invoke-TapByResourceId "com.autolua.engine:id/button_run_error"
    Start-Sleep -Seconds 1

    Invoke-TapByResourceId "com.autolua.engine:id/button_run_loop"
    Start-Sleep -Milliseconds 300
    Invoke-TapByResourceId "com.autolua.engine:id/button_stop"
    Start-Sleep -Seconds 1

    Invoke-TapByResourceId "com.autolua.engine:id/button_run_touch"
    Start-Sleep -Seconds 1

    Invoke-TapByResourceId "com.autolua.engine:id/button_run_screen"
    Start-Sleep -Seconds 1

    $deviceInfoResult = Invoke-EngineJsonRpc `
        -Method "device.info" `
        -Params @{}

    if ($deviceInfoResult.result.platform -ne "android") {
        throw "HTTP device.info platform 验证失败：$($deviceInfoResult | ConvertTo-Json -Depth 5)"
    }

    if ([string]::IsNullOrWhiteSpace($deviceInfoResult.result.engineVersion)) {
        throw "HTTP device.info engineVersion 为空：$($deviceInfoResult | ConvertTo-Json -Depth 5)"
    }

    if ($deviceInfoResult.result.luaVersion -notlike "Lua 5.4*") {
        throw "HTTP device.info luaVersion 验证失败：$($deviceInfoResult | ConvertTo-Json -Depth 5)"
    }

    if ($deviceInfoResult.result.httpPort -ne $engineRemotePort) {
        throw "HTTP device.info httpPort 验证失败：$($deviceInfoResult | ConvertTo-Json -Depth 5)"
    }

    $screenCaptureResult = Invoke-EngineJsonRpc `
        -Method "screen.capture" `
        -Params @{}

    if ($null -ne $screenCaptureResult.error) {
        if ($screenCaptureResult.error.message -ne "screen capture permission is not granted") {
            throw "HTTP screen.capture 错误验证失败：$($screenCaptureResult | ConvertTo-Json -Depth 5)"
        }
    } else {
        $capturedImage = $screenCaptureResult.result
        if ($capturedImage.type -ne "image" `
                -or $capturedImage.id -le 0 `
                -or $capturedImage.width -le 0 `
                -or $capturedImage.height -le 0 `
                -or $capturedImage.pixelStride -ne 4 `
                -or $capturedImage.byteLength -le 0 `
                -or $capturedImage.format -ne "rgba8888") {
            throw "HTTP screen.capture 句柄验证失败：$($screenCaptureResult | ConvertTo-Json -Depth 5)"
        }

        $releaseResult = Invoke-EngineJsonRpc `
            -Method "image.release" `
            -Params @{
                id = $capturedImage.id
            }

        if ($releaseResult.result.released -ne $true) {
            throw "HTTP image.release 验证失败：$($releaseResult | ConvertTo-Json -Depth 5)"
        }
    }

    $httpResult = Invoke-EngineJsonRpc `
        -Method "script.run" `
        -Params @{
            language = "lua"
            code = @"
local info = m.device.info()
print('hello from pc http verify')
print('verify lua version =', info.luaVersion)
local 中文变量 = '中文标识符正常'
local function 中文函数(内容)
    return 内容 .. ' OK'
end
if 中文函数(中文变量) ~= '中文标识符正常 OK' then
    error('中文函数验证失败')
end
_G.中文全局 = 456
if _G['中文' .. '全局'] ~= 456 then
    error('中文全局字段验证失败')
end
print('中文标识符验证通过')
if type(m) ~= 'table' or type(lr) ~= 'table' or type(cd) ~= 'table' then
    error('api namespace check failed')
end
local switched, switchErr = useApi('lr')
if not switched then
    error('useApi lr failed: ' .. tostring(switchErr))
end
if tap ~= lr.tap then
    error('lr global tap export failed')
end
switched, switchErr = useApi('cd')
if not switched then
    error('useApi cd failed: ' .. tostring(switchErr))
end
if tap ~= cd.tap then
    error('cd global tap export failed')
end
switched, switchErr = useApi('m')
if not switched then
    error('useApi m failed: ' .. tostring(switchErr))
end
if tap ~= m.tap then
    error('m global tap export failed')
end
local img = {
    id = 999999,
    type = 'image',
}
local pixel, pixelErr = m.image.getPixel(img, 0, 0)
if pixel ~= nil or pixelErr ~= 'image handle is not found' then
    error('image.getPixel invalid handle check failed')
end
local colors, colorsErr = m.image.getPixels(img, { 0, 0 })
if colors ~= nil or colorsErr ~= 'image handle is not found' then
    error('image.getPixels invalid handle check failed')
end
if type(m.key.isAccessibilityEnabled()) ~= 'boolean' then
    error('key.isAccessibilityEnabled check failed')
end
local path = m.file.appDataPath('http_verify_file_api.txt')
local ok, writeErr = m.file.write(path, 'temporary verify text')
if not ok then
    error('file.write failed: ' .. tostring(writeErr))
end
if m.file.exists(path) ~= true then
    error('file.exists after write failed')
end
local removed, removeErr = m.file.remove(path)
if not removed then
    error('file.remove failed: ' .. tostring(removeErr))
end
if m.file.exists(path) ~= false then
    error('file.exists after remove failed')
end
"@
        }

    if ($httpResult.result.status -ne "finished") {
        throw "HTTP script.run 验证失败：$($httpResult | ConvertTo-Json -Depth 5)"
    }

    $statusResult = Invoke-EngineJsonRpc `
        -Method "script.status" `
        -Params @{
            taskId = $httpResult.result.taskId
        }

    if ($statusResult.result.status -ne "finished") {
        throw "HTTP script.status 验证失败：$($statusResult | ConvertTo-Json -Depth 5)"
    }

    $logResult = Invoke-EngineJsonRpc `
        -Method "log.drain" `
        -Params @{
            afterId = 0
        }

    $matchedLog = $logResult.result.entries |
        Where-Object { $_.message -eq "hello from pc http verify" } |
        Select-Object -First 1
    if ($null -eq $matchedLog) {
        throw "HTTP log.drain 验证失败：$($logResult | ConvertTo-Json -Depth 5)"
    }

    $matchedNativeLog = $logResult.result.entries |
        Where-Object { $_.message -eq "native engine initialized" } |
        Select-Object -First 1
    if ($null -eq $matchedNativeLog) {
        throw "HTTP log.drain native 日志验证失败：$($logResult | ConvertTo-Json -Depth 5)"
    }

    $runToolPath = Join-Path $repoRoot "tools\android\run_lua_http.ps1"
    Invoke-Checked {
        powershell -ExecutionPolicy Bypass `
            -File $runToolPath `
            -AdbPath $AdbPath `
            -ConnectionHost $engineHost `
            -Port $enginePort `
            -RemotePort $engineRemotePort `
            -Code "print('hello from run_lua_http tool')" `
            -ShowLogs | Out-Null
    }

    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    powershell -ExecutionPolicy Bypass `
        -File $runToolPath `
        -AdbPath $AdbPath `
        -ConnectionHost $engineHost `
        -Port $enginePort `
        -RemotePort $engineRemotePort `
        -Code "error('expected run_lua_http failure')" | Out-Null
    $runToolFailureExitCode = $LASTEXITCODE
    $ErrorActionPreference = $oldErrorActionPreference
    if ($runToolFailureExitCode -eq 0) {
        throw "run_lua_http.ps1 失败脚本退出码验证失败"
    }

    Start-Sleep -Seconds 1

    Invoke-Adb @("logcat", "-d", "-s", "AutoLuaEngine")
} finally {
    Pop-Location
}
