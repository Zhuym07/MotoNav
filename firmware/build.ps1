param([ValidateSet('175','185b','box0','cyd')][string]$Board='175',
      [ValidateSet(0,1,2)][int]$PanelVariant=0,
      [string]$CliPath='arduino-cli',[string]$ConfigFile='',
      [ValidateRange(1,32)][int]$Jobs=4)
$ErrorActionPreference='Stop'
if($Board -eq '175' -and $PanelVariant -ne 0){throw '1.75 has only panel 0.'}
if($Board -eq 'box0' -and $PanelVariant -ne 0){throw 'BOX0 has only panel 0.'}
if($Board -eq 'cyd' -and $PanelVariant -ne 0){throw 'CYD has only panel 0.'}
$taskRoot=$PSScriptRoot
$taskBuild=Join-Path $taskRoot ".build/$Board"
$taskSketch=Join-Path $taskBuild 'MotoNav'
$taskOutput=Join-Path $taskBuild 'output'
New-Item -ItemType Directory -Force $taskSketch,$taskOutput | Out-Null
Get-ChildItem -LiteralPath (Join-Path $taskRoot "boards/$Board/MotoNav") -File | Where-Object Extension -in '.ino','.h' | Copy-Item -Destination $taskSketch -Force
if($Board -eq '185b'){
    $taskPin=Join-Path $taskSketch 'pin_config.h'
    $taskText=[IO.File]::ReadAllText($taskPin)
    $taskText=[regex]::Replace($taskText,'#define LCD_INIT_VARIANT [0-9]+',"#define LCD_INIT_VARIANT $PanelVariant")
    [IO.File]::WriteAllText($taskPin,$taskText,[Text.UTF8Encoding]::new($false))
}
$taskFqbn='esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,CDCOnBoot=cdc'
if($Board -eq 'cyd'){$taskFqbn='esp32:esp32:esp32'}
$taskArgs=@()
if($ConfigFile){$taskArgs+=@('--config-file',$ConfigFile)}
$taskHomePrefix=[Environment]::GetFolderPath('UserProfile').Replace('\','/')
$taskPrefixMap="-ffile-prefix-map=$taskHomePrefix=/builder"
$taskArgs+=@('compile','--jobs',"$Jobs",'--libraries',(Join-Path $taskRoot 'libraries'),
    '--build-path',(Join-Path $taskBuild 'build'),
    '--fqbn',$taskFqbn,
    '--build-property',"compiler.c.extra_flags=$taskPrefixMap",
    '--build-property',"compiler.cpp.extra_flags=$taskPrefixMap",
    '--output-dir',$taskOutput,$taskSketch)
& $CliPath @taskArgs
if($LASTEXITCODE -ne 0){throw "Arduino build failed ($LASTEXITCODE)"}
$taskDest=Join-Path $taskRoot "dist/$Board/panel$PanelVariant"
New-Item -ItemType Directory -Force $taskDest | Out-Null
$taskLabel=if($Board -eq '175'){'1.75'}elseif($Board -eq 'box0'){'BOX0'}elseif($Board -eq 'cyd'){'CYD-2.8'}else{'1.85B'}
$taskVersion=[regex]::Match([IO.File]::ReadAllText((Join-Path $taskSketch 'MotoNav.ino')),'^#define FW_VERSION "v([^\"]+)"',[Text.RegularExpressions.RegexOptions]::Multiline).Groups[1].Value
if(!$taskVersion){throw 'Cannot read firmware version'}
$taskPrefix=if($Board -eq 'cyd'){'MotoNav_CYD'}else{'MotoNav_universal'}
$taskName="${taskPrefix}_${taskLabel}_v${taskVersion}_panel$PanelVariant"
$taskApp=[IO.File]::ReadAllBytes((Join-Path $taskOutput 'MotoNav.ino.bin'))
$taskMerged=[IO.File]::ReadAllBytes((Join-Path $taskOutput 'MotoNav.ino.merged.bin'))
$taskLength=0x10000+$taskApp.Length
if($taskMerged.Length -lt $taskLength){throw 'Merged image is shorter than the application image.'}
$taskSegment=[byte[]]::new($taskApp.Length)
[Array]::Copy($taskMerged,0x10000,$taskSegment,0,$taskApp.Length)
$taskHasher=[Security.Cryptography.SHA256]::Create()
try {
    if([Convert]::ToBase64String($taskHasher.ComputeHash($taskSegment)) -ne [Convert]::ToBase64String($taskHasher.ComputeHash($taskApp))){throw 'Unexpected application offset in merged image.'}
} finally {$taskHasher.Dispose()}
# Core 3.3.10 exports a 16 MiB merged image. Drop only padding after the app;
# keep every byte of bootloader, partition table, boot data and the app itself.
$taskFull=[byte[]]::new($taskLength)
[Array]::Copy($taskMerged,0,$taskFull,0,$taskLength)
[IO.File]::WriteAllBytes((Join-Path $taskDest "${taskName}_app_0x10000.bin"),$taskApp)
[IO.File]::WriteAllBytes((Join-Path $taskDest "${taskName}_full_0x0.bin"),$taskFull)
# Integrity manifest consumed by the Mac flashing entry point; UTF-8 without BOM.
if($Board -eq '175' -or $Board -eq 'box0' -or $Board -eq 'cyd') {
    $taskManifestFiles=@()
    foreach($taskSuffix in @('app_0x10000','full_0x0')) {
        $taskFileName="${taskName}_${taskSuffix}.bin"
        $taskFilePath=Join-Path $taskDest $taskFileName
        $taskManifestFiles+=@{file=$taskFileName;bytes=(Get-Item -LiteralPath $taskFilePath).Length;sha256=(Get-FileHash -LiteralPath $taskFilePath -Algorithm SHA256).Hash.ToLowerInvariant()}
    }
    $taskPlatform=if($Board -eq '175'){'universal'}elseif($Board -eq 'box0'){'box0'}else{'cyd'}
    $taskBoard=if($Board -eq '175'){'Waveshare ESP32-S3-Touch-AMOLED-1.75'}elseif($Board -eq 'box0'){'ALIENTEK ESP32-S3 BOX0'}else{'ESP32-2432S028R ST7789'}
    $taskFlash=if($Board -eq 'cyd'){'4MB'}else{'16MB'}
    $taskPartition=if($Board -eq 'cyd'){'default'}else{'app3M_fat9M_16MB'}
    $taskManifest=@{firmware=$taskVersion;platform=$taskPlatform;board=$taskBoard;esp32Core='3.3.10';flashSize=$taskFlash;partition=$taskPartition;verifiedApplicationOffset='0x10000';deviceTested=$false;files=$taskManifestFiles}
    [IO.File]::WriteAllText((Join-Path $taskDest "${taskName}_manifest.json"),($taskManifest | ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
}
Get-ChildItem -LiteralPath $taskDest -File | Get-FileHash -Algorithm SHA256 | Format-Table -AutoSize
