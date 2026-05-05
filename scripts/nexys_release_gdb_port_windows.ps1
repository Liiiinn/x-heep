param(
    [int]$Port = 13333,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$connections = Get-NetTCPConnection -RemotePort $Port -State Established -ErrorAction SilentlyContinue
if (-not $connections) {
    Write-Host "[nexys-gdb-release] no stale GDB bridge clients on remote port $Port"
    exit 0
}

$owners = $connections |
    Where-Object { $_.OwningProcess -ne 0 -and $_.OwningProcess -ne $PID } |
    Select-Object -ExpandProperty OwningProcess -Unique

foreach ($owner in $owners) {
    $proc = Get-Process -Id $owner -ErrorAction SilentlyContinue
    if (-not $proc) {
        continue
    }

    Write-Host "[nexys-gdb-release] stale client PID $owner ($($proc.ProcessName)) -> $Port"
    if (-not $DryRun) {
        Stop-Process -Id $owner -Force
    }
}
