[CmdletBinding()]
param(
    [string]$OutputDirectory='tmp/outpost-performance',
    [string]$Executable='',
    [ValidateRange(1,5)][int]$Rounds=5,
    [ValidateRange(120,4096)][int]$Samples=360,
    [string[]]$Views=@('sky-north','lab-computer-side','electronics-lab','lighting-lab'),
    [switch]$Compare,
    [switch]$ComparePreparation,
    [switch]$CompareArchitecture,
    [switch]$CompareArchitectureShadows,
    [switch]$CompareLightTiles,
    [switch]$CompareLightDepth,
    [switch]$CompareGIReuse,
    [switch]$CompareDynamicCull,
    [switch]$ComparePoseReuse,
    [switch]$CompareRaySingle,
    [switch]$CompareLightClusters,
    [switch]$CompareSkinFusion,
    [switch]$CompareReceiverFP16,
    [switch]$CompareDepthPrepass,
    [switch]$CompareRoofCache,
    [switch]$CompareGI,
    [switch]$CompareGIBounces,
    [switch]$CompareGIVisibility,
    [switch]$CompareDaylight,
    [switch]$CompareIndirect,
    [switch]$CompareIndirectMode,
    [switch]$LightAblations,
    [ValidateSet('none','roof','sun','local','pcf','brdf','rays','receiver','receiver_read')]
    [string[]]$AblationModes=@('none','roof','sun','local','pcf','brdf','rays','receiver','receiver_read'),
    [ValidateRange(0,120)][int]$CooldownSeconds=0,
    [switch]$ProfileLights,
    [switch]$AllLabs,
    [ValidateRange(640,7680)][int]$Width=1920,
    [ValidateRange(480,4320)][int]$Height=1080
)
$ErrorActionPreference='Stop'
if(([int][bool]$Compare+[int][bool]$ComparePreparation+[int][bool]$CompareArchitecture+[int][bool]$CompareArchitectureShadows+[int][bool]$CompareLightTiles+[int][bool]$CompareLightDepth+[int][bool]$CompareGIReuse+[int][bool]$CompareDynamicCull+[int][bool]$ComparePoseReuse+[int][bool]$CompareRaySingle+[int][bool]$CompareLightClusters+[int][bool]$CompareSkinFusion+[int][bool]$CompareReceiverFP16+[int][bool]$CompareDepthPrepass+[int][bool]$CompareRoofCache+[int][bool]$CompareGI+[int][bool]$CompareGIBounces+[int][bool]$CompareGIVisibility+[int][bool]$CompareDaylight+[int][bool]$CompareIndirect+[int][bool]$CompareIndirectMode+[int][bool]$LightAblations) -gt 1){throw 'Choose one comparison axis'}
if($ProfileLights -and $LightAblations){throw 'Keep fragment counters separate from timing ablations'}
if($AllLabs -and !$PSBoundParameters.ContainsKey('Views')) {
    $Views=@('sky-north','electronics-lab','lighting-lab')
}
if($AllLabs -and @($Views | Where-Object {$_ -like 'lab-computer*'}).Count) {
    throw 'Computer inspection views override exhibit switches; use electronics-case or electronics-lab for all-labs sampling'
}
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Exe=if($Executable){
    $Target=if([IO.Path]::IsPathRooted($Executable)){$Executable}else{Join-Path $Root $Executable}
    (Resolve-Path -LiteralPath $Target).Path
}else{Join-Path $Package 'rasterfall.exe'}
$Package=Split-Path -Parent $Exe
$Out=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
if(Test-Path -LiteralPath $Out){throw 'Use a new evidence directory'}
if(Get-Process rasterfall -ErrorAction SilentlyContinue){throw 'Rasterfall already running'}
New-Item -ItemType Directory -Path $Out | Out-Null
$Utf8=[Text.UTF8Encoding]::new($false)
function Write-Json($Value,[string]$Name){
    [IO.File]::WriteAllText((Join-Path $Out $Name),(ConvertTo-Json -InputObject $Value -Depth 8),$Utf8)
}
$SavedPath=$env:Path
$Keys=@('RF_SCENE_PERF_FRAMES','RF_LABS_ALL','RF_GPU_SCENE_LEGACY_LAYER_COLORS',
    'RF_GPU_SCENE_DISABLE_LAYER_CULL','RF_GPU_SCENE_DISABLE_DRAW_CULL','RF_GPU_SCENE_LEGACY_BIND',
    'RF_GPU_SCENE_LEGACY_ORIGIN_BOUNDS','RF_GPU_SCENE_LEGACY_LAYER_PACKING',
    'RF_GPU_VULKAN_VENDOR_ID','RF_GPU_SKY_TIME','RF_GPU_SKY_SCALE','VK_INSTANCE_LAYERS','RF_GPU_ARCHITECTURE',
    'RF_GPU_ARCHITECTURE_SHADOW_MAPS','RF_GPU_LIGHT_TILES','RF_GPU_LIGHT_ABLATION','RF_GPU_LIGHT_PROFILE','RF_GPU_ROOF_CACHE','RF_GPU_GI','RF_GPU_GI_BOUNCES','RF_GPU_GI_VISIBILITY','RF_GPU_GI_DEBUG','RF_GPU_HDR_CAPTURE','RF_GPU_DAYLIGHT','RF_GPU_LIGHT_DEPTH','RF_GPU_GI_REUSE','RF_GPU_DEPTH_PREPASS','RF_GPU_INDIRECT_MODE','RF_GPU_DYNAMIC_CULL','RF_GPU_CPU_POSE_REUSE','RF_GPU_RAY_SINGLE','RF_GPU_LIGHT_CLUSTERS','RF_GPU_SKIN_FUSED','RF_GPU_RECEIVER_FP32')
