param(
    [Parameter(Position = 0)]
    [string]$Image,

    [switch]$Probe,

    [string]$OpenOcdExe = "openocd",

    [string]$OpenOcdScripts = $(if ($env:OPENOCD_SCRIPTS) { $env:OPENOCD_SCRIPTS } else { "D:/oss-cad-suite/share/openocd/scripts" }),

    [string]$UjprogBit2Svf,

    [switch]$KeepGeneratedSvf
)

$ErrorActionPreference = "Stop"

function Show-Usage {
    Write-Host "usage: .\dapprog.ps1 [--probe|-Probe] <file.bit|file.svf>"
    Write-Host "examples:"
    Write-Host "  .\tools\dapprog.ps1 --probe"
    Write-Host "  .\tools\dapprog.ps1 build/top.bit"
    Write-Host "  .\tools\dapprog.ps1 build/top_flash.svf"
}

function Resolve-CommandPath {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Candidates,

        [string]$ExplicitPath
    )

    if ($ExplicitPath) {
        if (Test-Path $ExplicitPath) {
            return (Resolve-Path $ExplicitPath).Path
        }
        $cmd = Get-Command $ExplicitPath -ErrorAction SilentlyContinue
        if ($cmd) {
            return $cmd.Source
        }
        throw "Tool not found: $ExplicitPath"
    }

    foreach ($candidate in $Candidates) {
        $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($cmd) {
            return $cmd.Source
        }
    }

    return $null
}

function Invoke-OpenOcd {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CommandString
    )

    $openOcdPath = Resolve-CommandPath -Candidates @($OpenOcdExe)
    if (-not $openOcdPath) {
        throw "openocd not found. Pass -OpenOcdExe or add it to PATH."
    }

    if (-not (Test-Path $OpenOcdScripts)) {
        throw "OpenOCD scripts directory not found: $OpenOcdScripts"
    }

    & $openOcdPath \
        -s $OpenOcdScripts \
        -f interface/cmsis-dap.cfg \
        -c "transport select jtag" \
        -f fpga/lattice_ecp5.cfg \
        -c $CommandString

    if ($LASTEXITCODE -ne 0) {
        throw "openocd failed with exit code $LASTEXITCODE"
    }
}

function Convert-BitToSvf {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BitFile
    )

    $scriptDir = Split-Path -Parent $PSCommandPath
    $converterPath = Resolve-CommandPath \
        -Candidates @("ujprog.bit2svf.exe", "ujprog.bit2svf") \
        -ExplicitPath $UjprogBit2Svf

    if (-not $converterPath) {
        $localCandidates = @(
            (Join-Path $scriptDir "ujprog.bit2svf.exe"),
            (Join-Path $scriptDir "ujprog.bit2svf")
        )
        foreach ($candidate in $localCandidates) {
            if (Test-Path $candidate) {
                $converterPath = (Resolve-Path $candidate).Path
                break
            }
        }
    }

    if (-not $converterPath) {
        throw "ujprog.bit2svf not found. Pass -UjprogBit2Svf or add it next to this script / in PATH."
    }

    $resolvedBitFile = (Resolve-Path $BitFile).Path
    $baseName = [System.IO.Path]::Combine(
        [System.IO.Path]::GetDirectoryName($resolvedBitFile),
        [System.IO.Path]::GetFileNameWithoutExtension($resolvedBitFile)
    )
    $targetSvf = "${baseName}_flash.svf"

    & $converterPath -j FLASH $resolvedBitFile | Set-Content -Path $targetSvf -Encoding ascii

    if ($LASTEXITCODE -ne 0) {
        throw "ujprog.bit2svf failed with exit code $LASTEXITCODE"
    }

    return $targetSvf
}

if ($Probe.IsPresent) {
    Invoke-OpenOcd -CommandString "init; scan_chain; exit"
    exit 0
}

if (-not $Image) {
    Show-Usage
    exit 0
}

if (-not (Test-Path $Image)) {
    throw "Input file not found: $Image"
}

$resolvedImage = (Resolve-Path $Image).Path
$extension = [System.IO.Path]::GetExtension($resolvedImage).ToLowerInvariant()

switch ($extension) {
    ".svf" {
        $target = $resolvedImage
    }
    ".bit" {
        $target = Convert-BitToSvf -BitFile $resolvedImage
    }
    default {
        throw "illegal suffix [$extension]"
    }
}

Write-Host "TARGET: $target"

Invoke-OpenOcd -CommandString "init; scan_chain; svf -tap ecp5.tap -quiet -progress $target; exit"

if (($extension -eq ".bit") -and (-not $KeepGeneratedSvf.IsPresent)) {
    Remove-Item -Path $target -ErrorAction SilentlyContinue
}