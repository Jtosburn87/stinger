# Draws the app icon (installer\Stinger.png, 1024 px): the amber swell from the Stream Deck plugin on a rounded
# dark tile. JUCE turns it into the Windows .ico and the Mac .icns (ICON_BIG in CMakeLists.txt).
# Usage:  powershell -ExecutionPolicy Bypass -File installer\make-app-icon.ps1
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$size = 1024
$bmp = New-Object System.Drawing.Bitmap $size, $size
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = "AntiAlias"
$g.Clear([System.Drawing.Color]::Transparent)

# rounded tile with the Mac grid's margin (824 px tile in 1024)
$m = 100; $w = $size - 2 * $m; $r = 185
$tile = New-Object System.Drawing.Drawing2D.GraphicsPath
$tile.AddArc($m, $m, 2 * $r, 2 * $r, 180, 90)
$tile.AddArc($m + $w - 2 * $r, $m, 2 * $r, 2 * $r, 270, 90)
$tile.AddArc($m + $w - 2 * $r, $m + $w - 2 * $r, 2 * $r, 2 * $r, 0, 90)
$tile.AddArc($m, $m + $w - 2 * $r, 2 * $r, 2 * $r, 90, 90)
$tile.CloseFigure()
$fill = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.Point 0, $m), (New-Object System.Drawing.Point 0, ($m + $w)),
        ([System.Drawing.Color]::FromArgb(38, 43, 50)), ([System.Drawing.Color]::FromArgb(16, 18, 22))
$g.FillPath($fill, $tile)
$g.DrawPath((New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(60, 255, 255, 255)), 6), $tile)

# the swell (same points as streamdeck\make-icons.ps1, on a 24 grid inset in the tile), with a soft glow under it
$g.SetClip($tile)
$inset = $w * 0.1; $s = ($w - 2 * $inset) / 24.0
$pts = [System.Drawing.PointF[]](@((2,12),(6,12),(9,4),(13,20),(16,12),(22,12)) |
       ForEach-Object { New-Object System.Drawing.PointF ($m + $inset + $_[0] * $s), ($m + $inset + $_[1] * $s) })
foreach ($layer in @(@(4.0, 18), @(2.8, 40), @(1.7, 255))) {
    $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb($layer[1], 242, 163, 58)), ($layer[0] * $s)
    $pen.LineJoin = "Round"; $pen.StartCap = "Round"; $pen.EndCap = "Round"
    $g.DrawLines($pen, $pts)
}

$g.Dispose()
$out = Join-Path $PSScriptRoot "Stinger.png"
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
Write-Host "Icon: $out"


