param(
    [switch]$NoProgram,
    [switch]$CheckOnly,
    [string]$Bitstream,
    [int]$GdbPort = 3333,
    [string]$ToolDir = "D:\UNI2\FINAL"
)

$ErrorActionPreference = "Stop"

$monitor = Join-Path $ToolDir "monitor.py"
$openocd = Join-Path $ToolDir "xpack-openocd-0.12.0-7\bin\openocd.exe"
$cfg = Join-Path $ToolDir "cw305-rbb.cfg"

if (-not (Test-Path -LiteralPath $monitor)) {
    throw "monitor.py not found: $monitor"
}

if (-not (Test-Path -LiteralPath $openocd)) {
    throw "openocd.exe not found: $openocd"
}

if (-not (Test-Path -LiteralPath $cfg)) {
    throw "OpenOCD config not found: $cfg"
}

if ($CheckOnly) {
    $listener = Get-NetTCPConnection -LocalPort $GdbPort -ErrorAction SilentlyContinue |
        Where-Object { $_.State -eq "Listen" }

    if ($listener) {
        Write-Host "[cw305] GDB port $GdbPort is listening."
        $listener | Select-Object LocalAddress, LocalPort, State, OwningProcess
        exit 0
    }

    Write-Warning "GDB port $GdbPort is not listening. Start monitor.py first and check Windows firewall if it remains closed."
    exit 1
}

$argsList = @($monitor)
if ($NoProgram) {
    $argsList += "--no-program"
}
if ($Bitstream) {
    $argsList += @("--bsfile", $Bitstream)
}

Write-Host "[cw305] Starting Windows monitor from $ToolDir"
Write-Host "[cw305] Command: python $($argsList -join ' ')"
Write-Host "[cw305] Keep this PowerShell window open while GDB is connected."
Write-Host "[cw305] In another PowerShell window, check with:"
Write-Host "[cw305]   powershell -ExecutionPolicy Bypass -File scripts\cw305_windows_debug.ps1 -CheckOnly"
Write-Host ""

Push-Location $ToolDir
try {
    & python @argsList
}
finally {
    Pop-Location
}
