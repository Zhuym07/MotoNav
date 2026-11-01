<#
.SYNOPSIS
Monitor a Box0 probe and save its complete serial stream for frame decoding.
.DESCRIPTION
Uses one serial session at 115200 baud with DTR/RTS disabled. After two seconds,
sends STATUS and CAPTURE for the current phone-driven screen by default.
All received text, including every [ROW], goes to the UTF-8 log; pixel rows are
hidden from the console. The parent can decode the logged RGB565 frames.
Captures never overlap. Seconds is the monitoring duration; a pending capture
may finish afterwards, with a 60-second timeout measured from its request.
Scenes are manual previews and may change navigation fields. Omit Scene for
live phone testing. CaptureCount 0 or -Capture:$false selects monitoring only.
.EXAMPLE
.\firmware\tests\capture_box0.ps1 -Seconds 90 -CaptureCount 3 -IntervalSeconds 30
.EXAMPLE
.\firmware\tests\capture_box0.ps1 -Port COM10 -Output .\nav.log -Capture:$false
#>
[CmdletBinding()]
param(
    [ValidateNotNullOrEmpty()]
    [string]$Port = 'COM10',

    [ValidateRange(1, 86400)]
    [int]$Seconds = 60,

    [ValidateNotNullOrEmpty()]
    [string]$Output = (Join-Path $PSScriptRoot ("..\.build\box0-probe\capture-{0}.log" -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))),

    [ValidateSet('HOME', 'MUSIC', 'WEATHER', 'PAIR', 'NAV', 'MAP')]
    [string]$Scene,

    [switch]$Capture = $true,

    [ValidateRange(0, 10000)]
    [int]$CaptureCount = 1,

    [ValidateRange(1, 86400)]
    [int]$IntervalSeconds = 30
)

$ErrorActionPreference = 'Stop'
$logPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
$null = [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($logPath))
$serial = $null
$log = $null
$requested = 0
$completed = 0
$inFlight = $false
$captureDeadline = 0.0
$nextCaptureAt = 2.0
$statusSent = $false
$mapPrepared = $false
$pending = ''

try {
    $log = [System.IO.StreamWriter]::new($logPath, $false, [System.Text.UTF8Encoding]::new($false))
    $log.AutoFlush = $true
    $serial = [System.IO.Ports.SerialPort]::new($Port, 115200)
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    $serial.Handshake = [System.IO.Ports.Handshake]::None
    $serial.Encoding = [System.Text.Encoding]::UTF8
    $serial.ReadBufferSize = 262144
    $serial.WriteTimeout = 1000
    $serial.Open()
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    Write-Host "[HOST] Monitoring $Port at 115200; log: $logPath"

    while ($true) {
        $chunk = $serial.ReadExisting()
        if ($chunk.Length -gt 0) {
            # Persist chunks immediately, including partial lines at shutdown.
            $log.Write($chunk)
            $pending += $chunk
            $newline = $pending.IndexOf("`n")
            while ($newline -ge 0) {
                $line = $pending.Substring(0, $newline).TrimEnd([char]13)
                $pending = $pending.Substring($newline + 1)
                if ($line.Length -gt 0 -and -not $line.StartsWith('[ROW] ')) {
                    Write-Host $line
                }
                if ($line -eq '[FRAME_END]') {
                    $completed++
                    $inFlight = $false
                    Write-Host "[HOST] Frame $completed saved in $logPath"
                }
                $newline = $pending.IndexOf("`n")
            }
        }

        $elapsed = $timer.Elapsed.TotalSeconds
        if ($inFlight -and $elapsed -ge $captureDeadline) {
            Write-Warning "Capture timed out; partial serial data is saved in $logPath"
            break
        }
        if ($elapsed -ge $Seconds -and -not $inFlight) { break }

        if ($elapsed -ge 2 -and -not $statusSent -and (-not $Capture -or $CaptureCount -eq 0)) {
            $serial.Write("STATUS`n")
            $statusSent = $true
        }
        if ($Capture -and $requested -lt $CaptureCount -and -not $inFlight -and
            $elapsed -lt $Seconds -and $elapsed -ge $nextCaptureAt) {
            if ($Scene -eq 'MAP' -and -not $mapPrepared) {
                # MAP only marks navigation dirty; allow a normal render first.
                $serial.Write("MAP`n")
                $mapPrepared = $true
                $nextCaptureAt = $elapsed + 0.1
            } else {
                $commands = "STATUS`n"
                if ($Scene -and $Scene -ne 'MAP') { $commands += "$Scene`n" }
                $commands += "CAPTURE`n"
                $serial.Write($commands)
                $requested++
                $inFlight = $true
                $captureDeadline = $elapsed + 60
                $nextCaptureAt = $elapsed + $IntervalSeconds
                Write-Host "[HOST] Requested capture $requested/$CaptureCount"
            }
        }
        Start-Sleep -Milliseconds 20
    }
} finally {
    try {
        if ($null -ne $serial) {
            try {
                if ($serial.IsOpen) { $serial.Close() }
            } finally {
                $serial.Dispose()
            }
        }
    } finally {
        if ($null -ne $log) { $log.Dispose() }
    }
}

if ($Capture -and $requested -lt $CaptureCount) {
    Write-Warning "Monitoring ended after requesting $requested/$CaptureCount captures; increase Seconds for more."
}
Write-Host "[HOST] Done: $completed completed frame(s), $requested requested; log: $logPath"
