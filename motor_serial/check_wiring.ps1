param([string]$Diagram = (Join-Path $PSScriptRoot '../아틀란티스_모터전용_배선도.svg'))
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
[xml]$drawing = Get-Content -LiteralPath $Diagram -Raw
[xml]$original = Get-Content -LiteralPath (Join-Path $projectRoot '아틀란티스_보트_점퍼선배선도.svg') -Raw
$expectedIds = @('W01','W02','W05','W06','W10','W11','W12','W14','W15','W16','W17')
$wires = @($drawing.SelectNodes('//*[local-name()="g" and @data-from]'))
$wireIds = @($wires | Where-Object { $_.GetAttribute('id') -ne 'C01' } | ForEach-Object { $_.GetAttribute('id') })
if (@(Compare-Object $expectedIds $wireIds).Count -ne 0) {
    throw 'Expected exactly the 11 motor-only wire IDs from the original drawing.'
}
$allIds = @($drawing.SelectNodes('//*[@id]') | ForEach-Object { $_.GetAttribute('id') })
if (@($allIds | Select-Object -Unique).Count -ne $allIds.Count) { throw 'Duplicate SVG IDs.' }

$graph = @{}
function Connect([string]$From, [string]$To) {
    if (!$graph.ContainsKey($From)) { $graph[$From] = [System.Collections.Generic.List[string]]::new() }
    if (!$graph.ContainsKey($To)) { $graph[$To] = [System.Collections.Generic.List[string]]::new() }
    $graph[$From].Add($To)
    $graph[$To].Add($From)
}

foreach ($wire in $wires) {
    $wireId = $wire.GetAttribute('id')
    if ($wireId -eq 'C01') {
        if ($wire.GetAttribute('data-from') -ne 'PC.USB' -or $wire.GetAttribute('data-to') -ne 'ESP.USB') {
            throw 'The USB test diagram must power ESP32 through PC USB.'
        }
        continue
    }
    $reference = $original.SelectSingleNode("//*[@id='$wireId']")
    foreach ($attribute in @('data-from','data-to','data-net')) {
        if ($wire.GetAttribute($attribute) -ne $reference.GetAttribute($attribute)) {
            throw "$wireId $attribute differs from original wiring."
        }
    }
    Connect $wire.GetAttribute('data-from') $wire.GetAttribute('data-to')
}
if (@($wires | Where-Object { $_.GetAttribute('id') -eq 'C01' }).Count -ne 1) { throw 'Missing USB cable.' }

# Electrical assumptions are shown on the SVG: one continuous GND rail and
# common GND terminals on the power module. Physical continuity needs a meter.
foreach ($point in @('RAIL.GND.1','RAIL.GND.15','RAIL.GND.16')) { Connect 'RAIL.GND.0' $point }
Connect 'POWER.5V_GND_1' 'POWER.5V_GND_3'
$visited = [System.Collections.Generic.HashSet[string]]::new()
$queue = [System.Collections.Generic.Queue[string]]::new()
$queue.Enqueue('ESP.GND')
while ($queue.Count -gt 0) {
    $point = $queue.Dequeue()
    if (!$visited.Add($point)) { continue }
    foreach ($neighbor in $graph[$point]) { $queue.Enqueue($neighbor) }
}
foreach ($point in @('DRV.GND','DRV.BIN1','DRV.BIN2','POWER.5V_GND_1','POWER.5V_GND_3')) {
    if (!$visited.Contains($point)) { throw "Missing common GND connection: $point" }
}
foreach ($point in @('DRV.VM','POWER.5V_1','DRV.AIN1','DRV.AIN2','DRV.STBY','DRV.AO1','DRV.AO2','MOTOR.1','MOTOR.2')) {
    if ($visited.Contains($point)) { throw "Unexpected short to GND: $point" }
}

foreach ($sketch in @('motor_serial','motor_ping')) {
    $source = Get-Content -LiteralPath (Join-Path $projectRoot "$sketch/$sketch.ino") -Raw
    foreach ($mapping in @(@('STBY',33), @('AIN1',25), @('AIN2',26))) {
        $pinName = $mapping[0]; $pinNumber = $mapping[1]
        if ($source -notmatch "constexpr uint8_t $($pinName)_PIN = $pinNumber;") {
            throw "$sketch GPIO mismatch: $pinName"
        }
    }
    if ($source -notmatch 'ledcWrite\(AIN2_PIN, 0\)' -or
        $source -notmatch 'ledcWrite\(AIN1_PIN, duty\)' -or
        $source -notmatch 'digitalWrite\(STBY_PIN, LOW\)') {
        throw "$sketch no longer matches the forward PWM and disable logic shown."
    }
}
Write-Output 'PASS: SVG XML and IDs; 11 wires match original drawing; USB cable present.'
Write-Output 'PASS: ESP32, power module, driver and unused BIN inputs share GND; no control or power wire is grounded.'
Write-Output 'PASS: GPIO33/STBY, GPIO25/AIN1, GPIO26/AIN2 and forward PWM logic match both sketches.'
Write-Output 'This checks diagram connectivity only. Verify physical pins, continuity, motor current and supply voltage separately.'
