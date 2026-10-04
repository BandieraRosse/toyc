[CmdletBinding()]
param(
    [string]$OutputDirectory='tmp/player-ui/performance',
    [ValidateRange(1,5)][int]$Rounds=2,
    [ValidateRange(120,4096)][int]$Samples=360,
    [string[]]$Cases=@('experiment-fps','player-fps','experiment-rts','player-rts','weaver-preview',
        'comms-visible','comms-hidden','comms-closed','comms-remote','comms-remote-hidden','dual-view'),
    [string]$Executable='build-windows/rasterfall-windows/rasterfall.exe',
    [ValidateRange(640,7680)][int]$Width=1280,
    [ValidateRange(480,4320)][int]$Height=720,
    [switch]$CheckOnly
)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Exe=if([IO.Path]::IsPathRooted($Executable)){[IO.Path]::GetFullPath($Executable)}else{[IO.Path]::GetFullPath((Join-Path $Root $Executable))}
$Allowed=@('experiment-fps','player-fps','experiment-rts','player-rts','weaver-preview',
    'comms-visible','comms-hidden','comms-closed','comms-remote','comms-remote-hidden','dual-view')
$Cases=@($Cases|ForEach-Object {$_ -split ','})
foreach($Case in $Cases){if($Case -notin $Allowed){throw "Unknown case: $Case"}}
if($CheckOnly){Write-Output '[UI-PERF] Script parsed. GPU was not started.';exit 0}
if(-not (Test-Path -LiteralPath $Exe -PathType Leaf)){throw 'Native executable is missing'}
if([IO.Path]::GetDirectoryName($Exe) -ne $Package){throw 'Comparison executable must share the staged asset directory'}
if(Get-Process -Name @('rasterfall','rf-gpu-graphics-test',[IO.Path]::GetFileNameWithoutExtension($Exe)) -ErrorAction SilentlyContinue){throw 'GPU lane is busy'}
$Out=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
if(Test-Path -LiteralPath $Out){throw 'Use a new evidence directory'}
New-Item -ItemType Directory -Path $Out | Out-Null
$Utf8=[Text.UTF8Encoding]::new($false)
function Write-Json($Value,[string]$Name){
    [IO.File]::WriteAllText((Join-Path $Out $Name),(ConvertTo-Json -InputObject $Value -Depth 10),$Utf8)
}
function Parse-Fields([string]$Text,[string]$Prefix){
    $Match=[regex]::Match($Text,([regex]::Escape($Prefix)+' ([^\r\n]+)'))
    if(!$Match.Success){throw "Missing $Prefix report"}
    $Result=@{}
    foreach($Field in [regex]::Matches($Match.Groups[1].Value,'(\w+)=([^ ]+)')){$Result[$Field.Groups[1].Value]=$Field.Groups[2].Value}
    return $Result
}
$Keys=@('RF_SCENE_PERF_FRAMES','RF_UI_PERF_CASE','RF_UI_MODE','RF_UI_STORY','RF_UI_NO_SAVE',
    'RF_UI_AUDIT','RF_UI_CAPTURE_DIRECTORY','RF_UI_SAVE_PATH','RF_STORY_SAVE_PATH',
    'RF_WEAVER_VIEW','RF_WEAVER_MENU','RF_WEAVER_BLUEPRINT','RF_WEAVER_TIME_MS','RF_WEAVER_PAUSE_MS',
    'RF_WEAVER_RESUME_MS','RF_WEAVER_PERF_MODE','RF_WEAVER_INTERACTION_AUDIT','RF_WEAVER_CAPTURE_DIRECTORY',
    'RF_WEAVER_CAPTURE_EVERY','RF_GPU_SKY_TIME','RF_GPU_SKY_SCALE','RF_GPU_SCENE_PROFILE_SLOW',
    'RF_GPU_SCENE_PROFILE_LAYERS','RF_GPU_SCENE_PROFILE_UPLOADS','RF_LABS_ALL','VK_INSTANCE_LAYERS')
$Saved=@{};$SavedPath=$env:Path
foreach($Key in $Keys){$Saved[$Key]=[Environment]::GetEnvironmentVariable($Key,'Process')}
$Hashes=@{}
foreach($Path in @($Exe,(Join-Path $Package 'rasterfall/assets/maps/outpost.map'),
    (Join-Path $Package 'rasterfall/assets/worlds/outpost.content'),
    (Join-Path $Package 'rasterfall/assets/models/pg_glock1.rmesh'),
    (Join-Path $Package 'rasterfall/assets/models/ar_ak47.rmesh'),
    (Join-Path $Package 'rasterfall/assets/manufacturing/blueprints.json'))){$Hashes[$Path]=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash}
$Config=@{executable=$Exe;comparison='same staged assets; use explicit Executable for binary A/B';hashes=$Hashes;
    rounds=$Rounds;samples=$Samples;cases=$Cases;width=$Width;height=$Height;warmup=120;cap=120;clock='realtime';sky_time=0;sky_scale=4;
    gpu_vendor=$env:RF_GPU_VULKAN_VENDOR_ID;frame_cpu='main-thread OS CPU time; scheduling accounting can quantize short frames';
    auxiliary_cpu='wall time of real low-frequency update, includes its synchronous GPU retirement';
    auxiliary_gpu='GPU timestamp duration of real auxiliary render; not added to main-frame percentiles';
    input='typed commands only during warmup; steady-state observations do not change owners'}
