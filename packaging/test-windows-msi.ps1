[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Msi,
    [string]$LogDir = '',
    [switch]$Uninstall,
    [string]$PreviousMsi = ''
)
$ErrorActionPreference = 'Stop'
if (-not $LogDir) { $LogDir = Join-Path (Split-Path $PSScriptRoot -Parent) 'build-windows\installer-test' }
$Msi = (Resolve-Path $Msi).Path
New-Item -ItemType Directory -Force $LogDir | Out-Null
$LogDir = (Resolve-Path $LogDir).Path
function Invoke-Msi([string]$Operation, [string]$Package, [string]$Log) {
    $process = Start-Process msiexec.exe -ArgumentList "$Operation `"$Package`" /qn /norestart /L*v `"$Log`"" -Wait -PassThru
    if ($process.ExitCode -notin @(0, 3010)) { throw "MSI $Operation failed: $($process.ExitCode). See $Log" }
}
function Get-AppEntries {
    foreach ($root in @('HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
                         'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall')) {
        Get-ChildItem $root -ErrorAction SilentlyContinue |
            ForEach-Object { Get-ItemProperty $_.PSPath } |
            Where-Object { $_.DisplayName -in @('Podcast Sanity', 'podcast-sanity') }
    }
}
if ($PreviousMsi) {
    $PreviousMsi = (Resolve-Path $PreviousMsi).Path
    Invoke-Msi '/i' $PreviousMsi (Join-Path $LogDir 'previous-install.log')
    $previous = @(Get-AppEntries)
    if ($previous.Count -ne 1 -or $previous[0].DisplayVersion -ne '0.1.0') {
        throw 'Upgrade baseline must contain exactly one 0.1.0 installation'
    }
}
$installLog = Join-Path $LogDir 'install.log'
Invoke-Msi '/i' $Msi $installLog
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
    $arp = @(Get-AppEntries)
    if ($arp.Count -ne 1) { throw "Expected one Apps & features entry, found $($arp.Count)" }
    if ($arp[0].DisplayName -ne 'Podcast Sanity' -or $arp[0].DisplayVersion -ne '0.2.0') {
        throw 'Expected Podcast Sanity 0.2.0 in Apps & features'
    }
    Write-Host "Apps & features: $($arp[0].DisplayName) $($arp[0].DisplayVersion) by $($arp[0].Publisher)"
    if ($arp[0].Publisher -match '@') { throw "Publisher must not contain an e-mail: $($arp[0].Publisher)" }
    $userProducts = @(Get-ChildItem 'HKCU:\Software\Microsoft\Installer\Products' |
        ForEach-Object { Get-ItemProperty $_.PSPath } |
        Where-Object { $_.ProductName -in @('Podcast Sanity', 'podcast-sanity') })
    if ($userProducts.Count -ne 1) { throw 'Expected exactly one current-user MSI product registration' }
    Write-Host "MSI verified. Application installed in $appDir"
} finally {
    foreach ($key in $saved.Keys) { [Environment]::SetEnvironmentVariable($key, $saved[$key], 'Process') }
}
if ($Uninstall) {
    Invoke-Msi '/x' $Msi (Join-Path $LogDir 'uninstall.log')
    if (Test-Path "$appDir\podcast-sanity.exe") { throw 'Uninstall left the application executable' }
    if (@(Get-AppEntries).Count -ne 0) { throw 'Uninstall left an Apps & features entry' }
    if (Test-Path $appDir) {
        if (@(Get-ChildItem $appDir -Recurse -File).Count -ne 0) { throw 'Uninstall left runtime files' }
    }
    Write-Host 'Uninstall verified: no runtime files or Apps & features entry remain.'
}
