# Downloads the two helper programs Stinger uses for "Pull WAV" / Import > links into tools\bin
# (packed into the installer by release.ps1):
#   yt-dlp.exe  - fetches the audio from YouTube and other sites (https://github.com/yt-dlp/yt-dlp, Unlicense)
#   ffmpeg.exe  - converts it to a 24-bit WAV (https://www.gyan.dev/ffmpeg/builds, GPL build)
#   deno.exe    - the JavaScript runtime yt-dlp needs for YouTube (https://deno.com, MIT)
# Usage:  powershell -ExecutionPolicy Bypass -File tools\fetch-tools.ps1 [-Force]
param([switch]$Force)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"   # Invoke-WebRequest is very slow with the progress bar
$bin = Join-Path $PSScriptRoot "bin"
New-Item -ItemType Directory -Force $bin | Out-Null
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$ytdlp = Join-Path $bin "yt-dlp.exe"
if ($Force -or -not (Test-Path $ytdlp)) {
    Write-Host "Downloading yt-dlp..."
    Invoke-WebRequest "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe" -OutFile $ytdlp
}

$ffmpeg = Join-Path $bin "ffmpeg.exe"
if ($Force -or -not (Test-Path $ffmpeg)) {
    Write-Host "Downloading ffmpeg..."
    $zip = Join-Path $env:TEMP "stinger-ffmpeg.zip"
    $unpack = Join-Path $env:TEMP "stinger-ffmpeg"
    Invoke-WebRequest "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip" -OutFile $zip
    Remove-Item $unpack -Recurse -Force -ErrorAction SilentlyContinue
    Expand-Archive $zip $unpack
    $exe = Get-ChildItem $unpack -Recurse -Filter ffmpeg.exe | Select-Object -First 1
    Copy-Item $exe.FullName $ffmpeg -Force
    $license = Get-ChildItem $unpack -Recurse -Filter LICENSE | Select-Object -First 1
    if ($license) { Copy-Item $license.FullName (Join-Path $bin "ffmpeg-LICENSE.txt") -Force }
    Remove-Item $zip, $unpack -Recurse -Force
}

$deno = Join-Path $bin "deno.exe"
if ($Force -or -not (Test-Path $deno)) {
    # YouTube needs a JavaScript runtime for yt-dlp to see all its formats (https://github.com/yt-dlp/yt-dlp/wiki/EJS)
    Write-Host "Downloading deno..."
    $zip = Join-Path $env:TEMP "stinger-deno.zip"
    Invoke-WebRequest "https://github.com/denoland/deno/releases/latest/download/deno-x86_64-pc-windows-msvc.zip" -OutFile $zip
    Expand-Archive $zip $bin -Force
    Remove-Item $zip -Force
}

& $ytdlp --version
& $deno --version | Select-Object -First 1
& $ffmpeg -hide_banner -version | Select-Object -First 1