Write-Json $Config 'config.json'
$Runs=[Collections.Generic.List[object]]::new();$Process=$null
try {
    [Environment]::SetEnvironmentVariable('PATH',$null,'Process')
    [Environment]::SetEnvironmentVariable('Path',$SavedPath,'Process')
    foreach($Key in $Keys){[Environment]::SetEnvironmentVariable($Key,$null,'Process')}
    $env:RF_SCENE_PERF_FRAMES=[string]$Samples;$env:RF_UI_NO_SAVE='1'
    $env:RF_GPU_SKY_TIME='0';$env:RF_GPU_SKY_SCALE='4';$env:RF_LABS_ALL='0'
    for($Round=1;$Round -le $Rounds;$Round++) {
        $Sequence=@($Cases);if($Round%2 -eq 0){[array]::Reverse($Sequence)}
        foreach($Case in $Sequence) {
            $Name="r$Round-$Case";$env:RF_UI_PERF_CASE=$Case
            $env:RF_UI_MODE=if($Case -like 'experiment-*'){'experiment'}else{'player'}
            $env:RF_UI_STORY=if($Case -like 'comms-*' -or $Case -eq 'dual-view'){'1'}else{'0'}
            $View=if($Case -eq 'weaver-preview' -or $Case -like 'comms-remote*'){'mesh-weaver'}else{'sky-north'}
            $env:RF_WEAVER_VIEW='interaction'
            $Argv=@('--skip-boot','--window-size',[string]$Width,[string]$Height,'--renderer','gpu-scene','--map','rasterfall/assets/maps/outpost.map','--gpu-normal-scene',$View,'0')
            $Process=Start-Process -FilePath $Exe -WorkingDirectory $Package -WindowStyle Hidden -PassThru `
                -ArgumentList (($Argv|ForEach-Object {'"'+$_+'"'}) -join ' ') `
                -RedirectStandardOutput "$Out/$Name.out" -RedirectStandardError "$Out/$Name.err"
            $Handle=$Process.Handle;$Timer=[Diagnostics.Stopwatch]::StartNew()
            while(-not $Process.WaitForExit(500)){if($Timer.Elapsed.TotalSeconds -gt 180){throw "$Name timed out"}}
            $Process.WaitForExit();$Process.Refresh()
            $Log=[IO.File]::ReadAllText("$Out/$Name.out",[Text.Encoding]::UTF8)
            $Errors=[IO.File]::ReadAllText("$Out/$Name.err",[Text.Encoding]::UTF8)
            if($Process.ExitCode -ne 0 -or $Errors -match 'VUID-|Validation Error|SYNC-HAZARD|preparation/submit failed'){throw "$Name failed; inspect stdout/stderr"}
            $Frame=Parse-Fields $Log 'SCENE-PERF';$Cpu=Parse-Fields $Log 'SCENE-CPU';$Ui=Parse-Fields $Log 'UI-PERF'
            if($Frame.valid -ne '1' -or $Ui.valid -ne '1' -or [int]$Frame.samples -ne $Samples -or
                [int]$Cpu.samples -ne $Samples -or [int]$Ui.samples -ne $Samples -or $Ui.case -ne $Case -or
                $Frame.extent -ne ($Width.ToString()+'x'+$Height.ToString()) -or
                [int]$Frame.gpu_p50_us -le 0){throw "$Name invalid workload or timing samples"}
            $Views=@([regex]::Matches($Log,'UI-VIEW [^\r\n]+') | ForEach-Object {Parse-Fields $_.Value 'UI-VIEW'})
            $Prewarm=@([regex]::Matches($Log,'SCENE-PREWARM [^\r\n]+') | ForEach-Object {Parse-Fields $_.Value 'SCENE-PREWARM'})
            $Runs.Add(@{round=$Round;case=$Case;view=$View;frame=$Frame;cpu=$Cpu;ui=$Ui;auxiliary=$Views;
                prewarm=$Prewarm;argv=$Argv;exit=$Process.ExitCode})
            Write-Json @($Runs.ToArray()) 'report.json'
            Write-Output "$Name frame_p50/p95=$($Frame.p50_us)/$($Frame.p95_us) cpu_p50/p95=$($Cpu.thread_cpu_p50_us)/$($Cpu.thread_cpu_p95_us) gpu_p50/p95=$($Frame.gpu_p50_us)/$($Frame.gpu_p95_us) aux=$($Ui.aux_samples) us"
            $Process=$null
        }
    }
    foreach($Path in $Hashes.Keys){if((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $Hashes[$Path]){throw "Evidence input changed: $Path"}}
} finally {
    if($Process -and !$Process.HasExited){Stop-Process -Id $Process.Id -Force}
    foreach($Key in $Keys){[Environment]::SetEnvironmentVariable($Key,$Saved[$Key],'Process')}
    [Environment]::SetEnvironmentVariable('Path',$SavedPath,'Process')
}
