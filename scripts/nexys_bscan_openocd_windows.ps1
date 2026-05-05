param(
    [string]$OpenOcd = "D:\UNI2\FINAL\xpack-openocd-0.12.0-7\bin\openocd.exe",
    [string]$Config = "D:\UNI2\FINAL\nexys4-bscan.cfg",
    [int]$Channel = 0,
    [int]$Port = 13333,
    [string]$BindAddress = "127.0.0.1",
    [switch]$KillExisting
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $OpenOcd)) {
    throw "OpenOCD not found: $OpenOcd"
}

if (-not (Test-Path -LiteralPath $Config)) {
    throw "OpenOCD config not found: $Config"
}

Write-Host "[nexys-bscan] OpenOCD: $OpenOcd"
Write-Host "[nexys-bscan] Config : $Config"
Write-Host "[nexys-bscan] FTDI channel: $Channel"
Write-Host "[nexys-bscan] GDB port: $Port"
Write-Host "[nexys-bscan] Bind address: $BindAddress"
Write-Host "[nexys-bscan] If OpenOCD cannot open the FTDI device, use Zadig on Interface A only."

$stopScript = Join-Path $PSScriptRoot "nexys_stop_openocd_windows.ps1"
if ($KillExisting) {
    if (Test-Path -LiteralPath $stopScript) {
        & $stopScript -Ports @($Port, 4444, 6666)
    } else {
        Get-Process openocd -ErrorAction SilentlyContinue | Stop-Process -Force
    }
}

$listener = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
if ($listener) {
    $owners = $listener | Select-Object -ExpandProperty OwningProcess -Unique
    foreach ($owner in $owners) {
        $proc = Get-Process -Id $owner -ErrorAction SilentlyContinue
        if ($proc) {
            Write-Warning "Port $Port is already used by PID $owner ($($proc.ProcessName)). Re-run with -KillExisting, stop it manually, or use -Port with another value."
        } else {
            Write-Warning "Port $Port is already used by PID $owner. Re-run with -KillExisting, stop it manually, or use -Port with another value."
        }
    }
    exit 1
}

& $OpenOcd `
    -c "set NEXYS_FTDI_CHANNEL $Channel" `
    -c "set NEXYS_BIND_ADDR $BindAddress" `
    -c "set NEXYS_GDB_PORT $Port" `
    -c "gdb port $Port" `
    -f $Config

exit $LASTEXITCODE
