[CmdletBinding()]
param([string]$OutputDirectory='tmp/scene-old-map-perf',
      [ValidateRange(120,10000)][int]$Frames=360,
      [ValidateRange(480,10000)][int]$AutoFrames=960,
      [ValidateRange(1,5)][int]$Rounds=3,
      [switch]$CompareNavGround,
      [switch]$ProfileTriangleUpdate,
      [ValidateSet(16,32,64)][int[]]$CorridorEnemies=@(32,64),
      [ValidateSet('All','Stationary','Auto','Corridor')][string]$Stage='All')
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Out=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
if (Test-Path -LiteralPath $Out) { throw 'Use a new evidence directory' }
if (Get-Process rasterfall -ErrorAction SilentlyContinue) { throw 'Rasterfall is already running' }
New-Item -ItemType Directory -Path $Out | Out-Null
$Saved=@{}
$OriginalPath=[Environment]::GetEnvironmentVariable('Path','Process')
$Runs=[Collections.Generic.List[object]]::new()
$RunningProcess=$null
$Cases=[Collections.Generic.List[object]]::new()
if ($Stage -in @('All','Stationary')) {
    foreach ($N in @(0,10,20,30)) {
        foreach ($Clock in @('fixed','realtime')) {
            $Cases.Add(@{name="near-$N-$Clock";view='near';enemies=$N;clock=$Clock;auto=$false;frames=$Frames})
        }
    }
    foreach ($View in @('mid','west-facility')) {
        $Cases.Add(@{name="$View-20-realtime";view=$View;enemies=20;clock='realtime';auto=$false;frames=$Frames})
    }
}
if ($Stage -in @('All','Auto')) {
    $Cases.Add(@{name='auto-wave-realtime';view='wave';enemies=-1;clock='realtime';auto=$true;frames=$AutoFrames})
}
if ($Stage -in @('All','Corridor')) {
    foreach ($Clock in @('fixed','realtime')) {
        $Cases.Add(@{name="west-empty-$Clock";view='west-empty';enemies=0;clock=$Clock;auto=$false;frames=$Frames})
    }
    foreach ($N in ($CorridorEnemies | Select-Object -Unique)) {
        foreach ($View in @('west-button','west-button-no-tank')) {
            foreach ($Clock in @('fixed','realtime')) {
                $Cases.Add(@{name="$View-$N-$Clock";view=$View;enemies=$N;clock=$Clock;auto=$false;frames=$Frames})
            }
        }
    }
}
if ($CompareNavGround) {
    $Variants=[Collections.Generic.List[object]]::new()
    foreach ($Case in $Cases) {
        foreach ($Legacy in @($false,$true)) {
            $Variant=$Case.Clone()
            $Variant.name=$Case.name+$(if ($Legacy) {'-reference'} else {'-reuse'})
            $Variant.legacy_nav_ground=$Legacy
            $Variants.Add($Variant)
        }
    }
    $Cases=$Variants
}
foreach ($Key in @('RF_GPU_VULKAN_VENDOR_ID','VK_INSTANCE_LAYERS',
    'RF_GAME_LEGACY_NAV_GROUND','RF_GAME_PROFILE_NO_CLOCK',
    'RF_GPU_PROFILE_TRIANGLE_UPDATE',
    'RF_GPU_SCENE_REBUILD_DYNAMIC','RF_GPU_SKIN_LEGACY_UPLOAD','RF_GPU_SKIN_STAGING_UPLOAD',
    'RF_GPU_SCENE_LEGACY_ENEMY_PREP','RF_GPU_SCENE_LEGACY_BIND','RF_GPU_SCENE_LEGACY_SKIN_REUSE',
    'RF_GPU_SCENE_LEGACY_SKIN_BATCH','RF_GPU_SCENE_REBUILD_LAYERS','RF_GPU_SCENE_LEGACY_BIND_UPLOAD',
    'RF_GPU_SCENE_LEGACY_VERTEX_TRANSFORM','RF_GPU_SCENE_LEGACY_COLOR_DRAWS')) {
    $Saved[$Key]=[Environment]::GetEnvironmentVariable($Key,'Process')
}
try {
    [Environment]::SetEnvironmentVariable('PATH',$null,'Process')
    [Environment]::SetEnvironmentVariable('Path',$OriginalPath,'Process')
    foreach ($Key in $Saved.Keys) { [Environment]::SetEnvironmentVariable($Key,'0','Process') }
    $env:RF_GPU_VULKAN_VENDOR_ID='0x10de'
    $env:RF_GPU_PROFILE_TRIANGLE_UPDATE=if ($ProfileTriangleUpdate) {'1'} else {'0'}
    [Environment]::SetEnvironmentVariable('VK_INSTANCE_LAYERS',$null,'Process')
    Get-FileHash "$Package/rasterfall.exe" | ConvertTo-Json | Set-Content -Encoding UTF8 "$Out/executable.json"
    Get-FileHash "$Package/rasterfall/assets/maps/rasterfall.map" | ConvertTo-Json | Set-Content -Encoding UTF8 "$Out/map.json"
    @{rounds=$Rounds;cases=@($Cases | ForEach-Object {$_.name});stage=$Stage;compare_nav_ground=[bool]$CompareNavGround;profile_triangle_update=[bool]$ProfileTriangleUpdate} |
        ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 "$Out/config.json"
    for ($Round=1;$Round -le $Rounds;$Round++) {
        $Ordered=@($Cases)
        if ($Round%2 -eq 0) { [array]::Reverse($Ordered) }
        foreach ($Case in $Ordered) {
            if (Get-Process rasterfall -ErrorAction SilentlyContinue) { throw 'Rasterfall is already running' }
            $Name="r$Round-$($Case.name)"
            $env:RF_GAME_LEGACY_NAV_GROUND=if ($Case.legacy_nav_ground) {'1'} else {'0'}
            $Argv=@('--gpu-scene-play','--map','rasterfall/assets/maps/rasterfall.map',
                '--frame-audit','--frames',"$($Case.frames)")
            if ($Case.auto) { $Argv+=@('--auto','--gpu-wave-repro') }
            else { $Argv+=@('--gpu-normal-scene',$Case.view,"$($Case.enemies)") }
            if ($Case.clock -eq 'fixed') { $Argv+='--gpu-normal-fixed-tick' }
            $Samples=[Collections.Generic.List[object]]::new()
            $p=Start-Process "$Package/rasterfall.exe" -WorkingDirectory $Package -WindowStyle Hidden -PassThru `
                -ArgumentList (($Argv | ForEach-Object {'"'+$_+'"'}) -join ' ') `
                -RedirectStandardOutput "$Out/$Name.out" -RedirectStandardError "$Out/$Name.err"
            $Handle=$p.Handle
            $RunningProcess=$p
            $Timer=[Diagnostics.Stopwatch]::StartNew()
            while (-not $p.WaitForExit(500)) {
                if ($Timer.Elapsed.TotalSeconds -gt 600) {
                    Stop-Process -Id $p.Id -Force
                    throw "$Name timeout"
                }
                try {
                    $p.Refresh()
                    $Threads=@($p.Threads | ForEach-Object {
                        @{id=$_.Id;cpu_ms=$_.TotalProcessorTime.TotalMilliseconds}
                    })
                    $Tail=Get-Content -Encoding UTF8 -LiteralPath "$Out/$Name.out" -Tail 45
                    $MatchesFrame=[regex]::Matches(($Tail -join "`n"),'SCENE-RUNTIME frame=(\d+)')
                    $Frame=if ($MatchesFrame.Count) {[int]$MatchesFrame[$MatchesFrame.Count-1].Groups[1].Value} else {0}
                    $Samples.Add(@{elapsed_ms=$Timer.Elapsed.TotalMilliseconds;frame=$Frame;
                        process_ms=$p.TotalProcessorTime.TotalMilliseconds;threads=$Threads})
                } catch {
                    if (-not $p.HasExited) { throw }
                }
            }
            $Samples | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 "$Out/$Name.cpu.json"
            $RunningProcess=$null
            $Runs.Add(@{name=$Name;round=$Round;case=$Case;argv=$Argv;exit_code=$p.ExitCode})
            $Runs | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 "$Out/runs.json"
            if ($p.ExitCode -ne 0) { throw "$Name exit $($p.ExitCode)" }
            Write-Host "[OLD-MAP-PERF] $Name completed frames=$($Case.frames)"
        }
    }
    $InitialHash=(Get-Content -Encoding UTF8 "$Out/executable.json" | ConvertFrom-Json).Hash
    if ((Get-FileHash "$Package/rasterfall.exe").Hash -ne $InitialHash) { throw 'Executable changed during sampling' }
    & python "$PSScriptRoot/gpu_scene_old_map_report.py" $Out
    if ($LASTEXITCODE -ne 0) { throw 'Old map performance validation failed' }
} finally {
    if ($RunningProcess -and -not $RunningProcess.HasExited) { Stop-Process -Id $RunningProcess.Id -Force }
    foreach ($Key in $Saved.Keys) { [Environment]::SetEnvironmentVariable($Key,$Saved[$Key],'Process') }
    [Environment]::SetEnvironmentVariable('Path',$OriginalPath,'Process')
}
