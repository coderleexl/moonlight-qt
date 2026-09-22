# Run in an x64 MSVC developer shell with Qt 6.11 MSVC binaries on PATH.
param([Parameter(Mandatory = $true)][string]$SunshinePayload)
$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path $PSScriptRoot -Parent
$SunshinePayload = (Resolve-Path $SunshinePayload).Path
$BuildRoot = Join-Path $RepoRoot 'build\preview-windows'
$Payload = Join-Path $RepoRoot 'build\preview-payload'
$Output = Join-Path $RepoRoot 'build\preview-installer'
function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE" }
}
foreach ($tool in @('qmake', 'nmake', 'windeployqt')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Missing tool: $tool" }
}
foreach ($file in @('sunshine.exe', 'assets\apps.json', 'assets\web\index.html')) {
    if (-not (Test-Path (Join-Path $SunshinePayload $file))) { throw "Sunshine payload missing $file" }
}
New-Item -ItemType Directory -Force $BuildRoot, $Output | Out-Null
# Only this script's deployment output is replaced. Build cache and source are retained.
if (Test-Path $Payload) { Remove-Item -Recurse -Force $Payload }
New-Item -ItemType Directory -Force $Payload | Out-Null
Push-Location $BuildRoot
try {
    Invoke-Checked 'qmake' @((Join-Path $RepoRoot 'moonlight-qt.pro'), 'CONFIG+=host_preview')
    Invoke-Checked 'nmake' @('release')
} finally { Pop-Location }
$Exe = Join-Path $BuildRoot 'app\release\Desk.exe'
Copy-Item $Exe $Payload
Copy-Item (Join-Path $RepoRoot 'libs\windows\lib\x64\*.dll') $Payload
Copy-Item (Join-Path $BuildRoot 'AntiHooking\release\AntiHooking.dll') $Payload
Copy-Item (Join-Path $RepoRoot 'app\SDL_GameControllerDB\gamecontrollerdb.txt') $Payload
Invoke-Checked 'windeployqt' @('--release', '--dir', $Payload, '--qmldir', (Join-Path $RepoRoot 'app\gui'),
    '--no-opengl-sw', '--no-compiler-runtime', '--no-sql', '--no-ffmpeg', (Join-Path $Payload 'Desk.exe'))
# App-local MSVC runtime avoids modifying the machine's shared runtime installation.
$Runtime = Get-ChildItem (Join-Path $env:VCToolsRedistDir 'x64\Microsoft.VC*.CRT') -Directory | Select-Object -First 1
if (-not $Runtime) { throw 'MSVC x64 runtime DLLs were not found' }
Copy-Item (Join-Path $Runtime.FullName '*.dll') $Payload
$HostDir = Join-Path $Payload 'host\sunshine'
New-Item -ItemType Directory -Force $HostDir | Out-Null
Copy-Item (Join-Path $SunshinePayload '*') $HostDir -Recurse
$LicenseDir = Join-Path $Payload 'licenses'
New-Item -ItemType Directory -Force $LicenseDir | Out-Null
Copy-Item (Join-Path $RepoRoot 'LICENSE') (Join-Path $LicenseDir 'Moonlight-LICENSE.txt')
Copy-Item (Join-Path $RepoRoot 'packaging\windows\README.txt') (Join-Path $Payload 'README.txt')
$Iscc = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
if ($Iscc) { $Compiler = $Iscc.Source }
else { $Compiler = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe' }
if (-not (Test-Path $Compiler)) { throw 'Install Inno Setup 6 before packaging' }
$Version = (Get-Content (Join-Path $RepoRoot 'app\version.txt') -Raw).Trim()
Invoke-Checked $Compiler @("/DPayloadDir=$Payload", "/DOutputDir=$Output", "/DAppVersion=$Version",
    (Join-Path $RepoRoot 'packaging\windows\preview.iss'))
Compress-Archive -Path "$Payload\*" -DestinationPath (Join-Path $Output 'Desk-x64.zip') -Force
Get-ChildItem $Output -File | Get-FileHash -Algorithm SHA256 | Format-Table -AutoSize
