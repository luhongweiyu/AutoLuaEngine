param(
    [Parameter(Mandatory = $true)]
    [string]$SourcePath,
    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

# 文件用途：从品牌母版派生图生成 Windows EXE 使用的多尺寸 ICO。
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

if (-not (Test-Path -LiteralPath $SourcePath)) {
    throw "Icon source image not found: $SourcePath"
}

$sourceStream = [System.IO.File]::OpenRead(
    (Get-Item -LiteralPath $SourcePath).FullName)
$sourceImage = [System.Drawing.Image]::FromStream($sourceStream)

function New-IconPng([int]$Size) {
    $bitmap = [System.Drawing.Bitmap]::new(
        $Size,
        $Size,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $stream = [System.IO.MemoryStream]::new()

    try {
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $graphics.SmoothingMode =
            [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.InterpolationMode =
            [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode =
            [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.CompositingQuality =
            [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $graphics.DrawImage(
            $sourceImage,
            [System.Drawing.Rectangle]::new(0, 0, $Size, $Size))
        $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
        return $stream.ToArray()
    } finally {
        $stream.Dispose()
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$images = [System.Collections.Generic.List[byte[]]]::new()
try {
    foreach ($size in $sizes) {
        $images.Add((New-IconPng $size))
    }
} finally {
    $sourceImage.Dispose()
    $sourceStream.Dispose()
}

$outputDirectory = [System.IO.Path]::GetDirectoryName($OutputPath)
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
    [System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
}

$fileStream = [System.IO.File]::Open(
    $OutputPath,
    [System.IO.FileMode]::Create,
    [System.IO.FileAccess]::Write,
    [System.IO.FileShare]::None)
$writer = [System.IO.BinaryWriter]::new($fileStream)
try {
    # ICONDIR
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$images.Count)

    $imageOffset = 6 + 16 * $images.Count
    for ($index = 0; $index -lt $images.Count; ++$index) {
        $size = $sizes[$index]
        $dimension = if ($size -eq 256) { 0 } else { $size }
        $image = $images[$index]

        # ICONDIRENTRY
        $writer.Write([byte]$dimension)
        $writer.Write([byte]$dimension)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$image.Length)
        $writer.Write([uint32]$imageOffset)
        $imageOffset += $image.Length
    }

    foreach ($image in $images) {
        $writer.Write([byte[]]$image)
    }
} finally {
    $writer.Dispose()
    $fileStream.Dispose()
}
