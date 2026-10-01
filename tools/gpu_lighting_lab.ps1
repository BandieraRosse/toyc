[CmdletBinding()]
param([int]$Frames=0)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Exe=Join-Path $Package 'rasterfall.exe'
if (-not (Test-Path -LiteralPath $Exe)) { throw 'Build first: windows/NativeCodex.ps1 build' }
Push-Location $Package
try {
    $Arguments=@('--renderer','gpu-scene','--map',(Join-Path $Root 'rasterfall/assets/maps/outpost.map'),'--gpu-normal-scene','lighting-lab','0')
    if($Frames -gt 0) { $Arguments+=@('--frames',"$Frames") }
    # Direct invocation keeps the interactive process attached to this shell.
    & $Exe @Arguments
    if($LASTEXITCODE) { throw "Lighting lab exited with $LASTEXITCODE" }
} finally { Pop-Location }
