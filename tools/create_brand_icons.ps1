Add-Type -AssemblyName System.Drawing

$workspace = Split-Path -Parent $PSScriptRoot
$markPath = Join-Path $workspace 'assets/images/znichka_icon.png'
$mark = [System.Drawing.Image]::FromFile($markPath)

function Draw-CloudSave($graphics, [single]$x, [single]$y, [single]$width, [single]$height) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.StartFigure()
    $path.AddBezier($x + .17 * $width, $y + .85 * $height,
        $x - .02 * $width, $y + .83 * $height,
        $x - .02 * $width, $y + .48 * $height,
        $x + .22 * $width, $y + .46 * $height)
    $path.AddBezier($x + .22 * $width, $y + .46 * $height,
        $x + .24 * $width, $y + .08 * $height,
        $x + .53 * $width, $y + .06 * $height,
        $x + .66 * $width, $y + .32 * $height)
    $path.AddBezier($x + .66 * $width, $y + .32 * $height,
        $x + .91 * $width, $y + .27 * $height,
        $x + 1.05 * $width, $y + .62 * $height,
        $x + .89 * $width, $y + .83 * $height)
    $path.AddLine($x + .89 * $width, $y + .83 * $height,
        $x + .17 * $width, $y + .85 * $height)
    $path.CloseFigure()
    $fill = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 10, 29, 54))
    $outline = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 147, 206, 255), [single]($width * .028))
    $outline.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $graphics.FillPath($fill, $path)
    $graphics.DrawPath($outline, $path)

    $gold = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 255, 196, 77), [single]($width * .044))
    $gold.StartCap = $gold.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $cx = [single]($x + .54 * $width)
    $graphics.DrawLine($gold, $cx, [single]($y + .40 * $height), $cx, [single]($y + .68 * $height))
    $graphics.DrawLine($gold, [single]($cx - .11 * $width), [single]($y + .58 * $height),
        $cx, [single]($y + .70 * $height))
    $graphics.DrawLine($gold, [single]($cx + .11 * $width), [single]($y + .58 * $height),
        $cx, [single]($y + .70 * $height))
    $gold.Dispose()
    $outline.Dispose()
    $fill.Dispose()
    $path.Dispose()
}

function Save-BrandIcon([int]$width, [int]$height, [string]$relativePath) {
    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.Clear([System.Drawing.Color]::FromArgb(255, 7, 16, 38))
    if ($width -eq $height) {
        $markSize = [single]($width * .29)
        $markX = [single]($width * .665)
        $markY = [single]($height * .065)
        $cloudX = [single]($width * .11)
        $cloudY = [single]($height * .29)
        $cloudWidth = [single]($width * .78)
        $cloudHeight = [single]($height * .55)
    } else {
        $markSize = [single]($height * .34)
        $markX = [single]($width - $markSize - $height * .08)
        $markY = [single]($height * .07)
        $cloudX = [single]($width * .09)
        $cloudY = [single]($height * .19)
        $cloudWidth = [single]($width * .65)
        $cloudHeight = [single]($height * .67)
    }
    $markHeight = [single]($markSize * $mark.Height / $mark.Width)
    Draw-CloudSave $graphics $cloudX $cloudY $cloudWidth $cloudHeight
    $graphics.DrawImage($mark, (New-Object System.Drawing.RectangleF($markX, $markY, $markSize, $markHeight)))
    $target = Join-Path $workspace $relativePath
    $bitmap.Save($target, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()
    Write-Output "$relativePath ($width x $height)"
}

try {
    Save-BrandIcon 512 512 'sce_sys/icon0.png'
    Save-BrandIcon 660 660 'sce_sys/icon0_4k.png'
    Save-BrandIcon 228 128 'sce_sys/save_data.png'
} finally {
    $mark.Dispose()
}
