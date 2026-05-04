param(
    [string]$OpenOcdExe = "D:\UNI2\FINAL\xpack-openocd-0.12.0-7\bin\openocd.exe",
    [int]$GdbPort = 3333,
    [string]$RuleName = "CW305 OpenOCD GDB 3333"
)

$ErrorActionPreference = "Stop"

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    throw "Run this script from an elevated PowerShell window."
}

if (-not (Test-Path -LiteralPath $OpenOcdExe)) {
    throw "openocd.exe not found: $OpenOcdExe"
}

$existing = Get-NetFirewallRule -DisplayName $RuleName -ErrorAction SilentlyContinue
if ($existing) {
    Set-NetFirewallRule -DisplayName $RuleName -Enabled True -Action Allow -Direction Inbound -Profile Any
}
else {
    New-NetFirewallRule `
        -DisplayName $RuleName `
        -Direction Inbound `
        -Program $OpenOcdExe `
        -Action Allow `
        -Protocol TCP `
        -LocalPort $GdbPort `
        -Profile Any | Out-Null
}

Get-NetFirewallRule -DisplayName $RuleName |
    Select-Object DisplayName, Enabled, Direction, Action, Profile
