[CmdletBinding()]
param([string]$OutputDirectory='', [string]$ValidationLayerDirectory='',
    [ValidatePattern('^$|^[0-9a-fA-F]{4}$')][string]$DeviceVendor='')
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
if (-not $OutputDirectory) { $OutputDirectory=Join-Path $Root ('tmp/scene-native-'+(Get-Date -Format 'yyyyMMdd-HHmmss')) }
$Out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $Out){throw 'Use a new evidence directory.'}
New-Item -ItemType Directory -Path $Out | Out-Null
$SavedPath=$env:Path
[Environment]::SetEnvironmentVariable('PATH',$null,'Process')
[Environment]::SetEnvironmentVariable('Path',$SavedPath,'Process')
$Keys=@('VK_LAYER_PATH','VK_INSTANCE_LAYERS','VK_VALIDATION_VALIDATE_SYNC','VK_LAYER_REPORT_FLAGS','VK_LOADER_DEBUG','RF_GPU_VULKAN_VENDOR_ID')
$Saved=@{};foreach($Key in $Keys){$Saved[$Key]=[Environment]::GetEnvironmentVariable($Key,'Process')}
$Runs=[Collections.Generic.List[object]]::new()
$Manifest=[ordered]@{result='FAIL'; validation_sync='NOT RUN - layer not requested'; requested_vendor=$DeviceVendor; intel_native='NOT VERIFIED'; executable_sha256=(Get-FileHash "$Package/rasterfall.exe").Hash; runs=$Runs}
function Run([string]$Name,[string[]]$Argv,[int]$Expected=0,[int]$Frames=0,[string]$Fault='') {
    if(Get-Process rasterfall -ErrorAction SilentlyContinue){throw 'Another Rasterfall process is running.'}
    $Quoted=($Argv | ForEach-Object {'"'+$_+'"'}) -join ' '
    $p=Start-Process -FilePath "$Package/rasterfall.exe" -WorkingDirectory $Package -ArgumentList $Quoted -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput "$Out/$Name.out" -RedirectStandardError "$Out/$Name.err"
    $Runs.Add([ordered]@{name=$Name; exit_code=$p.ExitCode; argv=$Argv})
    $Manifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 "$Out/manifest.json"
    if($p.ExitCode -ne $Expected){throw "$Name exit $($p.ExitCode), expected $Expected"}
    $Log=(Get-Content -Encoding UTF8 "$Out/$Name.out","$Out/$Name.err" | Out-String)
    if($Log -match 'Validation Error|SYNC-HAZARD|VUID-'){throw "$Name validation error"}
    if($Name -like 'scene-*') {
        if($Log -notmatch 'SCENE static-prop-clip=PASS wide=[1-9][0-9]* near_crossing=[1-9][0-9]*'){throw "$Name missing static prop clipping proof"}
        if($Log -notmatch 'SCENE occlusion=PASS' -or $Log -notmatch 'SCENE cleanup live=0 retired=0 pinned=0'){throw "$Name incomplete diagnostics/cleanup"}
        if($Log -notmatch "SCENE result=$Expected rendered=$Frames bridge=0 "){throw "$Name incomplete native frames"}
        if($DeviceVendor -and $Log -notmatch "SCENE adapter=.+ vendor=$DeviceVendor device="){throw "$Name wrong device"}
        if($Expected -and $Log -notmatch 'pins_held_until_drain=3'){throw "$Name early pin release"}
        if($Fault -and $Log -notmatch "fault-injection=$Fault frame=2"){throw "$Name missing fault"}
        $SyncEnabled=($Log -match 'CURRENT-VALIDATION-ENABLED' -and $Log -match ' - Synchronization') -or
            ($Log -match 'Khronos Validation Layer Active:' -and $Log -match 'Current Enables:.*VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT')
        if($ValidationLayerDirectory -and ($Log -notmatch 'Insert instance layer.*VK_LAYER_KHRONOS_validation' -or -not $SyncEnabled)){throw "$Name layer/sync activation not proven"}
    }
    Write-Host "[SCENE] $Name PASS"
}
function CheckActorPixels([string]$Name,[int[]]$Points) {
    $Bmp=[IO.File]::ReadAllBytes("$Out/$Name.bmp")
    $Ppm=[IO.File]::ReadAllBytes("$Out/$Name.bmp.scene.ppm")
    $Width=[BitConverter]::ToInt32($Bmp,18)
    $Height=[BitConverter]::ToInt32($Bmp,22)
    $Offset=[BitConverter]::ToInt32($Bmp,10)
    $Header=[Text.Encoding]::ASCII.GetBytes("P6`n$Width $(-$Height)`n255`n")
    if($Width -ne 1280 -or $Height -ne -720 -or
       [BitConverter]::ToInt16($Bmp,28) -ne 32 -or
       $Bmp.Length -lt $Offset+$Width*(-$Height)*4 -or
       $Ppm.Length -ne $Header.Length+$Width*(-$Height)*3) {
        throw "$Name invalid mixed/Scene capture format"
    }
    for($i=0;$i -lt $Header.Length;$i++) {
        if($Ppm[$i] -ne $Header[$i]){throw "$Name invalid Scene PPM header"}
    }
    for($i=0;$i -lt $Points.Length;$i+=2) {
        $X=$Points[$i];$Y=$Points[$i+1]
        $Pixel=$Y*$Width+$X
        $B=$Offset+$Pixel*4
        $P=$Header.Length+$Pixel*3
        if($Bmp[$B+2] -ne $Ppm[$P] -or
           $Bmp[$B+1] -ne $Ppm[$P+1] -or
           $Bmp[$B] -ne $Ppm[$P+2]) {
            throw "$Name RGB mismatch at ($X,$Y)"
        }
    }
}
try {
    $env:RF_GPU_VULKAN_VENDOR_ID=$DeviceVendor
    if($ValidationLayerDirectory) {
        $ValidationLayerDirectory=(Resolve-Path -LiteralPath $ValidationLayerDirectory).Path
        if(-not(Test-Path "$ValidationLayerDirectory/VkLayer_khronos_validation.json")){throw 'Validation layer manifest missing.'}
        $env:Path="$ValidationLayerDirectory;C:\msys64\mingw64\bin;$SavedPath"
        $env:VK_LAYER_PATH=$ValidationLayerDirectory;$env:VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation'
        $env:VK_VALIDATION_VALIDATE_SYNC='true';$env:VK_LAYER_REPORT_FLAGS='error,warn,info';$env:VK_LOADER_DEBUG='layer'
        $Manifest.validation_layer=@(Get-FileHash "$ValidationLayerDirectory/VkLayer_khronos_validation.json","$ValidationLayerDirectory/VkLayer_khronos_validation.dll" | Select-Object Path,Hash)
    }
    Run 'scene-lifecycle' @('--gpu-scene-native-fixture','--frames','120') 0 120
    $Log=Get-Content -Encoding UTF8 "$Out/scene-lifecycle.out" | Out-String
    if($Log -notmatch 'resource-growth object=1 vertices=10200' -or $Log -notmatch 'world-retirement PASS'){throw 'Missing growth/world retirement'}
    if($Log -notmatch 'SCENE cpu-backing=PASS reused_after_growth=1 reused_after_world_retire=1'){throw 'Missing CPU backing reuse'}
    if($Log -notmatch 'SCENE world-gpu=PASS world_items=11 opaque=8 models=6 draws=28 covered=[1-9][0-9]* cache_uploads=28 cache_hits=28 retired_releases=28'){throw 'Missing Runtime Map GPU cache/draw/retirement evidence'}
    $Times=[regex]::Matches($Log,'SCENE gpu-time frame=(\d+) supported=(\d) valid=(\d) world_draw_ms=([0-9.]+) present_blit_ms=([0-9.]+)')
    if($Times.Count -ne 119){throw "Expected 119 Scene GPU timings, got $($Times.Count)"}
    for($i=0;$i -lt $Times.Count;$i++) {
        if([int]$Times[$i].Groups[1].Value -ne $i+2 -or
           $Times[$i].Groups[2].Value -ne '1' -or $Times[$i].Groups[3].Value -ne '1') {
            throw "Invalid Scene GPU timing at sample $i"
        }
    }
    $Extents=@([regex]::Matches($Log,'extent=(\d+x\d+)') | ForEach-Object {$_.Groups[1].Value} | Sort-Object -Unique)
    if($Extents.Count -lt 2){throw 'Missing resize evidence'}
    foreach($Name in @('scene-native','scene-object-0','scene-object-1','scene-object-2')){Copy-Item -LiteralPath "$Package/$Name.ppm" -Destination "$Out/$Name.ppm"}
    foreach($Fault in @('acquire-out-of-date','record-failure','submit-failure','present-out-of-date','present-suboptimal')) {
        $Failure=$Fault -in @('record-failure','submit-failure')
        $Code=if($Failure){3}else{0};$Count=if($Failure){1}else{4}
        Run "scene-$Fault" @('--gpu-scene-native-fixture','--frames','4','--gpu-present-fault',$Fault,'2') $Code $Count $Fault
    }
    if($ValidationLayerDirectory){$Manifest.validation_sync='PASS'}
    if($DeviceVendor -eq '8086'){$Manifest.intel_native='PASS'}
    Run 'pose' @('--gpu-scene-pose-test')
    Run 'logic' @('--logic-test')
    Run 'normal-native' @('--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene','near','0','--gpu-normal-fixed-tick','--frame-audit','--frames','3')
    $Log=Get-Content -Encoding UTF8 "$Out/normal-native.out" | Out-String
    if($Log -notmatch 'SCENE-LOCAL frame=1 .*prop_payload=134 prop_opaque=21' -or
       $Log -notmatch 'SCENE-WORLD-GPU frame=1 draws=1241 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=16 pickup_model_items=7 pickup_procedural_draws=111 pickup_procedural_items=38 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=959 hits=20 prop_assets=23 prop_draws=57 prop_culled=98 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1' -or
       $Log -notmatch 'SCENE-WORLD-GPU frame=2 draws=1241 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=16 pickup_model_items=7 pickup_procedural_draws=111 pickup_procedural_items=38 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=0 hits=979 prop_assets=23 prop_draws=57 prop_culled=98 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1' -or
       $Log -notmatch 'SCENE-WORLD-GPU frame=3 draws=1241 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=16 pickup_model_items=7 pickup_procedural_draws=111 pickup_procedural_items=38 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=0 hits=979 prop_assets=23 prop_draws=57 prop_culled=98 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1') {
        throw 'Missing normal-frame frozen map GPU cache reuse evidence'
    }
    Run 'normal-map-wall' @('--map','rasterfall/assets/maps/gpu_scene_render_fixture.map','--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene','map-wall','0','--gpu-normal-fixed-tick','--frame-audit','--frames','2')
    $Log=Get-Content -Encoding UTF8 "$Out/normal-map-wall.out" | Out-String
    if($Log -notmatch 'SCENE-WORLD-GPU frame=1 draws=163 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=0 pickup_model_items=0 pickup_procedural_draws=0 pickup_procedural_items=0 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=28 hits=0 prop_assets=0 prop_draws=0 prop_culled=0 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1' -or
       $Log -notmatch 'SCENE-WORLD-GPU frame=2 draws=163 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=0 pickup_model_items=0 pickup_procedural_draws=0 pickup_procedural_items=0 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=0 hits=28 prop_assets=0 prop_draws=0 prop_culled=0 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1') {
        throw 'Missing visible normal-frame map GPU draw/cache reuse evidence'
    }
    Run 'normal-map-sign' @('--map','rasterfall/assets/maps/gpu_scene_render_fixture.map','--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene','map-sign','0','--gpu-normal-fixed-tick','--frame-audit','--frames','2')
    $Log=Get-Content -Encoding UTF8 "$Out/normal-map-sign.out" | Out-String
    if($Log -notmatch 'SCENE-WORLD-GPU frame=1 draws=163 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=0 pickup_model_items=0 pickup_procedural_draws=0 pickup_procedural_items=0 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=28 hits=0 prop_assets=0 prop_draws=0 prop_culled=0 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1' -or
       $Log -notmatch 'SCENE-WORLD-GPU frame=2 draws=163 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=0 pickup_model_items=0 pickup_procedural_draws=0 pickup_procedural_items=0 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=0 hits=28 prop_assets=0 prop_draws=0 prop_culled=0 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1') {
        throw 'Missing normal-frame sign-view Scene WORLD draw/cache reuse evidence'
    }
    Run 'normal-model-legacy' @('--map','rasterfall/assets/maps/rasterfall.map','--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene','model-legacy','0','--gpu-normal-fixed-tick','--frame-audit','--frames','2')
    $Log=Get-Content -Encoding UTF8 "$Out/normal-model-legacy.out" | Out-String
    if($Log -notmatch 'SCENE-WORLD-GPU frame=1 draws=1261 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=16 pickup_model_items=7 pickup_procedural_draws=111 pickup_procedural_items=38 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=961 hits=38 prop_assets=23 prop_draws=77 prop_culled=92 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1' -or
       $Log -notmatch 'SCENE-WORLD-GPU frame=2 draws=1261 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=16 pickup_model_items=7 pickup_procedural_draws=111 pickup_procedural_items=38 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=0 hits=999 prop_assets=23 prop_draws=77 prop_culled=92 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1') {
        throw 'Missing visible legacy model Scene WORLD draw/cache reuse evidence'
    }
    Run 'normal-model-special' @('--map','rasterfall/assets/maps/rasterfall.map','--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene','model-special','0','--gpu-normal-fixed-tick','--frame-audit','--frames','2')
    $Log=Get-Content -Encoding UTF8 "$Out/normal-model-special.out" | Out-String
    if($Log -notmatch 'SCENE-LOCAL frame=1 .*map_opaque=60 map_primitives=922 map_deferred=15 map_transparent=4' -or
       $Log -notmatch 'SCENE-WORLD-GPU frame=1 draws=1277 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=16 pickup_model_items=7 pickup_procedural_draws=111 pickup_procedural_items=38 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=968 hits=47 prop_assets=23 prop_draws=93 prop_culled=86 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1' -or
       $Log -notmatch 'SCENE-WORLD-GPU frame=2 draws=1277 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=16 pickup_model_items=7 pickup_procedural_draws=111 pickup_procedural_items=38 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=0 hits=1015 prop_assets=23 prop_draws=93 prop_culled=86 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1') {
        throw 'Missing special model Scene WORLD draw/cache reuse evidence'
    }
    Run 'normal-model-infected' @('--map','rasterfall/assets/maps/rasterfall.map','--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene','model-infected','0','--gpu-normal-fixed-tick','--frame-audit','--frames','2')
    $Log=Get-Content -Encoding UTF8 "$Out/normal-model-infected.out" | Out-String
    if($Log -notmatch 'SCENE-WORLD-GPU frame=1 draws=1306 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=16 pickup_model_items=7 pickup_procedural_draws=111 pickup_procedural_items=38 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=984 hits=60 prop_assets=23 prop_draws=122 prop_culled=73 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1' -or
       $Log -notmatch 'SCENE-WORLD-GPU frame=2 draws=1306 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=16 pickup_model_items=7 pickup_procedural_draws=111 pickup_procedural_items=38 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=0 hits=1044 prop_assets=23 prop_draws=122 prop_culled=73 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1') {
        throw 'Missing infected display model Scene WORLD draw/cache reuse evidence'
    }
    Run 'normal-actor-rifleman' @('--map','rasterfall/assets/maps/rasterfall.map','--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene','actor-rifleman','0','--gpu-normal-fixed-tick','--frame-audit','--frames','2')
    $Log=Get-Content -Encoding UTF8 "$Out/normal-actor-rifleman.out" | Out-String
    if($Log -notmatch 'SCENE-WORLD-GPU frame=1 draws=1249 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=16 pickup_model_items=7 pickup_procedural_draws=111 pickup_procedural_items=38 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=955 hits=32 prop_assets=23 prop_draws=65 prop_culled=96 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1' -or
       $Log -notmatch 'SCENE-WORLD-GPU frame=2 draws=1249 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=16 pickup_model_items=7 pickup_procedural_draws=111 pickup_procedural_items=38 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=0 hits=987 prop_assets=23 prop_draws=65 prop_culled=96 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1') {
        throw 'Missing visible normal actor Scene WORLD draw evidence'
    }
    Run 'normal-actor-standard' @('--map','rasterfall/assets/maps/rasterfall.map','--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene','actor-standard','0','--gpu-normal-fixed-tick','--frame-audit','--frames','2','--gpu-frame-capture',"$Out/normal-actor-standard.bmp",'--gpu-capture-frame','1')
    $Log=Get-Content -Encoding UTF8 "$Out/normal-actor-standard.out" | Out-String
    if($Log -notmatch 'SCENE-WORLD-GPU frame=1 draws=1240 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=16 pickup_model_items=7 pickup_procedural_draws=111 pickup_procedural_items=38 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=960 hits=18 prop_assets=23 prop_draws=56 prop_culled=96 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1' -or
       $Log -notmatch 'SCENE-WORLD-GPU frame=2 draws=1240 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=16 pickup_model_items=7 pickup_procedural_draws=111 pickup_procedural_items=38 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=0 hits=978 prop_assets=23 prop_draws=56 prop_culled=96 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1') {
        throw 'Missing standard squad Scene WORLD draw/cache reuse evidence'
    }
    CheckActorPixels 'normal-actor-standard' @(350,540,450,350,810,520,639,100,650,275,653,263,663,264)
    Run 'normal-actor-assault' @('--map','rasterfall/assets/maps/rasterfall.map','--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene','actor-assault','0','--gpu-normal-fixed-tick','--frame-audit','--frames','2','--gpu-frame-capture',"$Out/normal-actor-assault.bmp",'--gpu-capture-frame','1')
    $Log=Get-Content -Encoding UTF8 "$Out/normal-actor-assault.out" | Out-String
    if($Log -notmatch 'SCENE-WORLD-GPU frame=1 draws=1286 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=16 pickup_model_items=7 pickup_procedural_draws=111 pickup_procedural_items=38 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=984 hits=40 prop_assets=23 prop_draws=102 prop_culled=78 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1' -or
       $Log -notmatch 'SCENE-WORLD-GPU frame=2 draws=1286 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=16 pickup_model_items=7 pickup_procedural_draws=111 pickup_procedural_items=38 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=0 hits=1024 prop_assets=23 prop_draws=102 prop_culled=78 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1') {
        throw 'Missing assault squad Scene WORLD draw/cache reuse evidence'
    }
    CheckActorPixels 'normal-actor-assault' @(390,500,470,380,880,530)
    Run 'normal-projectile' @('--map','rasterfall/assets/maps/rasterfall.map','--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene','projectile','0','--gpu-normal-fixed-tick','--frame-audit','--frames','2','--gpu-frame-capture',"$Out/normal-projectile.bmp",'--gpu-capture-frame','1')
    $Log=Get-Content -Encoding UTF8 "$Out/normal-projectile.out" | Out-String
    if($Log -notmatch 'SCENE-WORLD-GPU frame=1 draws=1244 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=3 pickup_model_draws=16 pickup_model_items=7 pickup_procedural_draws=111 pickup_procedural_items=38 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=959 hits=20 prop_assets=23 prop_draws=57 prop_culled=98 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1' -or
       $Log -notmatch 'SCENE-WORLD-GPU frame=2 draws=1244 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=3 pickup_model_draws=16 pickup_model_items=7 pickup_procedural_draws=111 pickup_procedural_items=38 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=0 hits=979 prop_assets=23 prop_draws=57 prop_culled=98 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1') {
        throw 'Missing projectile Scene WORLD draws or resource reuse evidence'
    }
    CheckActorPixels 'normal-projectile' @(150,480,600,480,1040,440)
    Run 'normal-pickup' @('--map','rasterfall/assets/maps/rasterfall.map','--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene','pickup','0','--gpu-normal-fixed-tick','--frame-audit','--frames','2','--gpu-frame-capture',"$Out/normal-pickup.bmp",'--gpu-capture-frame','1')
    $Log=Get-Content -Encoding UTF8 "$Out/normal-pickup.out" | Out-String
    if($Log -notmatch 'SCENE-LOCAL frame=1 .*interaction_payload=45' -or
       $Log -notmatch 'SCENE-WORLD-GPU frame=1 draws=1256 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=16 pickup_model_items=7 pickup_procedural_draws=111 pickup_procedural_items=38 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=966 hits=28 prop_assets=23 prop_draws=72 prop_culled=93 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1' -or
       $Log -notmatch 'SCENE-WORLD-GPU frame=2 draws=1256 actor_draws=120 flag_draws=15 flag_text_draws=5 projectile_draws=0 pickup_model_draws=16 pickup_model_items=7 pickup_procedural_draws=111 pickup_procedural_items=38 pickup_procedural_deferred=0 covered=[1-9][0-9]* uploads=0 hits=994 prop_assets=23 prop_draws=72 prop_culled=93 prop_deferred=0 prop_numeric=0 prop_material=0 prop_transparent=0 diagnostic_readback=1') {
        throw 'Missing pickup Scene WORLD draw or resource reuse evidence'
    }
    CheckActorPixels 'normal-pickup' @(309,540,457,540,779,548,869,559)
    if((Get-FileHash "$Package/rasterfall.exe").Hash -ne $Manifest.executable_sha256){throw 'Executable changed during suite.'}
    $Manifest.result='PASS - available gates'
} finally {
    $Manifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 "$Out/manifest.json"
    foreach($Key in $Keys){[Environment]::SetEnvironmentVariable($Key,$Saved[$Key],'Process')}
    $env:Path=$SavedPath
}