$Saved=@{}
foreach($Key in $Keys){$Saved[$Key]=[Environment]::GetEnvironmentVariable($Key,'Process')}
$Runs=[Collections.Generic.List[object]]::new()
$Process=$null
$Hash=(Get-FileHash -LiteralPath $Exe).Hash
Write-Json @{exe=$Hash;executable=$Exe;map=(Get-FileHash -LiteralPath "$Package/rasterfall/assets/maps/outpost.map").Hash;
    rounds=$Rounds;samples=$Samples;views=$Views;all_labs=[bool]$AllLabs;compare=[bool]$Compare;
    cooldown_seconds=$CooldownSeconds;ablation_modes=$AblationModes;
    compare_preparation=[bool]$ComparePreparation;compare_architecture=[bool]$CompareArchitecture;gpu_vendor=$env:RF_GPU_VULKAN_VENDOR_ID;
    compare_architecture_shadows=[bool]$CompareArchitectureShadows;architecture=$env:RF_GPU_ARCHITECTURE;
    compare_light_tiles=[bool]$CompareLightTiles;light_ablations=[bool]$LightAblations;profile_lights=[bool]$ProfileLights;
    compare_roof_cache=[bool]$CompareRoofCache;compare_gi=[bool]$CompareGI;compare_gi_bounces=[bool]$CompareGIBounces;compare_indirect=[bool]$CompareIndirect;
    compare_light_depth=[bool]$CompareLightDepth;compare_gi_reuse=[bool]$CompareGIReuse;
    compare_depth_prepass=[bool]$CompareDepthPrepass;depth_prepass=$env:RF_GPU_DEPTH_PREPASS;
    compare_dynamic_cull=[bool]$CompareDynamicCull;dynamic_cull=$env:RF_GPU_DYNAMIC_CULL;
    compare_pose_reuse=[bool]$ComparePoseReuse;pose_reuse=$env:RF_GPU_CPU_POSE_REUSE;
    compare_ray_single=[bool]$CompareRaySingle;ray_single=$env:RF_GPU_RAY_SINGLE;
    compare_light_clusters=[bool]$CompareLightClusters;compare_skin_fusion=[bool]$CompareSkinFusion;compare_receiver_fp16=[bool]$CompareReceiverFP16;
    light_clusters=$env:RF_GPU_LIGHT_CLUSTERS;skin_fused=$env:RF_GPU_SKIN_FUSED;receiver_fp32=$env:RF_GPU_RECEIVER_FP32;
    light_depth=$env:RF_GPU_LIGHT_DEPTH;gi_reuse=$env:RF_GPU_GI_REUSE;
    daylight=$env:RF_GPU_DAYLIGHT;compare_daylight=[bool]$CompareDaylight;
    indirect_mode=$env:RF_GPU_INDIRECT_MODE;compare_indirect_mode=[bool]$CompareIndirectMode;
    spirv=(Get-FileHash -LiteralPath (Join-Path $Root 'gpu/src/rf_gpu_graphics_spirv.inc')).Hash;
    source_head=(& git -C $Root rev-parse HEAD);
    gi_bounces=$env:RF_GPU_GI_BOUNCES;
    gi_visibility=$env:RF_GPU_GI_VISIBILITY;compare_gi_visibility=[bool]$CompareGIVisibility;
    roof_cache=$env:RF_GPU_ROOF_CACHE;gi=$env:RF_GPU_GI;
    width=$Width;height=$Height;warmup=120;sky_scale=4;sky_time=0;clock='realtime';cap=120} 'config.json'
