[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Msi,
    [string]$LogDir = ''
)
$ErrorActionPreference = 'Stop'
if (-not $LogDir) { $LogDir = Join-Path (Split-Path $PSScriptRoot -Parent) 'build-windows\installer-test' }
$Msi = (Resolve-Path $Msi).Path
New-Item -ItemType Directory -Force $LogDir | Out-Null
$LogDir = (Resolve-Path $LogDir).Path
$installLog = Join-Path $LogDir 'install.log'
$process = Start-Process msiexec.exe -ArgumentList "/i `"$Msi`" /qn /norestart /L*v `"$installLog`"" -Wait -PassThru
if ($process.ExitCode -notin @(0, 3010)) { throw "MSI install failed: $($process.ExitCode). See $installLog" }
$appDir = Join-Path $env:LOCALAPPDATA 'Podcast Sanity\bin'
if (-not (Test-Path "$appDir\podcast-sanity.exe")) { throw "Installed app missing: $appDir" }
$saved = @{}
foreach ($key in @('PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_QPA_PLATFORM', 'QTDIR', 'QT_MEDIA_BACKEND')) {
    $saved[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
    [Environment]::SetEnvironmentVariable($key, $null, 'Process')
}
try {
    # Only Windows DLL locations remain. SDK and development DLLs cannot mask a
    # missing runtime in the installed payload.
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    & "$appDir\podcast-sanity-render.exe" --version
    if ($LASTEXITCODE -ne 0) { throw 'Installed CLI failed' }
    $process = Start-Process "$appDir\podcast-sanity.exe" -WorkingDirectory $LogDir `
        -ArgumentList "--selftest --selftest-seconds 8 --selftest-shots `"$LogDir\screenshots`"" `
        -RedirectStandardOutput "$LogDir\gui.log" -RedirectStandardError "$LogDir\gui-errors.log" -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "Installed GUI failed: $($process.ExitCode)" }
    $output = Get-Content "$LogDir\gui.log" -Raw
    if ($output -notmatch 'selftest: OK' -or $output -notmatch 'MP3') {
        throw 'Installed GUI did not complete mastering and MP3 export'
    }
    Write-Host $output
    Write-Host "MSI verified. Application installed in $appDir"
} finally {
    foreach ($key in $saved.Keys) { [Environment]::SetEnvironmentVariable($key, $saved[$key], 'Process') }
}
