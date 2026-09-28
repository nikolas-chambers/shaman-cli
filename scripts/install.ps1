# Install shaman on Windows:
#   irm https://raw.githubusercontent.com/nikolas-chambers/shaman-cli/main/scripts/install.ps1 | iex
# Puts shaman.exe in %LOCALAPPDATA%\Programs\shaman and adds it to your user PATH.
# $env:SHAMAN_VERSION = "v0.2.0" picks a release (default: latest).
$ErrorActionPreference = "Stop"
$repo = if ($env:SHAMAN_REPO) { $env:SHAMAN_REPO } else { "nikolas-chambers/shaman-cli" }
$version = if ($env:SHAMAN_VERSION) { $env:SHAMAN_VERSION } else { "latest" }
$asset = "shaman-windows-x64.exe"
$url = if ($version -eq "latest") { "https://github.com/$repo/releases/latest/download/$asset" }
       else { "https://github.com/$repo/releases/download/$version/$asset" }
$dir = Join-Path $env:LOCALAPPDATA "Programs\shaman"
New-Item -ItemType Directory -Force $dir | Out-Null
Write-Host "Downloading $asset ($version)..."
Invoke-WebRequest -UseBasicParsing $url -OutFile (Join-Path $dir "shaman.exe")
$path = [Environment]::GetEnvironmentVariable("Path", "User")
if (-not ($path -split ";" | Where-Object { $_ -eq $dir })) {
  [Environment]::SetEnvironmentVariable("Path", "$path;$dir", "User")
  Write-Host "Added $dir to your PATH (open a new terminal)."
}
& (Join-Path $dir "shaman.exe") --version
Write-Host "Next: shaman auth login <provider>"
