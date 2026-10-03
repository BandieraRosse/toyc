[CmdletBinding()]
param(
    [ValidateSet('clear','rain','warm')][string]$Preset='clear',
    [ValidateSet('north','east','south','west','up','down','sun','atmosphere')][string]$View='north',
    [switch]$Capture,
    [ValidateRange(0,1000000)][double]$Time=0,
    [string]$OutputDirectory='tmp/sky-v2-capture'
)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Exe=Join-Path $Package 'rasterfall.exe'
if (-not (Test-Path -LiteralPath $Exe)) { throw 'Run windows/NativeCodex.ps1 build first' }
if (Get-Process rasterfall -ErrorAction SilentlyContinue) { throw 'Rasterfall is already running' }
$SavedPreset=$env:RF_GPU_SKY_PRESET
$SavedTime=$env:RF_GPU_SKY_TIME
$SavedPath=$env:Path
try {
    # Some host shells expose both PATH and Path; Start-Process rejects duplicates.
    [Environment]::SetEnvironmentVariable('PATH',$null,'Process')
    [Environment]::SetEnvironmentVariable('Path',$SavedPath,'Process')
    $env:RF_GPU_SKY_PRESET=$Preset
    $env:RF_GPU_SKY_TIME=if ($Capture) { $Time.ToString([Globalization.CultureInfo]::InvariantCulture) } else { $null }
    $Scene=if($View -eq 'atmosphere'){'atmosphere-lab'}else{"sky-$View"}
    $Argv=@('--renderer','gpu-scene','--map',"$Root/rasterfall/assets/maps/outpost.map",
        '--gpu-normal-scene',$Scene,'0')
    if ($Capture) {
        $Out=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
        New-Item -ItemType Directory -Force -Path $Out | Out-Null
        $Stem=Join-Path $Out "$Preset-$View"
        if (Test-Path -LiteralPath "$Stem.out") { throw 'Use a new output directory for repeated evidence' }
        $Argv+=@('--frames','3','--gpu-normal-fixed-tick','--frame-audit',
            '--gpu-frame-capture',"$Stem.bmp",'--gpu-capture-frame','2')
        $Process=Start-Process -FilePath $Exe -WorkingDirectory $Package -WindowStyle Hidden -PassThru `
            -ArgumentList (($Argv | ForEach-Object {'"'+$_+'"'}) -join ' ') `
            -RedirectStandardOutput "$Stem.out" -RedirectStandardError "$Stem.err"
        $Handle=$Process.Handle
        $Deadline=[DateTime]::UtcNow.AddMinutes(3)
        while (-not $Process.WaitForExit(500)) {
            if ([DateTime]::UtcNow -gt $Deadline) {
                Stop-Process -Id $Process.Id -Force
                throw 'Sky capture timed out'
            }
        }
        $Log=Get-Content -Encoding UTF8 "$Stem.out","$Stem.err" | Out-String
        if ($Process.ExitCode -ne 0 -or $Log -match 'Validation Error|SYNC-HAZARD|VUID-' -or
            -not (Test-Path -LiteralPath "$Stem.bmp.scene.ppm")) {
            throw "Sky capture failed: exit=$($Process.ExitCode); inspect $Stem.out and .err"
        }
        Write-Output "Sky capture PASS: $Stem.bmp.scene.ppm"
    } else {
        Push-Location $Package
        try { & $Exe @Argv; if ($LASTEXITCODE) { throw "Sky viewer exited with $LASTEXITCODE" } }
        finally { Pop-Location }
    }
} finally {
    $env:RF_GPU_SKY_PRESET=$SavedPreset
    $env:RF_GPU_SKY_TIME=$SavedTime
    [Environment]::SetEnvironmentVariable('Path',$SavedPath,'Process')
}
