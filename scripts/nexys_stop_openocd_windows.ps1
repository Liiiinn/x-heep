param(
    [int[]]$Ports = @(13333, 4444, 6666),
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$owners = @()
foreach ($port in $Ports) {
    $listeners = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue
    foreach ($listener in $listeners) {
        if ($listener.OwningProcess -ne 0) {
            $owners += $listener.OwningProcess
        }
    }
}

$openocd = Get-Process openocd -ErrorAction SilentlyContinue
foreach ($proc in $openocd) {
    $owners += $proc.Id
}

$owners = $owners | Sort-Object -Unique
if (-not $owners) {
    Write-Host "[nexys-openocd-stop] no stale OpenOCD processes found"
    exit 0
}

foreach ($owner in $owners) {
    $proc = Get-Process -Id $owner -ErrorAction SilentlyContinue
    if (-not $proc) {
        continue
    }

    Write-Host "[nexys-openocd-stop] stale PID $owner ($($proc.ProcessName))"
    if (-not $DryRun) {
        Stop-Process -Id $owner -Force
    }
}
