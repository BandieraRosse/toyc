[CmdletBinding()]
param(
    [string]$OutputDirectory='tmp/character-upgrade/equipment-native',
    [ValidateSet('rifle-idle','rifle-walk','rifle-fire','breacher','recon','medic','engineer','heavy','gunner','elite')]
    [string[]]$Stations=@('breacher','heavy','elite'),
    [ValidateSet('front','quarter','side','back')]
    [string[]]$Views=@('front','quarter','back'),
    [ValidateRange(2,36000)][int]$Frames=8
)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Out=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
if(Test-Path -LiteralPath $Out){throw 'Use a new evidence directory'}
if(Get-Process rasterfall -ErrorAction SilentlyContinue){throw 'Rasterfall already running'}
New-Item -ItemType Directory -Path $Out | Out-Null
$Names=@('rifle-idle','rifle-walk','rifle-fire','breacher','recon','medic','engineer','heavy','gunner','elite')
$Saved=@{}
foreach($Key in @('RF_GPU_EQUIPMENT_STATION','RF_GPU_EQUIPMENT_VIEW','RF_LABS_ALL')) {
    $Saved[$Key]=[Environment]::GetEnvironmentVariable($Key,'Process')
}
$Runs=[Collections.Generic.List[object]]::new()
$Utf8=[Text.UTF8Encoding]::new($false)
$Process=$null
$SavedPath=$env:Path
try {
    [Environment]::SetEnvironmentVariable('PATH',$null,'Process')
    [Environment]::SetEnvironmentVariable('Path',$SavedPath,'Process')
    $env:RF_LABS_ALL='0'
    foreach($Station in $Stations){foreach($View in $Views){
        $env:RF_GPU_EQUIPMENT_STATION=[string][Array]::IndexOf($Names,$Station)
        $env:RF_GPU_EQUIPMENT_VIEW=$View
        $Name="$Station-$View"
        $Capture=Join-Path $Out "$Name.bmp"
        $Argv=@('--skip-boot','--renderer','gpu-scene','--map','rasterfall/assets/maps/outpost.map',
            '--gpu-normal-scene','equipment-lab','0','--gpu-normal-fixed-tick','--frames',"$Frames",
            '--frame-audit','--gpu-frame-capture',$Capture,'--gpu-capture-frame',"$Frames")
        $Process=Start-Process -FilePath "$Package/rasterfall.exe" -WorkingDirectory $Package -WindowStyle Hidden -PassThru `
            -ArgumentList (($Argv | ForEach-Object {'"'+$_+'"'}) -join ' ') `
            -RedirectStandardOutput "$Out/$Name.out" -RedirectStandardError "$Out/$Name.err"
        $Handle=$Process.Handle
        $Timer=[Diagnostics.Stopwatch]::StartNew()
        while(-not $Process.WaitForExit(500)) {
            if($Timer.Elapsed.TotalSeconds -gt 240){throw "$Name timed out"}
        }
        $Process.WaitForExit()
        $Log=Get-Content -Encoding UTF8 "$Out/$Name.out","$Out/$Name.err" | Out-String
        $Native=[regex]::Matches($Log,'SCENE-NATIVE frame=(\d+) world_only=0 draws=\d+ bridges=0 readback=([01]) mixed_execute=0')
        $Sources=[regex]::Matches($Log,'SCENE-SOURCE frame=(\d+) independent=1 legacy_producer=0 raster_commands=0 mixed_draws=0')
        if($Process.ExitCode -ne 0 -or $Native.Count -ne $Frames -or $Sources.Count -ne $Frames -or
            $Log -match 'resource unavailable|Validation Error|SYNC-HAZARD|VUID-' -or
            !(Test-Path -LiteralPath "$Capture.scene.ppm")) {throw "$Name failed; inspect logs"}
        for($Frame=1;$Frame -le $Frames;$Frame++) {
            $ExpectedReadback=if($Frame -eq $Frames){1}else{0}
            if([int]$Native[$Frame-1].Groups[1].Value -ne $Frame -or
                [int]$Sources[$Frame-1].Groups[1].Value -ne $Frame -or
                [int]$Native[$Frame-1].Groups[2].Value -ne $ExpectedReadback) {
                throw "$Name frame order or readback contract failed"
            }
        }
        $Runs.Add(@{station=$Station;view=$View;frames=$Frames;exit_code=$Process.ExitCode;
            image="$Capture.scene.ppm";sha256=(Get-FileHash -LiteralPath "$Capture.scene.ppm").Hash})
        $Process=$null
        Write-Output "[EQUIPMENT] $Name PASS"
    }}
    $Assets=@(Get-ChildItem -LiteralPath "$Package/rasterfall/assets/models/characters" -Filter '*.rmesh' |
        Where-Object {$_.Name -like 'rf_gear_*' -or $_.Name -eq 'rf_humanoid_v2.rmesh'} |
        ForEach-Object {@{name=$_.Name;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
    $Manifest=@{exe_sha256=(Get-FileHash -LiteralPath "$Package/rasterfall.exe").Hash;
        map_sha256=(Get-FileHash -LiteralPath "$Package/rasterfall/assets/maps/outpost.map").Hash;
        assets=$Assets;captures=@($Runs.ToArray())}
    [IO.File]::WriteAllText((Join-Path $Out 'manifest.json'),(ConvertTo-Json -InputObject $Manifest -Depth 8),$Utf8)
} finally {
    if($Process -and !$Process.HasExited){Stop-Process -Id $Process.Id -Force}
    foreach($Key in $Saved.Keys){[Environment]::SetEnvironmentVariable($Key,$Saved[$Key],'Process')}
    [Environment]::SetEnvironmentVariable('Path',$SavedPath,'Process')
}
