[CmdletBinding()]
param([string]$OutputDirectory='tmp/gpu-scene-preview',
      [string]$ValidationLayerDirectory='', [int]$Frames=4,
      [string[]]$Views=@('near','mid','thin-far','campaign'), [int]$Enemies=30,
      [switch]$Independent, [switch]$Capture)
$ErrorActionPreference='Stop'
if ($Capture -and -not $Independent) { throw '-Capture requires -Independent' }
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Out=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
if ($Independent -and -not $PSBoundParameters.ContainsKey('Views')) {
    $Views+=@('enemy-special','enemy-death','enemy-fade','enemy-tongue','actor-procedural','frame-effects')
}
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Saved=@{}
foreach ($Key in @('Path','RF_GPU_VULKAN_VENDOR_ID','VK_LAYER_PATH','VK_INSTANCE_LAYERS',
    'VK_VALIDATION_VALIDATE_SYNC','VK_LAYER_REPORT_FLAGS','VK_LOADER_DEBUG')) {
    $Saved[$Key]=[Environment]::GetEnvironmentVariable($Key,'Process')
}
try {
    [Environment]::SetEnvironmentVariable('PATH',$null,'Process')
    [Environment]::SetEnvironmentVariable('Path',$Saved['Path'],'Process')
    $env:RF_GPU_VULKAN_VENDOR_ID='0x10de'
    if ($ValidationLayerDirectory) {
        $Layer=(Resolve-Path -LiteralPath $ValidationLayerDirectory).Path
        $env:Path="$Layer;C:\msys64\mingw64\bin;$($Saved['Path'])"
        $env:VK_LAYER_PATH=$Layer
        $env:VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation'
        $env:VK_VALIDATION_VALIDATE_SYNC='true'
        $env:VK_LAYER_REPORT_FLAGS='error,warn,info'
        $env:VK_LOADER_DEBUG='layer'
    }
    foreach ($View in $Views) {
        if (Get-Process rasterfall -ErrorAction SilentlyContinue) { throw 'Rasterfall is already running' }
        $Argv=@('--renderer','gpu-compute','--gpu-required','--gpu-native-present',
            '--gpu-scene-world-preview','--gpu-normal-fixed-tick','--frames',$Frames)
        if ($Independent) { $Argv+='--gpu-scene-independent-preview' }
        $CapturePath=Join-Path $Out "$View.capture"
        if ($Capture) { $Argv+=@('--gpu-frame-capture',$CapturePath,'--gpu-capture-frame',1) }
        if ($View -eq 'campaign') { $Argv+=@('--gpu-wave-repro') }
        else {
            $ViewEnemies=$Enemies
            if ($View -in @('enemy-special','enemy-death','enemy-fade','enemy-tongue','actor-procedural','frame-effects')) { $ViewEnemies=0 }
            $Argv+=@('--gpu-normal-scene',$View,$ViewEnemies)
        }
        $Quoted=($Argv | ForEach-Object {'"'+$_+'"'}) -join ' '
        $p=Start-Process -FilePath "$Package/rasterfall.exe" -WorkingDirectory $Package `
            -ArgumentList $Quoted -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput "$Out/$View.out" -RedirectStandardError "$Out/$View.err"
        $Handle=$p.Handle
        $Deadline=[DateTime]::UtcNow.AddSeconds(180)
        while (-not $p.WaitForExit(1000)) {
            if ([DateTime]::UtcNow -gt $Deadline) { Stop-Process -Id $p.Id -Force;throw "$View timeout" }
        }
        if ($p.ExitCode -ne 0) { throw "$View exit $($p.ExitCode)" }
        $Log=Get-Content -Encoding UTF8 "$Out/$View.out","$Out/$View.err" | Out-String
        if ($Log -match 'Validation Error|SYNC-HAZARD|VUID-') { throw "$View validation error" }
        $WorldOnly=if ($Independent) {0} else {1}
        $Native=[regex]::Matches($Log,"SCENE-NATIVE frame=(\d+) world_only=$WorldOnly draws=\d+ bridges=0 readback=(\d+) mixed_execute=0")
        if ($Native.Count -ne $Frames) { throw "$View missing native Scene frames" }
        if ($Independent) {
            $Layers=[regex]::Matches($Log,'SCENE-LAYERS sky=([1-9]\d*) world=([1-9]\d*) transparent=(\d+) effects=(\d+) viewmodel=(\d+) overlay=([1-9]\d*) post=identity')
            if ($Layers.Count -ne $Frames) { throw "$View missing independent layers" }
            if ($View -eq 'frame-effects' -and ([int]$Layers[0].Groups[4].Value -lt 1 -or [int]$Layers[0].Groups[5].Value -lt 1)) {
                throw "$View missing effects/viewmodel"
            }
            $Sources=[regex]::Matches($Log,'SCENE-SOURCE frame=(\d+) independent=1 legacy_producer=0 raster_commands=0 mixed_draws=0 dynamic_sources_pending=0 enemies=(\d+) enemy_culled=(\d+) procedural=(\d+) supplemental_modular=(\d+)')
            if ($Sources.Count -ne $Frames) { throw "$View missing independent source frames" }
            $EnemySamples=[regex]::Matches($Log,'SCENE-ENEMY frame=(\d+) items=(\d+) draws=(\d+) deferred=(\d+) culled=(\d+)')
            $ProceduralSamples=[regex]::Matches($Log,'SCENE-PROCEDURAL frame=(\d+) items=(\d+) draws=(\d+) net_mode=\d+')
            if ($EnemySamples.Count -ne $Frames -or $ProceduralSamples.Count -ne $Frames) {
                throw "$View missing dynamic Scene frames"
            }
            for ($i=0;$i -lt $Frames;$i++) {
                if ([int]$Sources[$i].Groups[1].Value -ne $i+1) { throw "$View source frame mismatch" }
                if ([int]$EnemySamples[$i].Groups[1].Value -ne $i+1 -or
                    [int]$ProceduralSamples[$i].Groups[1].Value -ne $i+1) {
                    throw "$View dynamic frame mismatch"
                }
                $ExpectedEnemies=0
                if ($View -in @('near','mid','thin-far')) { $ExpectedEnemies=$Enemies }
                if ($View -in @('enemy-special','enemy-death','enemy-fade','enemy-tongue')) { $ExpectedEnemies=3 }
                if ($ExpectedEnemies -gt 0) {
                    $SourceEnemies=[int]$Sources[$i].Groups[2].Value + [int]$Sources[$i].Groups[3].Value
                    if ($SourceEnemies -ne $ExpectedEnemies -or
                        [int]$EnemySamples[$i].Groups[3].Value -le 0 -or
                        [int]$EnemySamples[$i].Groups[4].Value -ne 0) {
                        throw "$View missing independent enemy bodies at frame $($i+1)"
                    }
                }
                if ($View -eq 'actor-procedural' -and
                    ([int]$Sources[$i].Groups[4].Value -lt 8 -or
                     [int]$ProceduralSamples[$i].Groups[2].Value -lt 8 -or
                     [int]$ProceduralSamples[$i].Groups[3].Value -le 0)) {
                    throw "$View missing independent procedural actors at frame $($i+1)"
                }
            }
            if ($View -in @('enemy-death','enemy-tongue')) {
                $Extras=[regex]::Matches($Log,'SCENE-ENEMY-EXTRAS frame=(\d+) shadows=(\d+) tongues=(\d+) deaths=(\d+)')
                if ($Extras.Count -ne $Frames) { throw "$View missing enemy extras frames" }
                for ($i=0;$i -lt $Frames;$i++) {
                    if ([int]$Extras[$i].Groups[1].Value -ne $i+1 -or
                        ($View -eq 'enemy-death' -and [int]$Extras[$i].Groups[4].Value -le 0) -or
                        ($View -eq 'enemy-tongue' -and [int]$Extras[$i].Groups[3].Value -le 0)) {
                        throw "$View missing independent enemy extras at frame $($i+1)"
                    }
                }
            }
            if ($View -eq 'actor-procedural') {
                foreach ($Source in $Sources) {
                    if ([int]$Source.Groups[5].Value -lt 1) { throw 'Missing supplemental modular runtime pose' }
                }
            }
        }
        for ($i=0;$i -lt $Frames;$i++) {
            if ([int]$Native[$i].Groups[1].Value -ne $i+1) { throw "$View noncontiguous frame IDs" }
            $ExpectedReadback=0
            if ($Capture -and $i -eq 0) { $ExpectedReadback=1 }
            if ([int]$Native[$i].Groups[2].Value -ne $ExpectedReadback) {
                throw "$View unexpected Scene readback at frame $($i+1)"
            }
        }
        if ($Capture) {
            $SceneCapture="$CapturePath.scene.ppm"
            if (-not (Test-Path -LiteralPath $SceneCapture)) { throw "$View missing Scene capture" }
            $CaptureBytes=[IO.File]::ReadAllBytes($SceneCapture)
            if ($CaptureBytes.Length -lt 100 -or $CaptureBytes[0] -ne 80 -or $CaptureBytes[1] -ne 54) {
                throw "$View invalid Scene PPM"
            }
        }
        if ($ValidationLayerDirectory -and ($Log -notmatch 'Insert instance layer.*VK_LAYER_KHRONOS_validation' -or
            $Log -notmatch 'Synchronization|VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT')) {
            throw "$View validation/sync not activated"
        }
        Write-Host "[SCENE-PREVIEW] $View PASS frames=$Frames world_only=$WorldOnly independent=$Independent"
    }
    Get-FileHash "$Package/rasterfall.exe" | ConvertTo-Json | Set-Content -Encoding UTF8 "$Out/executable.json"
} finally {
    foreach ($Key in $Saved.Keys) { [Environment]::SetEnvironmentVariable($Key,$Saved[$Key],'Process') }
}
