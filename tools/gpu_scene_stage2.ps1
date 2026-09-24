[CmdletBinding()]
param([string]$OutputDirectory='tmp/gpu-scene-stage2',
      [string]$ValidationLayerDirectory='',
      [switch]$WorldOpaque,
      [string[]]$Views=@('actor-procedural','enemy-death','enemy-tongue','near','mid','thin-far'))
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Out=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$SavedPath=$env:Path
$SavedEnv=@{}
foreach ($Key in @('RF_GPU_CAPTURE_WORLD_OPAQUE','RF_GPU_VULKAN_VENDOR_ID','VK_LAYER_PATH','VK_INSTANCE_LAYERS',
    'VK_VALIDATION_VALIDATE_SYNC','VK_LAYER_REPORT_FLAGS','VK_LOADER_DEBUG')) {
    $SavedEnv[$Key]=[Environment]::GetEnvironmentVariable($Key,'Process')
}
try {
    [Environment]::SetEnvironmentVariable('PATH',$null,'Process')
    [Environment]::SetEnvironmentVariable('Path',$SavedPath,'Process')
    $env:RF_GPU_VULKAN_VENDOR_ID='0x10de'
    if ($WorldOpaque) { $env:RF_GPU_CAPTURE_WORLD_OPAQUE='1' }
    if ($ValidationLayerDirectory) {
        $Layer=(Resolve-Path -LiteralPath $ValidationLayerDirectory).Path
        $env:Path="$Layer;C:\msys64\mingw64\bin;$SavedPath"
        $env:VK_LAYER_PATH=$Layer
        $env:VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation'
        $env:VK_VALIDATION_VALIDATE_SYNC='true'
        $env:VK_LAYER_REPORT_FLAGS='error,warn,info'
        $env:VK_LOADER_DEBUG='layer'
    }
    foreach ($View in $Views) {
        foreach ($Enemies in $(if ($View -in @('near','mid','thin-far')) { @(0,30,60) } else { @(0) })) {
            if (Get-Process rasterfall -ErrorAction SilentlyContinue | Where-Object { -not $_.HasExited }) {
                throw 'Rasterfall is already running'
            }
            $Name="$View-$Enemies";$Capture=Join-Path $Out "$Name.bmp"
            $Argv=@('--renderer','gpu-compute','--gpu-required','--gpu-native-present',
                '--gpu-normal-scene',$View,$Enemies,'--gpu-normal-fixed-tick',
                '--frame-audit','--frames',2,'--gpu-frame-capture',$Capture,'--gpu-capture-frame',1)
            $Timeout=60000
            if ($View -eq 'campaign') {
                $Argv=@('--renderer','gpu-compute','--gpu-required','--gpu-native-present',
                    '--map','rasterfall/assets/maps/rasterfall.map','--gpu-wave-repro',
                    '--gpu-normal-fixed-tick','--frame-audit','--frames',320,
                    '--gpu-frame-capture',$Capture,'--gpu-capture-frame',320)
                $Timeout=900000
            }
            $Quoted=($Argv | ForEach-Object {'"'+$_+'"'}) -join ' '
            $p=Start-Process -FilePath "$Package/rasterfall.exe" -WorkingDirectory $Package `
                -ArgumentList $Quoted -WindowStyle Hidden -PassThru `
                -RedirectStandardOutput "$Out/$Name.out" -RedirectStandardError "$Out/$Name.err"
            $ProcessHandle=$p.Handle
            $Deadline=[DateTime]::UtcNow.AddMilliseconds($Timeout)
            while (-not $p.WaitForExit(1000)) {
                if ([DateTime]::UtcNow -gt $Deadline) { Stop-Process -Id $p.Id -Force;throw "$Name timeout" }
            }
            if ($p.ExitCode -ne 0) { throw "$Name exit $($p.ExitCode)" }
            $Log=Get-Content -Encoding UTF8 "$Out/$Name.out","$Out/$Name.err" | Out-String
            if ($Log -match 'Validation Error|SYNC-HAZARD|VUID-') { throw "$Name validation error" }
            if ($Log -notmatch 'FRAME-AUDIT.*path=gpu-native') { throw "$Name missing native present" }
            if ($WorldOpaque -and $Log -notmatch 'WORLD-OPAQUE-CAPTURE black-background=1 suffix-excluded=1') {
                throw "$Name missing WORLD-only capture"
            }
            if ($Log -match 'SCENE-WORLD-COST[^\r\n]*bridges=[1-9]' -or
                $Log -notmatch 'SCENE-WORLD-COST[^\r\n]*gpu_valid=1') { throw "$Name invalid Scene cost/bridge audit" }
            if ($Log -match 'SCENE-ENEMY[^\r\n]*deferred=[1-9]') { throw "$Name missing enemy body" }
            if ($View -eq 'actor-procedural' -and
                $Log -notmatch 'SCENE-PROCEDURAL[^\r\n]*items=8 draws=[1-9]') {
                throw "$Name missing procedural actors"
            }
            if (-not (Test-Path -LiteralPath $Capture) -or
                -not (Test-Path -LiteralPath "$Capture.scene.ppm")) { throw "$Name missing captures" }
            if ($ValidationLayerDirectory -and
                ($Log -notmatch 'Insert instance layer.*VK_LAYER_KHRONOS_validation' -or
                 $Log -notmatch 'Synchronization|VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT')) {
                throw "$Name missing validation/sync activation"
            }
            Write-Host "[SCENE-STAGE2] $Name PASS"
        }
    }
    Get-FileHash "$Package/rasterfall.exe", "$Out/*.bmp", "$Out/*.ppm" |
        Select-Object Path,Hash | ConvertTo-Json | Set-Content -Encoding UTF8 "$Out/hashes.json"
} finally {
    $env:Path=$SavedPath
    foreach ($Key in $SavedEnv.Keys) {
        [Environment]::SetEnvironmentVariable($Key,$SavedEnv[$Key],'Process')
    }
}
