param(
    [string]$Vid = "VID_0403",
    [string]$UsbPid = "PID_6010"
)

$ErrorActionPreference = "Stop"

Write-Host "[nexys-driver] USB devices matching $Vid $UsbPid"
$devices = Get-PnpDevice -PresentOnly |
    Where-Object { $_.InstanceId -like "*$Vid*$UsbPid*" } |
    Select-Object Class, FriendlyName, InstanceId, Status

$devices | Format-Table -AutoSize

Write-Host ""
Write-Host "[nexys-driver] Serial ports"
$devices |
    Where-Object { $_.Class -eq "Ports" -or $_.FriendlyName -like "*COM*" } |
    Format-Table -AutoSize

Write-Host ""
Write-Host "[nexys-driver] Expected setup for PC + Nexys4 only:"
Write-Host "  - Do not use usbipd-win; keep Nexys USB devices on Windows"
Write-Host "  - UART must remain visible as a USB Serial Port, currently expected as COM11"
Write-Host "  - If COM11 disappears after Zadig, restore the UART interface driver"
Write-Host "  - OpenOCD needs the JTAG interface on WinUSB/libusbK, not the UART COM port"
