[CmdletBinding()]
param(
    [string]$QtRoot = "$env:USERPROFILE\deps\qt\6.5.3\msvc2019_64",
    [string]$VcpkgRoot = "$env:USERPROFILE\deps\podcast-vcpkg",
    [string]$WixRoot = "$env:USERPROFILE\deps\wix3",
    [string]$Generator = 'Visual Studio 16 2019',
    [string]$BuildDir = 'build-windows',
    [string]$SignThumbprint = '',
    [string]$TimestampUrl = 'http://timestamp.digicert.com'
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
function Sign-File([string]$Path) {
    if (-not $SignThumbprint) { return }
    $signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" |
        Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $signtool) { throw 'signtool.exe not found: install the Windows SDK signing tools' }
    & $signtool.FullName sign /sha1 $SignThumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 $Path
    if ($LASTEXITCODE -ne 0) { throw "signtool failed on $Path" }
}
if (-not $SignThumbprint) {
    Write-Warning 'Unsigned build: Windows SmartScreen will warn on first launch. Pass -SignThumbprint to sign.'
}
Sign-File "$BuildDir\Release\podcast-sanity.exe"
Sign-File "$BuildDir\Release\podcast-sanity-render.exe"
Invoke-Checked "$tools\ctest.exe" @('--test-dir', $BuildDir, '-C', 'Release', '--output-on-failure')
Invoke-Checked "$tools\cpack.exe" @('--config', "$BuildDir/CPackConfig.cmake", '-C', 'Release',
    '-G', 'WIX;ZIP', '-B', "$BuildDir/packages")
Get-ChildItem "$BuildDir\packages\*.msi" | ForEach-Object { Sign-File $_.FullName }
Write-Host "Packages ready in $BuildDir\packages"
