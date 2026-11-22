$ErrorActionPreference = 'Stop'
& "$PSScriptRoot/build.ps1"
$folder = Join-Path $PSScriptRoot 'dist/native'
Copy-Item -LiteralPath "$PSScriptRoot/LICENSE","$PSScriptRoot/README.md" -Destination $folder
$zip = Join-Path $PSScriptRoot 'dist/MusicWidget-win-x64.zip'
$files = 'MusicWidget.exe','LICENSE','README.md' | ForEach-Object { Join-Path $folder $_ }
Compress-Archive -LiteralPath $files -DestinationPath $zip -Force
Write-Output $zip
