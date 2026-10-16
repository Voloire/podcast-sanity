[CmdletBinding()]
param(
    [string]$QtRoot = "$env:USERPROFILE\deps\qt\6.5.3\msvc2019_64",
    [string]$VcpkgRoot = "$env:USERPROFILE\deps\podcast-vcpkg",
    [string]$WixRoot = "$env:USERPROFILE\deps\wix3",
    [string]$Generator = 'Visual Studio 16 2019',
    [string]$BuildDir = 'build-windows'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $root $BuildDir }
foreach ($file in @("$QtRoot\bin\windeployqt.exe", "$QtRoot\lib\cmake\Qt6Multimedia\Qt6MultimediaConfig.cmake",
                    "$VcpkgRoot\scripts\buildsystems\vcpkg.cmake", "$WixRoot\candle.exe", "$WixRoot\light.exe")) {
    if (-not (Test-Path $file)) { throw "Missing prerequisite: $file" }
}
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    $cmake = Get-ChildItem "$env:APPDATA\Python\Python*\Scripts\cmake.exe" |
        Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $cmake) { throw 'Install CMake >= 4.3 and add it to PATH.' }
    $cmake = $cmake.FullName
} else { $cmake = $cmake.Source }
$tools = Split-Path $cmake
function Invoke-Checked([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed: $LASTEXITCODE" }
}
Invoke-Checked $cmake @('-S', $root, '-B', $BuildDir, '-G', $Generator, '-A', 'x64',
    "-DCMAKE_PREFIX_PATH=$QtRoot", "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot/scripts/buildsystems/vcpkg.cmake",
    '-DVCPKG_TARGET_TRIPLET=x64-windows', '-DSANITY_WITH_GUI=ON', '-DSANITY_REQUIRE_CODECS=ON',
    "-DCPACK_WIX_ROOT=$WixRoot", '-DSANITY_WITH_EBUR128=OFF')
Invoke-Checked $cmake @('--build', $BuildDir, '--config', 'Release', '--parallel', "$([Environment]::ProcessorCount)")
Invoke-Checked "$tools\ctest.exe" @('--test-dir', $BuildDir, '-C', 'Release', '--output-on-failure')
Invoke-Checked "$tools\cpack.exe" @('--config', "$BuildDir/CPackConfig.cmake", '-C', 'Release',
    '-G', 'WIX;ZIP', '-B', "$BuildDir/packages")
Write-Host "Packages ready in $BuildDir\packages"
