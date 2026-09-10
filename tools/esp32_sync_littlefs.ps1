param(
    [string]$ProjectDir = "src/firmwareEsp32",
    [string]$PioPath = "pio",
    [string]$Environment = "wemos_d1_mini32",
    [switch]$SkipFlash
)

Set-StrictMode -Version 3
$ErrorActionPreference = "Stop"

function Get-IniValue {
    param(
        [string]$Path,
        [string]$Key
    )

    foreach ($line in Get-Content -Path $Path) {
        if ($line -match "^\s*${Key}\s*=\s*(.+?)\s*$") {
            return $Matches[1].Trim()
        }
    }

    return $null
}

function Get-SdkconfigValue {
    param(
        [string]$Path,
        [string]$Key
    )

    foreach ($line in Get-Content -Path $Path) {
        if ($line -match "^${Key}=(.+)$") {
            return $Matches[1].Trim()
        }
    }

    return $null
}

function Get-PartitionInfo {
    param(
        [string]$Path,
        [string]$PartitionName
    )

    foreach ($rawLine in Get-Content -Path $Path) {
        $line = $rawLine.Trim()
        if (-not $line -or $line.StartsWith("#")) {
            continue
        }

        $fields = $line.Split(",") | ForEach-Object { $_.Trim() }
        if ($fields.Count -lt 5) {
            continue
        }

        if ($fields[0] -eq $PartitionName) {
            return [pscustomobject]@{
                Offset = $fields[3]
                Size = $fields[4]
            }
        }
    }

    throw "Partition '$PartitionName' not found in $Path"
}

function Get-LatestTimestampUtc {
    param([System.IO.FileInfo[]]$Files)

    if (-not $Files -or $Files.Count -eq 0) {
        throw "No source files found to evaluate web asset freshness."
    }

    return ($Files | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1).LastWriteTimeUtc
}

$projectPath = (Resolve-Path -Path $ProjectDir).Path
$platformioIni = Join-Path $projectPath "platformio.ini"
$partitionsCsv = Join-Path $projectPath "partitions.csv"
$sdkconfig = Join-Path $projectPath "sdkconfig.$Environment"
$webDir = Join-Path $projectPath "www"
$buildDir = Join-Path $projectPath ".pio/build/$Environment"
$webImageDir = Join-Path $buildDir "www_image"
$imagePath = Join-Path $buildDir "www.bin"
$flashStampPath = Join-Path $buildDir "www.flashed.stamp"
$mklittlefs = Join-Path $env:USERPROFILE ".platformio\packages\tool-mklittlefs\mklittlefs.exe"
$esptool = Join-Path $env:USERPROFILE ".platformio\packages\tool-esptoolpy\esptool.py"
$platformioPython = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\python.exe"
$npmCommand = (Get-Command npm.cmd -ErrorAction SilentlyContinue).Source

if (-not (Test-Path -Path $platformioIni)) {
    throw "Missing PlatformIO project: $platformioIni"
}

if (-not (Test-Path -Path $buildDir)) {
    throw "Missing build directory: $buildDir. Run the ESP32 build first."
}

if (-not (Test-Path -Path $sdkconfig)) {
    throw "Missing sdkconfig: $sdkconfig"
}

if (-not (Test-Path -Path $mklittlefs)) {
    throw "Missing mklittlefs tool: $mklittlefs"
}

if (-not (Test-Path -Path $esptool)) {
    throw "Missing esptool.py: $esptool"
}

if (-not (Test-Path -Path $platformioPython)) {
    throw "Missing PlatformIO Python: $platformioPython"
}

if (-not $npmCommand) {
    throw "npm.cmd was not found on PATH."
}

$partition = Get-PartitionInfo -Path $partitionsCsv -PartitionName "www"
$pageSize = [int](Get-SdkconfigValue -Path $sdkconfig -Key "CONFIG_LITTLEFS_PAGE_SIZE")
if ($pageSize -le 0) {
    throw "Invalid CONFIG_LITTLEFS_PAGE_SIZE in $sdkconfig"
}

$uploadPort = Get-IniValue -Path $platformioIni -Key "upload_port"
$uploadSpeed = Get-IniValue -Path $platformioIni -Key "upload_speed"
if (-not $uploadPort) {
    throw "Missing upload_port in $platformioIni"
}
if (-not $uploadSpeed) {
    $uploadSpeed = "921600"
}

$sourceFiles = Get-ChildItem -Path $webDir -Recurse -File | Where-Object {
    $_.FullName -notmatch "[\\/]node_modules[\\/]"
}
$latestSourceUtc = Get-LatestTimestampUtc -Files $sourceFiles
$needsRebuild = -not (Test-Path -Path $imagePath)

if (-not $needsRebuild) {
    $imageUtc = (Get-Item -Path $imagePath).LastWriteTimeUtc
    $needsRebuild = $latestSourceUtc -gt $imageUtc
}

if ($needsRebuild) {
    if (-not (Test-Path -Path (Join-Path $webDir "node_modules"))) {
        Write-Host "[ESP32][LittleFS] Installing web dependencies"
        Push-Location $webDir
        try {
            & $npmCommand install
        }
        finally {
            Pop-Location
        }
    }

    Write-Host "[ESP32][LittleFS] Rebuilding web assets"
    New-Item -ItemType Directory -Path $webImageDir -Force | Out-Null
    Push-Location $webDir
    try {
        & $npmCommand run build -- $webImageDir
    }
    finally {
        Pop-Location
    }

    Write-Host "[ESP32][LittleFS] Packing $imagePath"
    & $mklittlefs -c $webImageDir -b 4096 -p $pageSize -s $partition.Size $imagePath
}
else {
    Write-Host "[ESP32][LittleFS] Web image already up to date"
}

$needsFlash = -not (Test-Path -Path $flashStampPath)
if (-not $needsFlash) {
    $needsFlash = (Get-Item -Path $imagePath).LastWriteTimeUtc -gt (Get-Item -Path $flashStampPath).LastWriteTimeUtc
}

if ($SkipFlash) {
    if ($needsFlash) {
        Write-Host "[ESP32][LittleFS] Flash pending for $imagePath"
    }
    else {
        Write-Host "[ESP32][LittleFS] Flash already up to date"
    }
    exit 0
}

if (-not $needsFlash) {
    Write-Host "[ESP32][LittleFS] Skipping flash, no web changes detected"
    exit 0
}

Write-Host "[ESP32][LittleFS] Flashing $imagePath to partition www at $($partition.Offset)"
& $platformioPython $esptool --chip esp32 --port $uploadPort --baud $uploadSpeed write_flash -z $partition.Offset $imagePath
Set-Content -Path $flashStampPath -Value ((Get-Date).ToUniversalTime().ToString("o"))
Write-Host "[ESP32][LittleFS] Flash complete"