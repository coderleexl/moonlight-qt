# Packaging smoke test for the Windows CI runner. Does not validate GPU streaming.
$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path $PSScriptRoot -Parent
$LogDir = Join-Path $RepoRoot 'build\preview-smoke'
New-Item -ItemType Directory -Force $LogDir | Out-Null
$InstallDir = Join-Path $env:LOCALAPPDATA 'Programs\MoonlightDeskPreview'
$Installer = Join-Path $RepoRoot 'build\preview-installer\MoonlightDeskPreview-Setup-x64.exe'
$OriginalKey = 'HKCU:\Software\Moonlight Game Streaming Project\Moonlight'
$OriginalBefore = if (Test-Path $OriginalKey) { (Get-ItemProperty $OriginalKey | ConvertTo-Json -Depth 8) } else { '<absent>' }
$Install = Start-Process $Installer -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$LogDir\install.log`"") -PassThru -Wait
if ($Install.ExitCode -ne 0) { throw "Installer failed: $($Install.ExitCode)" }
$UninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{C2FD0142-DBA5-4D98-8EAC-82786E4671A1}_is1'
if (-not (Test-Path $UninstallKey)) { throw 'Independent uninstall registration is missing' }
$Exe = Join-Path $InstallDir 'MoonlightDeskPreview.exe'
if (-not (Test-Path $Exe)) { throw 'Preview executable not installed' }
$Version = Start-Process $Exe -ArgumentList '--version' -WorkingDirectory $InstallDir -PassThru -Wait `
    -RedirectStandardOutput "$LogDir\version.txt" -RedirectStandardError "$LogDir\version-error.txt"
if ($Version.ExitCode -ne 0) { throw 'Installed client failed to load its runtime dependencies' }

$AppProcess = $null
$HostProcess = $null
try {
    $AppProcess = Start-Process $Exe -WorkingDirectory $InstallDir -PassThru `
        -RedirectStandardOutput "$LogDir\client.txt" -RedirectStandardError "$LogDir\client-error.txt"
    Start-Sleep -Seconds 8
    if ($AppProcess.HasExited) { throw "Client exited during startup: $($AppProcess.ExitCode)" }
    $ClientErrors = Get-Content "$LogDir\client-error.txt" -Raw
    if ($ClientErrors -match 'QQmlApplicationEngine failed|failed to load component|module .* is not installed') {
        throw 'Installed QML components did not load'
    }
    # Own only this test's process; the launcher must never close another Moonlight.
    Stop-Process -Id $AppProcess.Id -Force
    $AppProcess.WaitForExit()

    $HostDir = Join-Path $InstallDir 'host\sunshine'
    $ConfigDir = Join-Path $LogDir 'host-config'
    New-Item -ItemType Directory -Force "$ConfigDir\credentials" | Out-Null
    $Config = Join-Path $ConfigDir 'sunshine.conf'
    @"
sunshine_name = Moonlight Desk Preview CI
port = 48989
bind_address = 127.0.0.1
address_family = ipv4
origin_web_ui_allowed = pc
upnp = disabled
file_apps = $ConfigDir\apps.json
file_state = $ConfigDir\sunshine_state.json
credentials_file = $ConfigDir\sunshine_state.json
pkey = $ConfigDir\credentials\key.pem
cert = $ConfigDir\credentials\cert.pem
log_path = $ConfigDir\sunshine.log
"@ | Set-Content $Config -Encoding utf8NoBOM
    $HostProcess = Start-Process (Join-Path $HostDir 'sunshine.exe') -ArgumentList "`"$Config`"" `
        -WorkingDirectory $HostDir -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput "$LogDir\host.txt" -RedirectStandardError "$LogDir\host-error.txt"
    $Ready = $false
    for ($attempt = 0; $attempt -lt 45; $attempt++) {
        if ($HostProcess.HasExited) { throw "Bundled Sunshine exited: $($HostProcess.ExitCode)" }
        try {
            # Trust only this test's local self-signed endpoint.
            $Response = Invoke-WebRequest 'https://127.0.0.1:48990/' -SkipCertificateCheck -TimeoutSec 2
            if ($Response.StatusCode -eq 200) { $Ready = $true; break }
        } catch { Start-Sleep -Seconds 1 }
    }
    if (-not $Ready) { throw 'Bundled Sunshine web UI was not ready within the test deadline' }
    'Installer, client startup and bundled Sunshine web UI passed. GPU streaming was not tested.' | Set-Content "$LogDir\result.txt"
} finally {
    foreach ($Owned in @($HostProcess, $AppProcess)) {
        if ($Owned -and -not $Owned.HasExited) { Stop-Process -Id $Owned.Id -Force; $Owned.WaitForExit() }
    }
}
$OriginalAfter = if (Test-Path $OriginalKey) { (Get-ItemProperty $OriginalKey | ConvertTo-Json -Depth 8) } else { '<absent>' }
if ($OriginalAfter -ne $OriginalBefore) { throw 'Original Moonlight settings changed' }
$Uninstaller = Start-Process (Join-Path $InstallDir 'unins000.exe') -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -PassThru -Wait
if ($Uninstaller.ExitCode -ne 0) { throw 'Preview uninstaller failed' }
for ($attempt = 0; $attempt -lt 20 -and (Test-Path $Exe); $attempt++) { Start-Sleep -Seconds 1 }
if (Test-Path $Exe) { throw 'Preview executable remains after uninstall' }
# Do not upload generated credentials/certificates in diagnostic artifacts.
Remove-Item -Recurse -Force (Join-Path $LogDir 'host-config')
