param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

# 文件用途：从与应用内一致的小鱼矢量造型生成 Windows EXE 使用的多尺寸 ICO。
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

function New-Point([float]$x, [float]$y) {
    return [System.Drawing.PointF]::new($x, $y)
}

function New-Brush([string]$color) {
    return [System.Drawing.SolidBrush]::new(
        [System.Drawing.ColorTranslator]::FromHtml($color))
}

function New-RoundedBackgroundPath {
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $path.AddArc(2, 2, 46, 46, 180, 90)
    $path.AddArc(60, 2, 46, 46, 270, 90)
    $path.AddArc(60, 60, 46, 46, 0, 90)
    $path.AddArc(2, 60, 46, 46, 90, 90)
    $path.CloseFigure()
    return $path
}

function New-TailPath {
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $path.AddPolygon([System.Drawing.PointF[]]@(
        (New-Point 77 43),
        (New-Point 96 31),
        (New-Point 91 48),
        (New-Point 100 54),
        (New-Point 91 60),
        (New-Point 96 77),
        (New-Point 77 65)
    ))
    return $path
}

function New-BodyPath {
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $path.StartFigure()
    $path.AddBezier(
        (New-Point 16 54), (New-Point 16 36.3),
        (New-Point 31.3 23), (New-Point 51 23))
    $path.AddBezier(
        (New-Point 51 23), (New-Point 69.8 23),
        (New-Point 84 36.5), (New-Point 84 54))
    $path.AddBezier(
        (New-Point 84 54), (New-Point 84 71.5),
        (New-Point 69.8 85), (New-Point 51 85))
    $path.AddBezier(
        (New-Point 51 85), (New-Point 31.3 85),
        (New-Point 16 71.7), (New-Point 16 54))
    $path.CloseFigure()
    return $path
}

function New-DorsalFinPath {
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $path.StartFigure()
    $path.AddBezier(
        (New-Point 54 24), (New-Point 64 25),
        (New-Point 72 29), (New-Point 77 36))
    $path.AddBezier(
        (New-Point 77 36), (New-Point 68 35),
        (New-Point 61 37), (New-Point 55 42))
    $path.CloseFigure()
    return $path
}

function New-SideFinPath {
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $path.StartFigure()
    $path.AddBezier(
        (New-Point 53 55), (New-Point 65 57),
        (New-Point 71 64), (New-Point 69 74))
    $path.AddBezier(
        (New-Point 69 74), (New-Point 61 70),
        (New-Point 55 65), (New-Point 49 59))
    $path.CloseFigure()
    return $path
}

function New-LogoPng([int]$size) {
    $bitmap = [System.Drawing.Bitmap]::new(
        $size,
        $size,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $backgroundPath = New-RoundedBackgroundPath
    $tailPath = New-TailPath
    $bodyPath = New-BodyPath
    $dorsalFinPath = New-DorsalFinPath
    $sideFinPath = New-SideFinPath
    $backgroundBrush = New-Brush "#1479D1"
    $tailBrush = New-Brush "#4FD7C8"
    $bodyBrush = New-Brush "#FFFFFF"
    $sideFinBrush = New-Brush "#C4F5EF"
    $detailBrush = New-Brush "#123A63"
    $mouthPen = [System.Drawing.Pen]::new(
        [System.Drawing.ColorTranslator]::FromHtml("#123A63"), 2.6)
    $gillPen = [System.Drawing.Pen]::new(
        [System.Drawing.ColorTranslator]::FromHtml("#1479D1"), 3.0)
    $stream = [System.IO.MemoryStream]::new()

    try {
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.CompositingQuality =
            [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $scale = $size / 108.0
        $graphics.ScaleTransform($scale, $scale)

        $graphics.FillPath($backgroundBrush, $backgroundPath)
        $graphics.FillPath($tailBrush, $tailPath)
        $graphics.FillPath($bodyBrush, $bodyPath)
        $graphics.FillPath($tailBrush, $dorsalFinPath)
        $graphics.FillPath($sideFinBrush, $sideFinPath)
        $graphics.FillEllipse($detailBrush, 33, 39, 12, 12)

        $mouthPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $mouthPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
        $graphics.DrawBezier(
            $mouthPen,
            (New-Point 21 61), (New-Point 24 63),
            (New-Point 28 63), (New-Point 31 61))

        $gillPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $gillPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
        $graphics.DrawBezier(
            $gillPen,
            (New-Point 48 39), (New-Point 43 46),
            (New-Point 43 61), (New-Point 48 68))

        $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
        return $stream.ToArray()
    } finally {
        $stream.Dispose()
        $mouthPen.Dispose()
        $gillPen.Dispose()
        $detailBrush.Dispose()
        $sideFinBrush.Dispose()
        $bodyBrush.Dispose()
        $tailBrush.Dispose()
        $backgroundBrush.Dispose()
        $sideFinPath.Dispose()
        $dorsalFinPath.Dispose()
        $bodyPath.Dispose()
        $tailPath.Dispose()
        $backgroundPath.Dispose()
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$images = [System.Collections.Generic.List[byte[]]]::new()
foreach ($size in $sizes) {
    $images.Add((New-LogoPng $size))
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
