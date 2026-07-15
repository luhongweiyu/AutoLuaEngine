<#
文件用途：校验脚本文档的目录与函数页是否保持一致。

本脚本只读取仓库文件，不会修改文档。它用于在提交前发现四类常见问题：
1. catalog.json 引用了不存在的 Markdown 文件；
2. 新增函数页没有加入目录，或目录重复引用同一文件；
3. 函数页的 params / returns 元数据不在文件开头，导致交互式文档无法解析。
4. 带 cmd 的脚本 API 页面缺少统一的名称、语法、参数、返回值或详细说明区块。
#>

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$仓库根目录 = Split-Path -Parent $PSScriptRoot
$文档根目录 = Join-Path $仓库根目录 'docs\脚本文档'
$目录文件 = Join-Path $文档根目录 'catalog.json'
$Markdown根目录 = Join-Path $文档根目录 'md'
$错误列表 = [System.Collections.Generic.List[string]]::new()
$目录文档路径 = [System.Collections.Generic.List[string]]::new()
$函数文档路径 = [System.Collections.Generic.List[string]]::new()

function 添加错误 {
    param([string]$内容)
    $错误列表.Add($内容)
}

function 获取属性值 {
    param(
        [object]$对象,
        [string]$名称
    )

    $属性 = $对象.PSObject.Properties[$名称]
    if ($null -eq $属性) {
        return $null
    }
    return $属性.Value
}

function 检查目录节点 {
    param([object[]]$节点列表)

    foreach ($节点 in $节点列表) {
        $名称 = 获取属性值 $节点 'name'
        if ([string]::IsNullOrWhiteSpace([string]$名称)) {
            添加错误 'catalog.json 存在未命名节点。'
        }

        $相对路径 = 获取属性值 $节点 'md'
        $命令 = 获取属性值 $节点 'cmd'
        if ($null -ne $相对路径) {
            $相对路径 = ([string]$相对路径).Replace('\', '/')
            if ($相对路径 -notmatch '^md/.+\.md$') {
                添加错误 "目录项“$名称”的 md 路径无效：$相对路径"
            } else {
                $目录文档路径.Add($相对路径)
                if (-not [string]::IsNullOrWhiteSpace([string]$命令)) {
                    $函数文档路径.Add($相对路径)
                }
                $实际路径 = Join-Path $文档根目录 ($相对路径.Replace('/', '\'))
                if (-not (Test-Path -LiteralPath $实际路径 -PathType Leaf)) {
                    添加错误 "目录项“$名称”引用的文件不存在：$相对路径"
                }
            }
        }

        $子项 = 获取属性值 $节点 'items'
        if ($null -ne $子项) {
            检查目录节点 @($子项)
        }
    }
}

if (-not (Test-Path -LiteralPath $目录文件 -PathType Leaf)) {
    添加错误 "缺少目录文件：$目录文件"
} elseif (-not (Test-Path -LiteralPath $Markdown根目录 -PathType Container)) {
    添加错误 "缺少 Markdown 目录：$Markdown根目录"
} else {
    try {
        $目录 = Get-Content -LiteralPath $目录文件 -Raw -Encoding UTF8 | ConvertFrom-Json
        检查目录节点 @($目录)
    } catch {
        添加错误 "catalog.json 无法解析：$($_.Exception.Message)"
    }
}

# 目录文件成功解析后，再检查每个函数页是否拥有可被 HTML 读取的前置元数据。
$全部函数页 = @()
if (Test-Path -LiteralPath $Markdown根目录 -PathType Container) {
    $全部函数页 = Get-ChildItem -LiteralPath $Markdown根目录 -Recurse -File -Filter '*.md' |
        Sort-Object FullName

    foreach ($文件 in $全部函数页) {
        $相对路径 = $文件.FullName.Substring($文档根目录.Length + 1).Replace('\', '/')
        $行 = @(Get-Content -LiteralPath $文件.FullName -Encoding UTF8)
        if ($行.Count -lt 4 -or $行[0] -ne '---') {
            添加错误 "函数页前置元数据无效：$相对路径"
            continue
        }

        $结束行 = -1
        for ($索引 = 1; $索引 -lt $行.Count; $索引++) {
            if ($行[$索引] -eq '---') {
                $结束行 = $索引
                break
            }
        }
        if ($结束行 -lt 0) {
            添加错误 "函数页缺少元数据结束标记：$相对路径"
            continue
        }

        $元数据 = $行[1..($结束行 - 1)]
        if (-not ($元数据 -match '^params:')) {
            添加错误 "函数页缺少 params 元数据：$相对路径"
        }
        if (-not ($元数据 -match '^returns:')) {
            添加错误 "函数页缺少 returns 元数据：$相对路径"
        }

        # 仅函数页要求统一阅读结构；概述、规则、C ABI 等技术页面保留各自的写法。
        if ($相对路径 -in $函数文档路径) {
            $正文 = [string]::Join([Environment]::NewLine, $行[($结束行 + 1)..($行.Count - 1)])
            foreach ($字段 in @('**方法名称：**', '**语法：**', '**参数说明：**', '**详细说明：**')) {
                if (-not $正文.Contains($字段)) {
                    添加错误 "函数页缺少“$字段”字段：$相对路径"
                }
            }
            if ($正文 -notmatch '(?m)^\| 返回值 \| 说明 \|\s*$') {
                添加错误 "函数页缺少“返回值 | 说明”表格：$相对路径"
            }
            if ($正文 -notmatch '(?m)^```lua\s*$') {
                添加错误 "函数页缺少 Lua 使用示例：$相对路径"
            }
        }
    }
}

$重复目录路径 = $目录文档路径 | Group-Object | Where-Object Count -gt 1
foreach ($重复项 in $重复目录路径) {
    添加错误 "catalog.json 重复引用文件：$($重复项.Name)"
}

$实际相对路径 = $全部函数页 | ForEach-Object {
    $_.FullName.Substring($文档根目录.Length + 1).Replace('\', '/')
}
foreach ($相对路径 in $实际相对路径) {
    if ($相对路径 -notin $目录文档路径) {
        添加错误 "函数页未加入 catalog.json：$相对路径"
    }
}

if ($错误列表.Count -gt 0) {
    Write-Host '脚本文档校验失败：' -ForegroundColor Red
    $错误列表 | ForEach-Object { Write-Host "- $_" -ForegroundColor Red }
    exit 1
}

Write-Host "脚本文档校验通过：目录项 $($目录文档路径.Count) 个，函数页 $($全部函数页.Count) 个。" -ForegroundColor Green