try {
    [Environment]::SetEnvironmentVariable('PATH',$null,'Process')
    [Environment]::SetEnvironmentVariable('Path',$SavedPath,'Process')
    $env:RF_SCENE_PERF_FRAMES=[string]$Samples
    $env:RF_LABS_ALL=if($AllLabs){'1'}else{'0'}
    $env:RF_GPU_SKY_TIME='0';$env:RF_GPU_SKY_SCALE='4'
    $env:RF_GPU_GI_DEBUG='0'
    if($CompareGIReuse){$env:RF_GPU_GI='1';$env:RF_GPU_DAYLIGHT='1';$env:RF_GPU_GI_VISIBILITY='1'}
    if($CompareRoofCache){$env:RF_GPU_DAYLIGHT='0'}
    if($CompareIndirectMode){$env:RF_GPU_GI='1';$env:RF_GPU_DAYLIGHT='1';$env:RF_GPU_GI_VISIBILITY='1'}
    [Environment]::SetEnvironmentVariable('RF_GPU_HDR_CAPTURE',$null,'Process')
    $env:RF_GPU_SCENE_LEGACY_BIND='0'
    $env:RF_GPU_LIGHT_PROFILE=if($ProfileLights){'1'}else{'0'}
    [Environment]::SetEnvironmentVariable('VK_INSTANCE_LAYERS',$null,'Process')
    for($Round=1;$Round -le $Rounds;$Round++) {
        foreach($View in $Views) {
            $Modes=if($Compare -or $ComparePreparation -or $CompareArchitectureShadows -or $CompareLightTiles -or $CompareLightDepth -or $CompareGIReuse -or $CompareDynamicCull -or $ComparePoseReuse -or $CompareRaySingle -or $CompareLightClusters -or $CompareSkinFusion -or $CompareReceiverFP16 -or $CompareDepthPrepass -or $CompareRoofCache -or $CompareGI -or $CompareGIBounces -or $CompareGIVisibility -or $CompareDaylight -or $CompareIndirect){@('reference','optimized')}else{@('optimized')}
            if($CompareArchitecture){$Modes=@('software','hardware')}
            if($LightAblations){$Modes=@($AblationModes)}
            if($CompareIndirectMode){
                $Cycle=@('reference','fast','direct_only_diag');$Modes=@()
                for($Offset=0;$Offset -lt 3;$Offset++){$Modes+=$Cycle[($Round-1+$Offset)%3]}
            }
            if($Round%2 -eq 0){[array]::Reverse($Modes)}
            foreach($Mode in $Modes) {
                $Name="r$Round-$View-$Mode"
                for($Idle=0;$Idle -lt $CooldownSeconds;$Idle++){Start-Sleep -Seconds 1}
                if($CompareIndirectMode){$env:RF_GPU_INDIRECT_MODE=$Mode}
                $Thermal=[Collections.Generic.List[string]]::new();$LastThermal=-2
                if($CompareLightDepth){$env:RF_GPU_LIGHT_DEPTH=if($Mode -eq 'reference'){'0'}else{'1'}}
                if($CompareDepthPrepass){$env:RF_GPU_DEPTH_PREPASS=if($Mode -eq 'reference'){'0'}else{'1'}}
                if($CompareDynamicCull){$env:RF_GPU_DYNAMIC_CULL=if($Mode -eq 'reference'){'0'}else{'1'}}
                if($ComparePoseReuse){$env:RF_GPU_CPU_POSE_REUSE=if($Mode -eq 'reference'){'0'}else{'1'}}
                if($CompareRaySingle){$env:RF_GPU_RAY_SINGLE=if($Mode -eq 'reference'){'0'}else{'1'}}
                if($CompareLightClusters){$env:RF_GPU_LIGHT_DEPTH='1';$env:RF_GPU_LIGHT_CLUSTERS=if($Mode -eq 'reference'){'0'}else{'1'}}
                if($CompareSkinFusion){$env:RF_GPU_SKIN_FUSED=if($Mode -eq 'reference'){'0'}else{'1'}}
                if($CompareReceiverFP16){$env:RF_GPU_INDIRECT_MODE='fast';$env:RF_GPU_RECEIVER_FP32=if($Mode -eq 'reference'){'1'}else{'0'}}
                if($CompareGIReuse){$env:RF_GPU_GI_REUSE=if($Mode -eq 'reference'){'0'}else{'1'}}
                if($CompareDaylight){$env:RF_GPU_DAYLIGHT=if($Mode -eq 'reference'){'0'}else{'1'}}
                if($CompareArchitecture){$env:RF_GPU_ARCHITECTURE=$Mode}
                $env:RF_GPU_ARCHITECTURE_SHADOW_MAPS=if($CompareArchitectureShadows -and $Mode -eq 'reference'){'1'}else{'0'}
                $env:RF_GPU_LIGHT_TILES=if($CompareLightTiles -and $Mode -eq 'reference'){'0'}else{'1'}
                $env:RF_GPU_LIGHT_ABLATION=if($LightAblations){$Mode}else{'none'}
                if($CompareRoofCache -or $CompareGI -or $CompareIndirect) {
                    $env:RF_GPU_ROOF_CACHE=if(($CompareRoofCache -or $CompareIndirect) -and $Mode -eq 'reference'){'0'}else{'1'}
                    $env:RF_GPU_GI=if($CompareRoofCache -or $Mode -eq 'reference'){'0'}else{'1'}
                }
                if($CompareGIBounces) {
                    $env:RF_GPU_GI='1';$env:RF_GPU_ROOF_CACHE='1'
                    $env:RF_GPU_GI_BOUNCES=if($Mode -eq 'reference'){'1'}else{'2'}
                }
                if($CompareGIVisibility) {
                    $env:RF_GPU_GI='1';$env:RF_GPU_GI_VISIBILITY=if($Mode -eq 'reference'){'2'}else{'1'}
                }
                foreach($Key in @('RF_GPU_SCENE_LEGACY_LAYER_COLORS','RF_GPU_SCENE_DISABLE_LAYER_CULL',
                    'RF_GPU_SCENE_DISABLE_DRAW_CULL')) {
                    [Environment]::SetEnvironmentVariable($Key,$(if($Compare -and $Mode -eq 'reference'){'1'}else{'0'}),'Process')
                }
                foreach($Key in @('RF_GPU_SCENE_LEGACY_ORIGIN_BOUNDS','RF_GPU_SCENE_LEGACY_LAYER_PACKING')) {
                    [Environment]::SetEnvironmentVariable($Key,$(if($ComparePreparation -and $Mode -eq 'reference'){'1'}else{'0'}),'Process')
                }
                $Map=if($View -like 'frontier-*'){'rasterfall/assets/maps/frontier_station_01.map'}else{'rasterfall/assets/maps/outpost.map'}
                $Argv=@('--skip-boot','--renderer','gpu-scene','--map',$Map,
                    '--gpu-normal-scene',$View,'0','--window-size',[string]$Width,[string]$Height)
                $Process=Start-Process -FilePath $Exe -WorkingDirectory $Package -WindowStyle Hidden -PassThru `
                    -ArgumentList (($Argv | ForEach-Object {'"'+$_+'"'}) -join ' ') `
                    -RedirectStandardOutput "$Out/$Name.out" -RedirectStandardError "$Out/$Name.err"
                $Handle=$Process.Handle
                $Timer=[Diagnostics.Stopwatch]::StartNew()
                while(-not $Process.WaitForExit(500)) {
                    if((Get-Command nvidia-smi -ErrorAction SilentlyContinue) -and $Timer.Elapsed.TotalSeconds-$LastThermal -ge 2) {
                        $Thermal.Add([string]((& nvidia-smi --query-gpu=timestamp,temperature.gpu,power.draw,clocks.current.graphics,utilization.gpu,clocks_event_reasons.sw_thermal_slowdown --format=csv,noheader) -join '; '))
                        $LastThermal=$Timer.Elapsed.TotalSeconds
                    }
                    if($Timer.Elapsed.TotalSeconds -gt 240){throw "$Name timed out"}
                }
                $Process.WaitForExit()
                $Log=[string](Get-Content -Encoding UTF8 -LiteralPath "$Out/$Name.out" -Raw)
                $Errors=[string](Get-Content -Encoding UTF8 -LiteralPath "$Out/$Name.err" -Raw)
                $Match=[regex]::Match($Log,'SCENE-PERF ([^\r\n]+)')
                if($Process.ExitCode -ne 0 -or !$Match.Success -or $Errors -match 'VUID-|Validation Error|SYNC-HAZARD') {
                    throw "$Name failed; inspect stdout/stderr"
                }
                $Result=@{}
                if($CompareArchitecture -and $Errors -notmatch "rf-gpu-ray: requested=$Mode selected=$Mode") {
                    throw "$Name did not activate the requested architecture backend"
                }
                foreach($Field in [regex]::Matches($Match.Groups[1].Value,'(\w+)=([^ ]+)')) {
                    $Result[$Field.Groups[1].Value]=$Field.Groups[2].Value
                }
                foreach($Line in [regex]::Matches($Log,'SCENE-(?:GPU-STAGES|LIGHT-PROFILE) ([^\r\n]+)')) {
                    foreach($Field in [regex]::Matches($Line.Groups[1].Value,'(\w+)=([^ ]+)')) {
                        $Result[$Field.Groups[1].Value]=$Field.Groups[2].Value
                    }
                }
                if((!$ProfileLights -and $Result.valid -ne '1') -or [int]$Result.samples -ne $Samples -or [int]$Result.gpu_p50_us -le 0){throw "$Name invalid samples"}
                if($ProfileLights -and ($Result.diagnostic -ne '1' -or [long]$Result.shaded_mean -le 0)){throw "$Name missing diagnostic counters"}
                if($Errors -notmatch "rf-gpu-light: tiles=$($env:RF_GPU_LIGHT_TILES) profile=$($env:RF_GPU_LIGHT_PROFILE)"){throw "$Name did not activate requested light mode"}
                if(($CompareRoofCache -or $CompareGI -or $CompareIndirect) -and $Errors -notmatch "rf-gpu-indirect: roof-cache=$($env:RF_GPU_ROOF_CACHE) probe-gi=$($env:RF_GPU_GI)"){throw "$Name did not activate requested indirect mode"}
                if($CompareLightDepth -and $Errors -notmatch "rf-gpu-light: depth-slices=$(if($Mode -eq 'reference'){'1'}else{'32'})\b"){throw "$Name did not activate requested depth masks"}
                if($CompareDepthPrepass -and $Errors -notmatch "rf-gpu-scene: depth-prepass=$($env:RF_GPU_DEPTH_PREPASS)"){throw "$Name did not activate requested depth prepass"}
                if($CompareLightClusters -and $Errors -notmatch "rf-gpu-light: clusters=$($env:RF_GPU_LIGHT_CLUSTERS)"){throw "$Name did not activate requested clusters"}
                if($CompareDynamicCull -and $Errors -notmatch "rf-gpu-scene: dynamic-cull=$($env:RF_GPU_DYNAMIC_CULL)"){throw "$Name did not activate requested dynamic_cull"}
                if($ComparePoseReuse -and $Errors -notmatch "SCENE-POSE reuse=$($env:RF_GPU_CPU_POSE_REUSE)"){throw "$Name did not activate requested CPU pose reuse"}
                if($CompareRaySingle -and $Errors -notmatch "rf-gpu-light: ray-single=$($env:RF_GPU_RAY_SINGLE)"){throw "$Name did not activate requested single ray traversal"}
                if($CompareSkinFusion -and $Errors -notmatch "rf-gpu-skin: fused=$($env:RF_GPU_SKIN_FUSED)"){throw "$Name did not activate requested skin fusion"}
                if($CompareReceiverFP16 -and $Errors -notmatch "rf-gpu-receiver-cache: [^\r\n]*precision=$(if($Mode -eq 'reference'){'fp32'}else{'fp16'})"){throw "$Name did not activate requested receiver format"}
                if($CompareGIReuse -and $Errors -notmatch "rf-gpu-gi: shared-visibility=$($env:RF_GPU_GI_REUSE)"){throw "$Name did not activate requested GI reuse"}
                if($CompareDaylight -and $Errors -notmatch "rf-gpu-daylight: enabled=$($env:RF_GPU_DAYLIGHT)"){throw "$Name did not activate requested daylight mode"}
                if($CompareGIBounces -and $Errors -notmatch "rf-gpu-indirect: roof-cache=1 probe-gi=1 bounces=$($env:RF_GPU_GI_BOUNCES)"){throw "$Name did not activate requested bounce count"}
                if($CompareGIVisibility -and $Errors -notmatch "rf-gpu-gi: [^\r\n]*visibility=$($env:RF_GPU_GI_VISIBILITY)"){throw "$Name did not activate requested visibility mode"}
                if($CompareIndirectMode -and $Errors -notmatch "rf-gpu-indirect-mode: $Mode\b"){throw "$Name did not activate requested indirect mode"}
                if($Result.extent -ne "${Width}x${Height}"){throw "$Name extent differs from requested window size"}
                $Runs.Add(@{round=$Round;view=$View;mode=$Mode;result=$Result;argv=$Argv;map_sha256=(Get-FileHash -LiteralPath (Join-Path $Package $Map)).Hash;thermal=@($Thermal.ToArray());exit=$Process.ExitCode})
                Write-Json @($Runs.ToArray()) 'report.json'
                Write-Output "$Name p50=$($Result.p50_us) p95=$($Result.p95_us) p99=$($Result.p99_us) us; GPU=$($Result.gpu_p50_us) shadow=$($Result.shadow_p50_us) main=$($Result.main_p50_us) us; shadow_draws=$($Result.shadow_draws)"
                $Process=$null
            }
        }
    }
    if((Get-FileHash -LiteralPath $Exe).Hash -ne $Hash){throw 'Executable changed while sampling'}
} finally {
    if($Process -and !$Process.HasExited){Stop-Process -Id $Process.Id -Force}
    foreach($Key in $Keys){[Environment]::SetEnvironmentVariable($Key,$Saved[$Key],'Process')}
    [Environment]::SetEnvironmentVariable('Path',$SavedPath,'Process')
}
