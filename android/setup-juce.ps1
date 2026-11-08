# Makes android\juce: JUCE 8.0.8 (the version the computer apps use) with the patches in android\juce-patches applied.
# Android builds with this copy only, so the patches never reach the Windows or Mac apps.
# Usage:  powershell -ExecutionPolicy Bypass -File android\setup-juce.ps1
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (Test-Path juce) { throw "android\juce already exists. Delete it yourself first if you want it made again." }

git clone --depth 1 --branch 8.0.8 https://github.com/juce-framework/JUCE.git juce
if ($LASTEXITCODE) { throw "Couldn't download JUCE." }

foreach ($patch in Get-ChildItem juce-patches\*.patch | Sort-Object Name) {
    git -C juce apply --whitespace=nowarn $patch.FullName
    if ($LASTEXITCODE) { throw "The patch $($patch.Name) doesn't apply to this JUCE." }
    Write-Host "Applied $($patch.Name)"
}
Write-Host "Ready: android\juce"
