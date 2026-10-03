[CmdletBinding()]
param(
    [string]$OutputDirectory='tmp/mesh-weaver/performance',
    [ValidateRange(1,5)][int]$Rounds=3,
    [ValidateRange(120,720)][int]$Samples=240,
    [ValidateSet('quarter','front','rear','gun','wide')][string]$View='quarter'
)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Out=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
if(Test-Path -LiteralPath $Out){throw 'Use a new evidence directory'}
if(Get-Process rasterfall -ErrorAction SilentlyContinue){throw 'Rasterfall already running'}
New-Item -ItemType Directory -Path $Out | Out-Null
$Utf8=[Text.UTF8Encoding]::new($false)
function Write-Json($Value,[string]$Name){
    [IO.File]::WriteAllText((Join-Path $Out $Name),(ConvertTo-Json -InputObject $Value -Depth 8),$Utf8)
}
function Parse-Fields([string]$Text,[string]$Prefix){
    $Match=[regex]::Match($Text,([regex]::Escape($Prefix)+' ([^\r\n]+)'))
    if(!$Match.Success){throw "Missing $Prefix report"}
    $Result=@{}
    foreach($Field in [regex]::Matches($Match.Groups[1].Value,'(\w+)=([^ ]+)')){
        $Result[$Field.Groups[1].Value]=$Field.Groups[2].Value
    }
    return $Result
}
$MapPath=Join-Path $Package 'rasterfall/assets/maps/outpost.map'
$MapText=[IO.File]::ReadAllText($MapPath,[Text.Encoding]::UTF8)
$AbsentMap=Join-Path $Out 'outpost-no-weaver.map'
$MachineRecords='(?m)^(?:object id=mesh_weaver(?:_service_links)? |render id=mesh_weaver_screen |collision id=mesh_weaver_(?:base_col|post_[0-3]) )[^\r\n]*(?:\r?\n|$)'
$Removed=[regex]::Matches($MapText,$MachineRecords)
if($Removed.Count -ne 8){throw 'Unexpected machine records; review absent map derivation'}
$AbsentText=[regex]::Replace($MapText,$MachineRecords,'')
[IO.File]::WriteAllText($AbsentMap,$AbsentText,$Utf8)
$SavedPath=$env:Path
$Keys=@('RF_SCENE_PERF_FRAMES','RF_WEAVER_PERF_MODE','RF_WEAVER_VIEW','RF_GPU_SKY_TIME',
    'RF_GPU_SKY_SCALE','RF_GPU_SCENE_PROFILE_SLOW','RF_LABS_ALL','VK_INSTANCE_LAYERS')
$Saved=@{}
foreach($Key in $Keys){$Saved[$Key]=[Environment]::GetEnvironmentVariable($Key,'Process')}
$Runs=[Collections.Generic.List[object]]::new()
$Process=$null
$Hash=(Get-FileHash -LiteralPath "$Package/rasterfall.exe").Hash
Write-Json @{exe=$Hash;map=(Get-FileHash -LiteralPath $MapPath).Hash;
    absent_map=(Get-FileHash -LiteralPath $AbsentMap).Hash;removed_records=@($Removed | ForEach-Object {$_.Value.Trim()});
    rounds=$Rounds;samples=$Samples;view=$View;warmup=120;sky_time=0;sky_scale=4;
    clock='realtime';cap=120;active='real AK task starts at warmup frame 60 with one second accounted preroll; sampling starts after 120';
    gpu_vendor=$env:RF_GPU_VULKAN_VENDOR_ID} 'config.json'
try {
    [Environment]::SetEnvironmentVariable('PATH',$null,'Process')
    [Environment]::SetEnvironmentVariable('Path',$SavedPath,'Process')
    $env:RF_SCENE_PERF_FRAMES=[string]$Samples
    $env:RF_WEAVER_VIEW=$View;$env:RF_GPU_SKY_TIME='0';$env:RF_GPU_SKY_SCALE='4'
    $env:RF_GPU_SCENE_PROFILE_SLOW='1';$env:RF_LABS_ALL='0'
    [Environment]::SetEnvironmentVariable('VK_INSTANCE_LAYERS',$null,'Process')
    for($Round=1;$Round -le $Rounds;$Round++) {
        $Modes=@('absent','idle','active')
        if($Round%2 -eq 0){[array]::Reverse($Modes)}
        foreach($Mode in $Modes) {
            $Name="r$Round-$Mode"
            $env:RF_WEAVER_PERF_MODE=$Mode
            $RunMap=if($Mode -eq 'absent'){$AbsentMap}else{$MapPath}
            $Argv=@('--skip-boot','--renderer','gpu-scene','--map',$RunMap,'--gpu-normal-scene','mesh-weaver','0')
            $Process=Start-Process -FilePath "$Package/rasterfall.exe" -WorkingDirectory $Package -WindowStyle Hidden -PassThru `
                -ArgumentList (($Argv | ForEach-Object {'"'+$_+'"'}) -join ' ') `
                -RedirectStandardOutput "$Out/$Name.out" -RedirectStandardError "$Out/$Name.err"
            $Handle=$Process.Handle
            $Timer=[Diagnostics.Stopwatch]::StartNew()
            while(-not $Process.WaitForExit(500)) {
                if($Timer.Elapsed.TotalSeconds -gt 240){throw "$Name timed out"}
            }
            $Process.WaitForExit()
            $Log=[IO.File]::ReadAllText("$Out/$Name.out",[Text.Encoding]::UTF8)
            $Errors=[IO.File]::ReadAllText("$Out/$Name.err",[Text.Encoding]::UTF8)
            if($Process.ExitCode -ne 0 -or $Errors -match 'VUID-|Validation Error|SYNC-HAZARD|preparation/submit failed'){
                throw "$Name failed; inspect stdout/stderr"
            }
            $Result=Parse-Fields $Log 'SCENE-PERF'
            $Work=Parse-Fields $Log 'MESH-WEAVER-PERF'
            if($Result.valid -ne '1' -or [int]$Result.samples -ne $Samples -or
                [int]$Result.gpu_p50_us -le 0 -or $Work.valid -ne '1' -or $Work.mode -ne $Mode){
                throw "$Name invalid sample or job finished before sample ended"
            }
            $Runs.Add(@{round=$Round;mode=$Mode;result=$Result;workload=$Work;argv=$Argv;exit=$Process.ExitCode})
            Write-Json @($Runs.ToArray()) 'report.json'
            Write-Output "$Name p50=$($Result.p50_us) p95=$($Result.p95_us) GPU=$($Result.gpu_p50_us) prepare=$($Result.prepare_p50_us) us"
            $Process=$null
        }
    }
    if((Get-FileHash -LiteralPath "$Package/rasterfall.exe").Hash -ne $Hash){throw 'Executable changed while sampling'}
} finally {
    if($Process -and !$Process.HasExited){Stop-Process -Id $Process.Id -Force}
    foreach($Key in $Keys){[Environment]::SetEnvironmentVariable($Key,$Saved[$Key],'Process')}
    [Environment]::SetEnvironmentVariable('Path',$SavedPath,'Process')
}
