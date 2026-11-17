param([string]$Port = 'COM6', [switch]$Stop)
$ErrorActionPreference = 'Stop'
$serial = [System.IO.Ports.SerialPort]::new($Port, 115200)
try {
    $serial.Open()
    # Some USB adapters reset the board on open. Wait without starting it.
    Start-Sleep -Seconds 2
    $serial.DiscardInBuffer()
    $serial.Write($(if ($Stop) { 's' } else { 'r' }))
    $log = ''
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    while ($timer.ElapsedMilliseconds -lt 3500) {
        $chunk = $serial.ReadExisting()
        if ($chunk) { [Console]::Write($chunk); $log += $chunk }
        Start-Sleep -Milliseconds 20
    }
    if ($log -match 'ERROR|assert failed|Rebooting|rst:0x') {
        throw 'Firmware error or unexpected reset.'
    }
    if ($Stop) {
        if ($log -notmatch 'MOTOR STOP STBY=0') { throw 'No stop confirmation.' }
    } elseif (
        [regex]::Matches($log, 'MOTOR START').Count -ne 1 -or
        [regex]::Matches($log, 'MOTOR LOW').Count -ne 1 -or
        [regex]::Matches($log, 'MOTOR HIGH').Count -ne 1 -or
        [regex]::Matches($log, 'MOTOR DONE STBY=0').Count -ne 1 -or
        $log -notmatch '(?s)MOTOR START.*MOTOR LOW.*MOTOR HIGH.*MOTOR DONE STBY=0') {
        throw 'Expected one completed LOW -> HIGH -> STOP sequence.'
    }
    Write-Host 'PASS: motor command confirmed by serial log.'
} finally {
    if ($serial.IsOpen) {
        # Also request stop if a timeout or error interrupted the check.
        try { $serial.Write('s') } finally { $serial.Close() }
    }
    $serial.Dispose()
}
