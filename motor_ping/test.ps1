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

function Assert-One-Run([string]$Log) {
    Write-Host $Log
    if ($Log -match 'ERROR|ABORT|assert failed|Rebooting|rst:0x' -or
        [regex]::Matches($Log, 'MOTOR START #').Count -ne 1 -or
        [regex]::Matches($Log, 'MOTOR LOW duty=77/255').Count -ne 1 -or
        [regex]::Matches($Log, 'MOTOR HIGH duty=153/255').Count -ne 1 -or
        [regex]::Matches($Log, 'MOTOR DONE #\d+ STBY=0').Count -ne 1 -or
        $Log -notmatch '(?s)MOTOR START #.*MOTOR LOW.*MOTOR HIGH.*MOTOR DONE #') {
        throw 'Expected one LOW -> HIGH -> DONE sequence without reboot or abort.'
    }
}

try {
    $parsedAddress = [System.Net.IPAddress]::Parse($Address)
    if ($parsedAddress.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) {
        throw 'Use the numeric IPv4 address printed by the ESP32.'
    }
    $serial.Open()
    [void](Read-For 5000)
    $idle = Read-For 2500
    if ($idle -match 'MOTOR START|MOTOR LOW|MOTOR HIGH|ERROR|ABORT|assert failed|rst:0x') {
        throw "Unexpected idle output: $idle"
    }

    Write-Host 'Checking one ping...'
    Send-Ping
    Assert-One-Run (Read-For 3500)

    Write-Host 'Checking extra pings during the sequence (no queued runs)...'
    $burstTimer = [System.Diagnostics.Stopwatch]::StartNew()
    Send-Ping
    $burstLog = Read-For 100
    for ($i = 0; $i -lt 4; $i++) {
        Send-Ping
        $burstLog += Read-For 30
    }
    # Slow networks could deliver a burst after the 2-second run has ended.
    # In that case do not mistake a legitimately new run for a firmware bug.
    if ($burstTimer.ElapsedMilliseconds -gt 1000) {
        throw 'Burst took over 1 second; duplicate-ping check is inconclusive on this network.'
    }
    Assert-One-Run ($burstLog + (Read-For 4500))

    Write-Host 'Checking a new run after completion...'
    Send-Ping
    Assert-One-Run (Read-For 3500)
    Write-Host 'PASS: replies and motor command logs match; confirm physical rotation visually.'
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
    $pingClient.Dispose()
}
