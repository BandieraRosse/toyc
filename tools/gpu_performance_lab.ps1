[CmdletBinding()]
param(
    [string]$OutputDirectory='tmp/performance-lab',
    [ValidateRange(1,10)][int]$Rounds=3,
    [ValidateSet('Isolated','Interference','Full','Panorama','Live','All')][string]$Stage='Isolated',
    [ValidateRange(1,10)][int[]]$Scenes=@(1,2,3,4),
    [switch]$Capped,
    [switch]$CompareGeometry,
    [switch]$CompareBackend,
    [switch]$ProfileSlow,
    [int]$Width=0,
    [int]$Height=0
)
$ErrorActionPreference='Stop'
if(($Width -ne 0 -or $Height -ne 0) -and ($Width -lt 640 -or $Width -gt 7680 -or $Height -lt 480 -or $Height -gt 4320)) {
    throw 'Specify both Width (640..7680) and Height (480..4320), or leave both zero for the native display default'
}
if($CompareGeometry -and $CompareBackend) {throw 'Choose one comparison axis'}
if(!$PSBoundParameters.ContainsKey('Scenes')) {
    if($Stage -eq 'Panorama') {$Scenes=@(5,6)}
    elseif($Stage -eq 'Live') {$Scenes=@(7,8,9,10)}
    elseif($Stage -eq 'All') {$Scenes=@(1,2,3,4,5,6,7,8,9,10)}
}
if($Stage -in @('Isolated','Interference') -and @($Scenes | Where-Object {$_ -gt 4}).Count) {
    throw 'Panorama/live scenes require Panorama, Live, Full or All stage'
}
if($Stage -eq 'Panorama' -and @($Scenes | Where-Object {$_ -lt 5 -or $_ -gt 6}).Count) {throw 'Panorama stage accepts scenes 5 and 6'}
if($Stage -eq 'Live' -and @($Scenes | Where-Object {$_ -lt 7}).Count) {throw 'Live stage accepts scenes 7 through 10'}
$TaskPath=[Environment]::GetEnvironmentVariable('Path','Process')
[Environment]::SetEnvironmentVariable('PATH',$null,'Process')
[Environment]::SetEnvironmentVariable('Path',$TaskPath,'Process')
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Out=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
if (Test-Path -LiteralPath $Out) { throw 'Use a new evidence directory' }
if (Get-Process rasterfall -ErrorAction SilentlyContinue) { throw 'Rasterfall is already running' }
if (-not (Test-Path -LiteralPath "$Package/rasterfall.exe")) { throw 'Run NativeCodex.ps1 test or run to stage the current build first' }
New-Item -ItemType Directory -Path $Out | Out-Null
$Encoding=[Text.UTF8Encoding]::new($false)
function Write-Json($Value,[string]$Name) {
    [IO.File]::WriteAllText((Join-Path $Out $Name),(ConvertTo-Json -InputObject $Value -Depth 8),$Encoding)
}
$Keys=@('RF_PERF_LAB_AUTORUN','RF_PERF_LAB_SCOPE','RF_PERF_LAB_UNCAPPED',
    'RF_PERF_LAB_INTERFERENCE','RF_GPU_SCENE_LEGACY_DISPLAY_GEOMETRY','VK_INSTANCE_LAYERS',
    'RF_GPU_SCENE_LEGACY_RETAINED_LAYERS','RF_GPU_SCENE_LEGACY_POSE_REUSE',
    'RF_GPU_SCENE_LEGACY_ENEMY_CACHE','RF_GPU_SCENE_LEGACY_SNAPSHOT_CACHE',
    'RF_GPU_SCENE_PROFILE_SLOW','RF_GPU_SCENE_PROFILE_LAYERS','RF_GPU_PROFILE_TRIANGLE_UPDATE')
$Saved=@{}
foreach ($Key in $Keys) { $Saved[$Key]=[Environment]::GetEnvironmentVariable($Key,'Process') }
$Runs=[Collections.Generic.List[object]]::new()
$Process=$null
$Files=@('rasterfall.exe','rasterfall/assets/maps/outpost.map',
    'rasterfall/assets/maps/performance_empty.map','rasterfall/assets/maps/performance_components.map',
    'rasterfall/assets/worlds/performance.content','rasterfall/assets/worlds/outpost.content',
    'rasterfall/assets/maps/frontier_station_01.map','rasterfall/assets/worlds/frontier_station_01.content')
