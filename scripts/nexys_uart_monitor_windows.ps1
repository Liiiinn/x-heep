param(
    [string]$Port = "COM11",
    [int]$Baud = 9600
)

$ErrorActionPreference = "Stop"

$serial = [System.IO.Ports.SerialPort]::new($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 200
$serial.NewLine = "`n"

try {
    $serial.Open()
    Write-Host "[nexys-uart] Listening on $Port at $Baud 8N1. Press Ctrl+C to stop."
    while ($true) {
        try {
            $text = $serial.ReadExisting()
            if ($text.Length -gt 0) {
                Write-Host -NoNewline $text
            }
        } catch [TimeoutException] {
        }
        Start-Sleep -Milliseconds 50
    }
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
}
