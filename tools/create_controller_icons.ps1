Add-Type -AssemblyName System.Drawing

$workspace = Split-Path -Parent $PSScriptRoot
$output = Join-Path $workspace 'assets/images'
$size = 128
$gold = [System.Drawing.Color]::FromArgb(255, 255, 199, 83)
$blue = [System.Drawing.Color]::FromArgb(255, 110, 156, 255)
$violet = [System.Drawing.Color]::FromArgb(255, 142, 99, 224)

foreach ($symbol in @('cross', 'circle', 'square', 'triangle')) {
    $large = New-Object System.Drawing.Bitmap($size, $size)
    $graphics = [System.Drawing.Graphics]::FromImage($large)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear([System.Drawing.Color]::Transparent)

    $background = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(235, 9, 20, 44))
    $rim = New-Object System.Drawing.Pen($blue, 7)
    $rim.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $glyph = New-Object System.Drawing.Pen($gold, 9)
    $glyph.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $glyph.StartCap = $glyph.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $accent = New-Object System.Drawing.Pen($violet, 6)

    $graphics.FillEllipse($background, 7, 7, 114, 114)
    $graphics.DrawArc($rim, 10, 10, 108, 108, -89, 268)
    $graphics.DrawArc($accent, 10, 10, 108, 108, 183, 84)
    switch ($symbol) {
        'cross' {
            $graphics.DrawLine($glyph, 46, 46, 82, 82)
            $graphics.DrawLine($glyph, 82, 46, 46, 82)
        }
        'circle' { $graphics.DrawEllipse($glyph, 43, 43, 42, 42) }
        'square' { $graphics.DrawRectangle($glyph, 45, 45, 38, 38) }
        'triangle' {
            $points = [System.Drawing.Point[]]@(
                (New-Object System.Drawing.Point(64, 42)),
                (New-Object System.Drawing.Point(86, 82)),
                (New-Object System.Drawing.Point(42, 82)),
                (New-Object System.Drawing.Point(64, 42)))
            $graphics.DrawLines($glyph, $points)
        }
    }

    $small = New-Object System.Drawing.Bitmap(32, 32)
    $downsample = [System.Drawing.Graphics]::FromImage($small)
    $downsample.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $downsample.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $downsample.DrawImage($large, 0, 0, 32, 32)
    $small.Save((Join-Path $output "footer_ico_$symbol.png"), [System.Drawing.Imaging.ImageFormat]::Png)

    $downsample.Dispose()
    $small.Dispose()
    $accent.Dispose()
    $glyph.Dispose()
    $rim.Dispose()
    $background.Dispose()
    $graphics.Dispose()
    $large.Dispose()
}
