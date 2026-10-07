Add-Type -AssemblyName System.Drawing

$workspace = Split-Path -Parent $PSScriptRoot
$target = Join-Path $workspace 'assets/images/cloud_save_logo.png'
$bitmap = New-Object System.Drawing.Bitmap(900, 550)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$graphics.Clear([System.Drawing.Color]::Transparent)

$x = 255.0; $y = 20.0; $width = 390.0; $height = 330.0
$cloud = New-Object System.Drawing.Drawing2D.GraphicsPath
$cloud.StartFigure()
$cloud.AddBezier($x + .17 * $width, $y + .85 * $height,
    $x - .02 * $width, $y + .83 * $height,
    $x - .02 * $width, $y + .48 * $height,
    $x + .22 * $width, $y + .46 * $height)
$cloud.AddBezier($x + .22 * $width, $y + .46 * $height,
    $x + .24 * $width, $y + .08 * $height,
    $x + .53 * $width, $y + .06 * $height,
    $x + .66 * $width, $y + .32 * $height)
$cloud.AddBezier($x + .66 * $width, $y + .32 * $height,
    $x + .91 * $width, $y + .27 * $height,
    $x + 1.05 * $width, $y + .62 * $height,
    $x + .89 * $width, $y + .83 * $height)
$cloud.AddLine($x + .89 * $width, $y + .83 * $height,
    $x + .17 * $width, $y + .85 * $height)
$cloud.CloseFigure()

$fill = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 10, 29, 54))
$outline = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 147, 206, 255), 11.0)
$outline.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
$graphics.FillPath($fill, $cloud)
$graphics.DrawPath($outline, $cloud)

$arrow = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 255, 196, 77), 17.0)
$arrow.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
$arrow.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
$cx = 465.0
$graphics.DrawLine($arrow, $cx, 152.0, $cx, 264.0)
$graphics.DrawLine($arrow, 422.0, 222.0, $cx, 268.0)
$graphics.DrawLine($arrow, 508.0, 222.0, $cx, 268.0)

$font = New-Object System.Drawing.Font('Arial', 75.0, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
$letters = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 233, 238, 249))
$wordmark = 'PS4 CLOUD SAVE'
$size = $graphics.MeasureString($wordmark, $font)
$graphics.DrawString($wordmark, $font, $letters, [single]((900 - $size.Width) / 2), 390.0)

$bitmap.Save($target, [System.Drawing.Imaging.ImageFormat]::Png)
$letters.Dispose(); $font.Dispose(); $arrow.Dispose(); $outline.Dispose()
$fill.Dispose(); $cloud.Dispose(); $graphics.Dispose(); $bitmap.Dispose()
