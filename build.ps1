param([switch]$Benchmark)
$ErrorActionPreference = 'Stop'
Push-Location -LiteralPath $PSScriptRoot
try {
    $includeDirs = @()
    $libDirs = @()
    $compiler = (Get-Command cl.exe -ErrorAction SilentlyContinue).Source
    if (!$compiler) {
        $vc = Get-ChildItem '.build-tools/VC/Tools/MSVC' -Directory -ErrorAction SilentlyContinue | Select-Object -Last 1
        $sdk = Get-ChildItem '.build-tools/microsoft.windows.sdk.cpp/c/Include' -Directory -ErrorAction SilentlyContinue | Select-Object -Last 1
        if (!$vc -or !$sdk) { throw 'Run build.ps1 in an x64 Developer PowerShell with MSVC and the Windows SDK installed.' }
        $compiler = Join-Path $vc.FullName 'bin/Hostx64/x64/cl.exe'
        $includeDirs = @("$($vc.FullName)/include") + @('shared','um','ucrt','winrt','cppwinrt' | ForEach-Object { Join-Path $sdk.FullName $_ })
        $sdkLib = (Resolve-Path '.build-tools/microsoft.windows.sdk.cpp.x64/c').Path
        $libDirs = @("$($vc.FullName)/lib/x64", "$sdkLib/um/x64", "$sdkLib/ucrt/x64")
    }
    $output = if ($Benchmark) { 'build/fixture' } else { 'dist/native' }
    $objects = if ($Benchmark) { 'build/fixture-obj' } else { 'build/obj' }
    New-Item -Force -ItemType Directory $objects,$output | Out-Null
    $common = @('/nologo','/std:c++20','/EHsc','/utf-8','/MT','/O1','/Gy','/Gw','/W4','/permissive-','/DUNICODE','/D_UNICODE','/DNOMINMAX','/DWIN32_LEAN_AND_MEAN',"/Fo$objects/")
    $common += @($includeDirs | Where-Object { $_ } | ForEach-Object { "/I$_" })
    $link = @('/link','/OPT:REF','/OPT:ICF','/INCREMENTAL:NO') + @($libDirs | Where-Object { $_ } | ForEach-Object { "/LIBPATH:$_" })
    $libraries = @('windowsapp.lib','user32.lib','gdi32.lib','shell32.lib','ole32.lib','oleaut32.lib','advapi32.lib','dwmapi.lib','uiautomationcore.lib')
    if ($Benchmark) {
        & $compiler @common 'bench/producer.cpp' "/Fe$output/Spotify.WidgetFixture.exe" @link '/SUBSYSTEM:CONSOLE' @libraries
    } else {
        & $compiler @common 'native/main.cpp' 'native/view.cpp' 'native/layout.cpp' "/Fe$output/MusicWidget.exe" @link '/SUBSYSTEM:WINDOWS' @libraries
    }
    if ($LASTEXITCODE) { throw "MSVC returned $LASTEXITCODE" }
} finally { Pop-Location }
