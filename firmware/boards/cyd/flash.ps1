param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^COM[0-9]+$')]
    [string]$Port,
    [switch]$Verify,
    [string]$EsptoolPath = ''
)

$ErrorActionPreference = 'Stop'
$taskRepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$taskSketch = Join-Path $PSScriptRoot 'MotoNav\MotoNav.ino'
$taskVersionMatch = [regex]::Match([IO.File]::ReadAllText($taskSketch), '^#define FW_VERSION "v([^"]+)"', [Text.RegularExpressions.RegexOptions]::Multiline)
if (-not $taskVersionMatch.Success) { throw 'Cannot read the CYD firmware version.' }

$taskVersion = $taskVersionMatch.Groups[1].Value
$taskName = "MotoNav_CYD_CYD-2.8_v${taskVersion}_panel0"
$taskDist = Join-Path $taskRepoRoot 'firmware\dist\cyd\panel0'
$taskManifestPath = Join-Path $taskDist "${taskName}_manifest.json"
$taskManifest = Get-Content -LiteralPath $taskManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($taskManifest.board -ne 'ESP32-2432S028R ST7789' -or $taskManifest.flashSize -ne '4MB' -or $taskManifest.verifiedApplicationOffset -ne '0x10000') {
    throw 'The CYD manifest does not match the expected 4MB ESP32 image layout.'
}

$taskEntry = $taskManifest.files | Where-Object file -eq "${taskName}_app_0x10000.bin"
if (-not $taskEntry) { throw 'The application image is missing from the CYD manifest.' }
$taskImage = Join-Path $taskDist $taskEntry.file
if (-not (Test-Path -LiteralPath $taskImage -PathType Leaf)) { throw 'The CYD application image is missing.' }
$taskActual = Get-FileHash -LiteralPath $taskImage -Algorithm SHA256
if ((Get-Item -LiteralPath $taskImage).Length -ne $taskEntry.bytes -or $taskActual.Hash.ToLowerInvariant() -ne $taskEntry.sha256) {
    throw 'The CYD application image does not match its manifest.'
}

if (-not $EsptoolPath) {
    $EsptoolPath = Join-Path $taskRepoRoot '.tools\arduino15\packages\esp32\tools\esptool_py\5.3.0\esptool.exe'
}
if (-not (Test-Path -LiteralPath $EsptoolPath -PathType Leaf)) { throw 'esptool.exe not found; pass -EsptoolPath.' }
$taskPorts = [IO.Ports.SerialPort]::GetPortNames()
if ($taskPorts -notcontains $Port.ToUpperInvariant()) { throw "${Port} is not an available serial port." }

$taskCommon = @('--chip', 'esp32', '--port', $Port.ToUpperInvariant(), '--baud', '115200', '--before', 'default-reset', '--after', 'hard-reset')
if ($Verify) {
    & $EsptoolPath @taskCommon verify-flash 0x10000 $taskImage
} else {
    Write-Host "Flashing CYD firmware $taskVersion to $($Port.ToUpperInvariant()) at 0x10000. Existing settings are preserved."
    & $EsptoolPath @taskCommon write-flash 0x10000 $taskImage
}
if ($LASTEXITCODE -ne 0) { throw "esptool failed with exit code $LASTEXITCODE." }
