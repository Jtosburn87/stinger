# Builds a Release, runs the tests, and copies Stinger.exe into dist\Stinger-<version>\ (the copy to run;
# build\ stays free for the next build), with the Stream Deck plugin installer next to it.
# Usage:  powershell -ExecutionPolicy Bypass -File release.ps1
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

& powershell -ExecutionPolicy Bypass -File build.ps1 -Config Release
if ($LASTEXITCODE) { throw "Build or tests failed." }

$version = (Select-String -Path CMakeLists.txt -Pattern 'project\(Stinger VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$version += (Select-String -Path CMakeLists.txt -Pattern 'STINGER_VERSION_LABEL "\$\{PROJECT_VERSION\}([^"]*)"').Matches[0].Groups[1].Value
$stage = "dist\Stinger-$version"
New-Item -ItemType Directory -Force $stage | Out-Null

# Stream Deck plugin: a .streamDeckPlugin is a zip with the .sdPlugin folder at its root; double-click installs it.
# (Built by hand: Compress-Archive in Windows PowerShell writes backslashes into the paths.)
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$plugin = Get-Item "streamdeck\com.stinger.streamdeck.sdPlugin"
# the plugin's version follows Stinger's, so Stream Deck sees each release as an update. Raise $pluginBuild when the
# plugin changes between versions of Stinger: Stream Deck ignores a plugin whose version it already has.
$pluginBuild = 1
$manifest = Join-Path $plugin.FullName "manifest.json"
$numeric = (Select-String -Path CMakeLists.txt -Pattern 'project\(Stinger VERSION ([0-9.]+)').Matches[0].Groups[1].Value + ".$pluginBuild"
$json = [System.IO.File]::ReadAllText($manifest)
$updated = [regex]::Replace($json, '"Version": "[0-9.]+"', '"Version": "' + $numeric + '"')
if ($updated -ne $json) { [System.IO.File]::WriteAllText($manifest, $updated, (New-Object System.Text.UTF8Encoding $false)) }
$target = Join-Path $PSScriptRoot "$stage\Stinger.streamDeckPlugin"
Remove-Item $target -ErrorAction SilentlyContinue
$archive = [System.IO.Compression.ZipFile]::Open($target, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($f in Get-ChildItem $plugin.FullName -Recurse -File) {
        $name = $plugin.Name + "/" + $f.FullName.Substring($plugin.FullName.Length + 1).Replace("\", "/")
        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $f.FullName, $name)
    }
} finally { $archive.Dispose() }
Write-Host "Stream Deck plugin: $stage\Stinger.streamDeckPlugin"

# Installer: Stinger.exe, the Stream Deck plugin and the download tools (yt-dlp, ffmpeg, deno) in one Setup.exe.
& powershell -ExecutionPolicy Bypass -File tools\fetch-tools.ps1 | Out-Null
$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe") |
        Where-Object { Test-Path $_ } | Select-Object -First 1
if ($iscc) {
    # the installer packs its own copy (Stinger.exe in dist may be running and locked)
    Copy-Item "build\Stinger_artefacts\Release\Stinger.exe" "$stage\Setup-Stinger.exe" -Force
    $abs = (Resolve-Path $stage).Path
    & $iscc /Q "/DAppVersion=$version" "/DStage=$abs" "/DExe=Setup-Stinger.exe" installer\Stinger.iss
    if ($LASTEXITCODE) { throw "Installer build failed." }
    Remove-Item "$stage\Setup-Stinger.exe"
    Write-Host "Installer: $stage\Stinger-$version-Setup.exe"
} else {
    Write-Warning "Inno Setup not found (winget install JRSoftware.InnoSetup): no installer built."
}

# A copy running from dist can't be replaced: leave it alone rather than touch it.
$stageExe = Join-Path $PSScriptRoot "$stage\Stinger.exe"
if (Get-Process Stinger -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $stageExe }) {
    Write-Warning "Stinger is running from $stage - built and tested, but Stinger.exe was NOT replaced. Close it and run release.ps1 again."
    exit 0
}
Copy-Item "build\Stinger_artefacts\Release\Stinger.exe" $stage -Force
Write-Host "`nRelease: $stageExe"
