[CmdletBinding()]
param(
    [string]$OutputDirectory='tmp/weapon-cycle/native',
    [ValidateRange(-1,14)][int]$Station=-1,
    [ValidateRange(-1,59999)][int]$TimeMs=-1,
    [ValidateSet('front','quarter','side','left')][string]$View='quarter',
    [ValidateRange(2,36000)][int]$Frames=60
)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Out=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Saved=@{}
$SavedPath=$env:Path
foreach($Key in @('RF_WEAPON_CYCLE_TIME_MS','RF_WEAPON_CYCLE_STATION','RF_WEAPON_CYCLE_VIEW')) {
    $Saved[$Key]=[Environment]::GetEnvironmentVariable($Key,'Process')
}
try {
    [Environment]::SetEnvironmentVariable('PATH',$null,'Process')
    [Environment]::SetEnvironmentVariable('Path',$SavedPath,'Process')
    $env:RF_WEAPON_CYCLE_TIME_MS=if($TimeMs -ge 0){[string]$TimeMs}else{''}
    $env:RF_WEAPON_CYCLE_STATION=[string]$Station
    $env:RF_WEAPON_CYCLE_VIEW=$View
    $Name="station-$Station-time-$TimeMs-$View"
    $Capture=Join-Path $Out "$Name.bmp"
    $Argv=@('--renderer','gpu-scene','--map','rasterfall/assets/maps/outpost.map',
        '--gpu-normal-scene','weapon-cycle-lab','0','--gpu-normal-fixed-tick',
        '--frames',"$Frames",'--frame-audit','--gpu-frame-capture',$Capture,'--gpu-capture-frame',"$Frames")
    $Quoted=($Argv | ForEach-Object {'"'+$_+'"'}) -join ' '
    $p=Start-Process -FilePath "$Package/rasterfall.exe" -WorkingDirectory $Package `
        -ArgumentList $Quoted -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput "$Out/$Name.out" -RedirectStandardError "$Out/$Name.err"
    $ProcessHandle=$p.Handle
    $Deadline=[DateTime]::UtcNow.AddMinutes(4)
    while(-not $p.WaitForExit(1000)) {
        if([DateTime]::UtcNow -gt $Deadline){Stop-Process -Id $p.Id -Force;throw 'Weapon cycle timeout'}
    }
    if($p.ExitCode -ne 0){throw "Weapon cycle exit $($p.ExitCode)"}
    $Log=Get-Content -Encoding UTF8 "$Out/$Name.out","$Out/$Name.err" | Out-String
    if($Log -notmatch 'SCENE-NATIVE.*bridges=0.*mixed_execute=0' -or
        $Log -match 'unavailable|Validation Error|SYNC-HAZARD|VUID-|\bFAIL\b' -or
        -not (Test-Path -LiteralPath "$Capture.scene.ppm")){throw 'Weapon cycle incomplete native evidence'}
    @{station=$Station;time_ms=$TimeMs;view=$View;frames=$Frames;exit_code=$p.ExitCode;
        capture="$Capture.scene.ppm";exe_sha256=(Get-FileHash "$Package/rasterfall.exe").Hash} |
        ConvertTo-Json | Set-Content -Encoding UTF8 "$Out/$Name.json"
    Write-Host "[WEAPON-CYCLE] $Name PASS frames=$Frames"
} finally {
    $env:Path=$SavedPath
    foreach($Key in $Saved.Keys){[Environment]::SetEnvironmentVariable($Key,$Saved[$Key],'Process')}
}
