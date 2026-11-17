param([string]$Port = 'COM6', [int]$Seconds = 30, [switch]$Reset)
$ErrorActionPreference = 'Stop'
$serial = [System.IO.Ports.SerialPort]::new($Port, 115200)
try {
    $serial.Open()
    if ($Reset) {
        $serial.DtrEnable = $false
        $serial.RtsEnable = $true
        Start-Sleep -Milliseconds 150
        $serial.RtsEnable = $false
    }
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $Seconds) {
        $received = $serial.ReadExisting()
        if ($received) { [Console]::Write($received) }
        Start-Sleep -Milliseconds 100
    }
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
