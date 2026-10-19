# Draws the Stream Deck plugin's PNG icons (the keys themselves are drawn live by plugin.js).
# Usage:  powershell -ExecutionPolicy Bypass -File streamdeck\make-icons.ps1
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$out = Join-Path $PSScriptRoot "com.stinger.streamdeck.sdPlugin\images"
New-Item -ItemType Directory -Force $out | Out-Null

function New-Canvas ([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = "AntiAlias"
    $g.TextRenderingHint = "AntiAliasGridFit"
    return @($bmp, $g)
}

function Draw-Swell ($g, [single]$size, $colour, [single]$width) {
    $s = $size / 24.0
    $pen = New-Object System.Drawing.Pen $colour, ($width * $s)
    $pen.LineJoin = "Round"; $pen.StartCap = "Round"; $pen.EndCap = "Round"
    $pts = @((2,12),(6,12),(9,4),(13,20),(16,12),(22,12)) | ForEach-Object { New-Object System.Drawing.PointF ($_[0] * $s), ($_[1] * $s) }
    $g.DrawLines($pen, [System.Drawing.PointF[]]$pts)
}

function Save ($bmp, $g, [string]$name) {
    $g.Dispose(); $bmp.Save((Join-Path $out "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}

$amber = [System.Drawing.Color]::FromArgb(242, 163, 58)
$light = [System.Drawing.Color]::FromArgb(216, 216, 216)

# plugin icon (256 / 512) and category icon (28 / 56)
foreach ($pair in @(@("plugin", 256), @("plugin@2x", 512), @("category", 28), @("category@2x", 56))) {
    $bmp, $g = New-Canvas $pair[1]
    if ($pair[0] -like "plugin*") {
        $g.Clear([System.Drawing.Color]::FromArgb(21, 24, 28))
        Draw-Swell $g $pair[1] $amber 2.2
    } else {
        Draw-Swell $g $pair[1] $light 2.2
    }
    Save $bmp $g $pair[0]
}

# action list icons (20 / 40): light glyphs on transparent
function Action-Icon ([string]$name, [scriptblock]$draw) {
    foreach ($size in 20, 40) {
        $bmp, $g = New-Canvas $size
        & $draw $g ([single]$size)
        Save $bmp $g ("action-$name" + $(if ($size -eq 40) { "@2x" } else { "" }))
    }
}
function Glyph-Text ($g, [single]$size, [string]$text, [single]$scale) {
    $font = New-Object System.Drawing.Font "Segoe UI", ($size * $scale), ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
    $fmt = New-Object System.Drawing.StringFormat; $fmt.Alignment = "Center"; $fmt.LineAlignment = "Center"
    $g.DrawString($text, $font, (New-Object System.Drawing.SolidBrush $light), (New-Object System.Drawing.RectangleF 0, 0, $size, $size), $fmt)
}
Action-Icon "button"   { param($g, $s) $pen = New-Object System.Drawing.Pen $light, ($s / 12); $g.DrawRectangle($pen, $s * 0.15, $s * 0.15, $s * 0.7, $s * 0.7); Draw-Swell $g $s $light 1.6 }
Action-Icon "go"       { param($g, $s) Glyph-Text $g $s "GO" 0.5 }
Action-Icon "fadenext" { param($g, $s) $pen = New-Object System.Drawing.Pen $light, ($s / 10); $g.DrawLine($pen, $s * 0.1, $s * 0.8, $s * 0.9, $s * 0.2); $g.DrawLine($pen, $s * 0.1, $s * 0.2, $s * 0.9, $s * 0.8) }
Action-Icon "stopall"  { param($g, $s) $g.FillRectangle((New-Object System.Drawing.SolidBrush $light), $s * 0.22, $s * 0.22, $s * 0.56, $s * 0.56) }
Action-Icon "fadeall"  { param($g, $s) $pen = New-Object System.Drawing.Pen $light, ($s / 10); $g.DrawLine($pen, $s * 0.1, $s * 0.2, $s * 0.9, $s * 0.8) }
Action-Icon "bank"     { param($g, $s) Glyph-Text $g $s "A" 0.7 }
Action-Icon "countdown" { param($g, $s) $pen = New-Object System.Drawing.Pen $light, ($s / 12); $g.DrawEllipse($pen, $s * 0.12, $s * 0.12, $s * 0.76, $s * 0.76); $g.DrawLine($pen, $s * 0.5, $s * 0.5, $s * 0.5, $s * 0.24); $g.DrawLine($pen, $s * 0.5, $s * 0.5, $s * 0.7, $s * 0.6) }

# default key images (72 / 144), shown until the plugin draws the key
function Key-Image ([string]$name, $back, [string]$label) {
    foreach ($size in 72, 144) {
        $bmp, $g = New-Canvas $size
        $g.Clear($back)
        $font = New-Object System.Drawing.Font "Segoe UI", ($size * 0.2), ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
        $fmt = New-Object System.Drawing.StringFormat; $fmt.Alignment = "Center"; $fmt.LineAlignment = "Center"
        $g.DrawString($label, $font, [System.Drawing.Brushes]::White, (New-Object System.Drawing.RectangleF 0, 0, $size, $size), $fmt)
        Save $bmp $g ("key-$name" + $(if ($size -eq 144) { "@2x" } else { "" }))
    }
}
Key-Image "button"   ([System.Drawing.Color]::FromArgb(30, 34, 40))   "STINGER"
Key-Image "go"       ([System.Drawing.Color]::FromArgb(47, 143, 91))  "GO"
Key-Image "fadenext" ([System.Drawing.Color]::FromArgb(23, 38, 54))   "FADE`nNEXT"
Key-Image "stopall"  ([System.Drawing.Color]::FromArgb(201, 58, 63))  "STOP`nALL"
Key-Image "fadeall"  ([System.Drawing.Color]::FromArgb(38, 43, 50))   "FADE`nALL"
Key-Image "bank"     ([System.Drawing.Color]::FromArgb(30, 34, 40))   "BANK"
Key-Image "countdown" ([System.Drawing.Color]::FromArgb(42, 36, 24))   "-0:00"
Write-Host "Icons written to $out"
