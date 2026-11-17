param([Parameter(Mandatory)][string]$Address, [string]$Port = 'COM6')
$ErrorActionPreference = 'Stop'
$serial = [System.IO.Ports.SerialPort]::new($Port, 115200)
$pingClient = [System.Net.NetworkInformation.Ping]::new()

function Read-For([int]$Milliseconds) {
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $log = [System.Text.StringBuilder]::new()
    while ($timer.ElapsedMilliseconds -lt $Milliseconds) {
        [void]$log.Append($serial.ReadExisting())
        Start-Sleep -Milliseconds 10
    }
    $log.ToString()
}

function Send-Ping {
    $reply = $pingClient.Send($Address, 1000)
    if ($reply.Status -ne [System.Net.NetworkInformation.IPStatus]::Success) {
        throw "Ping failed: $($reply.Status)"
    }
}

function Assert-One-Movement([string]$Log) {
    Write-Host $Log
    if ($Log -match 'ERROR|assert failed|Rebooting|rst:0x' -or
        [regex]::Matches($Log, 'SERVO START #').Count -ne 1 -or
        [regex]::Matches($Log, 'SERVO RETURN').Count -ne 1 -or
        [regex]::Matches($Log, 'SERVO DONE #').Count -ne 1) {
        throw 'Expected exactly one completed movement; check firmware, IP, and serial port.'
    }
}

try {
    $parsedAddress = [System.Net.IPAddress]::Parse($Address)
    if ($parsedAddress.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) {
        throw 'Use the numeric IPv4 address printed by the ESP32.'
    }
    $serial.Open()
    # Let an existing movement or any reset caused by opening serial finish.
    [void](Read-For 5000)
    $idle = Read-For 1500
    if ($idle -match 'SERVO START|ERROR') { throw "Unexpected idle output: $idle" }

    Write-Host 'Checking one ping...'
    Send-Ping
    Assert-One-Movement (Read-For 2500)

    Write-Host 'Checking extra pings during movement (no queued movements)...'
    Send-Ping
    $burstLog = Read-For 100
    for ($i = 0; $i -lt 4; $i++) {
        Send-Ping
        $burstLog += Read-For 30
    }
    Assert-One-Movement ($burstLog + (Read-For 3000))

    Write-Host 'Checking that a later ping starts a new movement...'
    Send-Ping
    Assert-One-Movement (Read-For 2500)
    Write-Host 'PASS: ping replies and movement logs match; confirm physical motion visually.'
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
    $pingClient.Dispose()
}