$Hashes=@($Files | ForEach-Object { Get-FileHash -LiteralPath (Join-Path $Package $_) })
Write-Json $Hashes 'hashes.json'
$Argv=@('--skip-boot','--gpu-scene-play','--map','rasterfall/assets/maps/outpost.map')
if($Width) {$Argv+=@('--window-size',[string]$Width,[string]$Height)}
Write-Json @{rounds=$Rounds;stage=$Stage;scenes=$Scenes;capped=[bool]$Capped;compare_geometry=[bool]$CompareGeometry;compare_backend=[bool]$CompareBackend;profile_slow=[bool]$ProfileSlow;gpu_vendor=$env:RF_GPU_VULKAN_VENDOR_ID;sky_time=$env:RF_GPU_SKY_TIME;sky_scale=$env:RF_GPU_SKY_SCALE;argv=$Argv;validation=$false} 'config.json'
$Cases=[Collections.Generic.List[object]]::new()
foreach ($Scene in ($Scenes | Select-Object -Unique)) {
    if($Scene -gt 4) {$Cases.Add(@{scene=$Scene;scope='full';interference=0});continue}
    if($Stage -in @('Full','All')) {$Cases.Add(@{scene=$Scene;scope='full';interference=0})}
    if ($Stage -in @('Isolated','All')) { $Cases.Add(@{scene=$Scene;scope='isolated';interference=0}) }
    if ($Stage -in @('Interference','All')) {
        $Cases.Add(@{scene=$Scene;scope='outpost';interference=1})
        $Cases.Add(@{scene=$Scene;scope='outpost';interference=0})
    }
}
if($CompareGeometry -or $CompareBackend) {
    $Pairs=[Collections.Generic.List[object]]::new()
    foreach($Case in $Cases) {foreach($Legacy in @(1,0)) {
        $Pairs.Add(@{scene=$Case.scene;scope=$Case.scope;interference=$Case.interference;legacy=$Legacy})
    }}
    $Cases=$Pairs
}
try {
    [Environment]::SetEnvironmentVariable('VK_INSTANCE_LAYERS',$null,'Process')
    $env:RF_GPU_SCENE_PROFILE_SLOW=if($ProfileSlow){'1'}else{'0'}
    $env:RF_GPU_SCENE_PROFILE_LAYERS='0'
    $env:RF_GPU_PROFILE_TRIANGLE_UPDATE='0'
    for ($Round=1;$Round -le $Rounds;$Round++) {
        $Ordered=@($Cases)
        if ($Round%2 -eq 0) { [array]::Reverse($Ordered) }
        foreach ($Case in $Ordered) {
            $Name="r$Round-s$($Case.scene)-$($Case.scope)-interference$($Case.interference)"
            if($CompareGeometry -or $CompareBackend) {$Name+="-legacy$($Case.legacy)"}
            $env:RF_PERF_LAB_AUTORUN=[string]$Case.scene
            $env:RF_PERF_LAB_SCOPE=$Case.scope
            $env:RF_PERF_LAB_UNCAPPED=if ($Capped) {'0'} else {'1'}
            $env:RF_PERF_LAB_INTERFERENCE=[string]$Case.interference
            $env:RF_GPU_SCENE_LEGACY_DISPLAY_GEOMETRY=if($CompareGeometry){[string]$Case.legacy}else{'0'}
            # Keep the CPU-only geometry comparison meaningful with GPU retention enabled by default.
            $env:RF_GPU_SCENE_LEGACY_RETAINED_LAYERS=if($CompareGeometry){'1'}elseif($CompareBackend){[string]$Case.legacy}else{'0'}
            foreach($Key in @('RF_GPU_SCENE_LEGACY_POSE_REUSE','RF_GPU_SCENE_LEGACY_ENEMY_CACHE','RF_GPU_SCENE_LEGACY_SNAPSHOT_CACHE')) {
                [Environment]::SetEnvironmentVariable($Key,$(if($CompareBackend){[string]$Case.legacy}else{'0'}),'Process')
            }
            Write-Host "[PERF-LAB] $Name starting"
            $Process=Start-Process -FilePath "$Package/rasterfall.exe" -WorkingDirectory $Package `
                -ArgumentList $Argv -PassThru -WindowStyle Hidden `
                -RedirectStandardOutput "$Out/$Name.out" -RedirectStandardError "$Out/$Name.err"
            $Handle=$Process.Handle
            $Timer=[Diagnostics.Stopwatch]::StartNew()
            while (-not $Process.WaitForExit(500)) {
                if ($Timer.Elapsed.TotalSeconds -gt 180) {
                    Stop-Process -Id $Process.Id -Force
                    throw "$Name timed out"
                }
            }
            $Process.WaitForExit()
            $Log=[string](Get-Content -Encoding UTF8 -LiteralPath "$Out/$Name.out" -Raw)
            $ErrorLog=[string](Get-Content -Encoding UTF8 -LiteralPath "$Out/$Name.err" -Raw)
            if (Test-Path -LiteralPath "$Package/rasterfall.log") {
                Copy-Item -LiteralPath "$Package/rasterfall.log" -Destination "$Out/$Name.rasterfall.log"
            }
            if ($Process.ExitCode -ne 0 -or $ErrorLog -match 'VUID-|Validation Error|SYNC-HAZARD') {
                throw "$Name failed; inspect stdout/stderr/runtime log"
            }
            $Result=[regex]::Match($Log,'PERF-LAB result ([^\r\n]+)')
            $Config=[regex]::Match($Log,'PERF-LAB config ([^\r\n]+)')
            if (-not $Result.Success -or -not $Config.Success) { throw "$Name missing result/config" }
            $Values=@{}
            $Stages=[regex]::Match($Log,'PERF-LAB stages ([^\r\n]+)')
            $Preparation=[regex]::Match($Log,'PERF-LAB preparation ([^\r\n]+)')
            $Live=[regex]::Match($Log,'PERF-LAB live ([^\r\n]+)')
            foreach ($Match in [regex]::Matches($Result.Value+' '+$Config.Value+' '+$Stages.Value+' '+$Preparation.Value+' '+$Live.Value,'(\w+)=([^\s]+)')) {
                $Values[$Match.Groups[1].Value]=$Match.Groups[2].Value
            }
            $Points=@([regex]::Matches($Log,'PERF-LAB point ([^\r\n]+)') | ForEach-Object {
                $Point=@{}
                foreach($Field in [regex]::Matches($_.Groups[1].Value,'(\w+)=([^\s]+)')) {
                    $Point[$Field.Groups[1].Value]=$Field.Groups[2].Value
                }
                $Point
            })
            if($Case.scene -in @(5,6,10) -and ($Points.Count -ne 5 -or
                @($Points | Where-Object {[int]$_.frames -lt 1}).Count -or $Values.scope -ne 'full')) {
                throw "$Name incomplete panorama"
            }
            if($Case.scene -ge 7 -and (!$Live.Success -or [int]$Values.ticks -lt 1 -or
                [int]$Values.remaining_actors -ne 0 -or $Values.scope -ne 'full')) {throw "$Name missing live trial/cleanup evidence"}
            if($Case.scene -in @(7,8) -and [int]$Values.combat_frames -lt 1) {throw "$Name never sampled active combat"}
            if($Case.scene -eq 9 -and ($Values.route_done -ne '1' -or $Values.route_legs -ne '4')) {throw "$Name route did not complete"}
            if ($Values.valid -ne '1' -or [int]$Values.frames -lt 1 -or
                [int]$Values.remaining -ne 0 -or [int]$Values.gpu_samples -lt 1) {
                throw "$Name invalid workload or missing GPU timing"
            }
            if($Width -and $Values.extent -ne "${Width}x${Height}") {throw "$Name extent differs from requested window size"}
            if ($Case.scope -eq 'isolated' -and ($Values.lights -ne '0' -or $Values.shadow_maps -ne '3')) {
                throw "$Name isolated lighting contract failed"
            }
            $Slow=@([regex]::Matches($Log,'SCENE-SLOW ([^\r\n]+)') | ForEach-Object {
                $Fields=@{}
                foreach($Field in [regex]::Matches($_.Groups[1].Value,'(\w+)=([^\s]+)')) {
                    $Fields[$Field.Groups[1].Value]=$Field.Groups[2].Value
                }
                $Fields
            })
            if($ProfileSlow -and !$Slow.Count) {throw "$Name missing slow-frame trace"}
            $Runs.Add(@{name=$Name;round=$Round;case=$Case;values=$Values;points=$Points;slow_frames=$Slow;exit_code=$Process.ExitCode})
            Write-Json @($Runs.ToArray()) 'report.json'
            $Process=$null
            Write-Host "[PERF-LAB] $Name $($Result.Value)"
        }
    }
    foreach ($Hash in $Hashes) {
        if ((Get-FileHash -LiteralPath $Hash.Path).Hash -ne $Hash.Hash) { throw 'Build/assets changed during sampling' }
    }
} finally {
    if ($Process -and -not $Process.HasExited) { Stop-Process -Id $Process.Id -Force }
    foreach ($Key in $Keys) { [Environment]::SetEnvironmentVariable($Key,$Saved[$Key],'Process') }
}
