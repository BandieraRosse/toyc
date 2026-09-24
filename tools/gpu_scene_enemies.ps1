[CmdletBinding()]
param([string]$OutputDirectory='tmp/gpu-scene-enemies',
      [string]$ValidationLayerDirectory='')
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Out=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$SavedPath=$env:Path
$SavedEnv=@{}
foreach ($Key in @('RF_GPU_VULKAN_VENDOR_ID','VK_LAYER_PATH','VK_INSTANCE_LAYERS',
    'VK_VALIDATION_VALIDATE_SYNC','VK_LAYER_REPORT_FLAGS','VK_LOADER_DEBUG')) {
    $SavedEnv[$Key]=[Environment]::GetEnvironmentVariable($Key,'Process')
}
try {
    [Environment]::SetEnvironmentVariable('PATH',$null,'Process')
    [Environment]::SetEnvironmentVariable('Path',$SavedPath,'Process')
    $env:RF_GPU_VULKAN_VENDOR_ID='0x10de'
    if ($ValidationLayerDirectory) {
        $Layer=(Resolve-Path -LiteralPath $ValidationLayerDirectory).Path
        $env:Path="$Layer;C:\msys64\mingw64\bin;$SavedPath"
        $env:VK_LAYER_PATH=$Layer
        $env:VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation'
        $env:VK_VALIDATION_VALIDATE_SYNC='true'
        $env:VK_LAYER_REPORT_FLAGS='error,warn,info'
        $env:VK_LOADER_DEBUG='layer'
    }
    foreach ($Run in @(@('special-first','enemy-special',0,2,1),
                       @('special-motion','enemy-special',0,12,12),
                       @('ordinary-first','near',30,2,1),
                       @('ordinary-motion','near',30,2,2),
                       @('ordinary-block','near',30,2,1,'block-infected'),
                       @('ordinary-humanoid','near',30,2,1,'humanoid-infected'))) {
        if (Get-Process rasterfall -ErrorAction SilentlyContinue | Where-Object { -not $_.HasExited }) {
            throw 'Rasterfall is already running'
        }
        $Name=$Run[0];$Capture=Join-Path $Out "$Name.bmp"
        $Argv=@('--renderer','gpu-compute','--gpu-required','--gpu-native-present',
            '--gpu-normal-scene',$Run[1],$Run[2],'--gpu-normal-fixed-tick',
            '--frame-audit','--frames',$Run[3],'--gpu-frame-capture',$Capture,
            '--gpu-capture-frame',$Run[4])
        if ($Run.Count -gt 5) { $Argv+=@('--enemy-visual-family',$Run[5]) }
        $Quoted=($Argv | ForEach-Object {'"'+$_+'"'}) -join ' '
        $p=Start-Process -FilePath "$Package/rasterfall.exe" -WorkingDirectory $Package `
            -ArgumentList $Quoted -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput "$Out/$Name.out" -RedirectStandardError "$Out/$Name.err"
        $ProcessHandle=$p.Handle
        if (-not $p.WaitForExit(60000)) { Stop-Process -Id $p.Id -Force;throw "$Name timeout" }
        if ($p.ExitCode -ne 0) { throw "$Name exit $($p.ExitCode)" }
        $Log=Get-Content -Encoding UTF8 "$Out/$Name.out","$Out/$Name.err" | Out-String
        if ($Log -match 'Validation Error|SYNC-HAZARD|VUID-') { throw "$Name validation error" }
        if ($Log -notmatch 'FRAME-AUDIT.*path=gpu-native') { throw "$Name missing native present" }
        $Samples=[regex]::Matches($Log,'SCENE-ENEMY frame=(\d+) items=(\d+) draws=(\d+) deferred=(\d+) culled=(\d+)')
        if ($Samples.Count -ne $Run[3]) { throw "$Name missing enemy frames" }
        foreach ($Sample in $Samples) {
            if ($Run[1] -eq 'enemy-special') {
                if ($Sample.Groups[2].Value -ne '3' -or [int]$Sample.Groups[3].Value -le 0 -or
                    $Sample.Groups[4].Value -ne '0') { throw "$Name missing special bodies" }
            } elseif ($Sample.Groups[2].Value -ne '30' -or
                [int]$Sample.Groups[3].Value -le 0 -or $Sample.Groups[4].Value -ne '0') {
                throw "$Name missing ordinary enemy bodies"
            }
        }
        if (-not (Test-Path -LiteralPath $Capture) -or
            -not (Test-Path -LiteralPath "$Capture.scene.ppm")) { throw "$Name missing captures" }
        if ($ValidationLayerDirectory -and
            ($Log -notmatch 'Insert instance layer.*VK_LAYER_KHRONOS_validation' -or
             $Log -notmatch 'Synchronization|VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT')) {
            throw "$Name missing validation/sync activation"
        }
        Write-Host "[SCENE-ENEMY] $Name PASS frames=$($Samples.Count)"
    }
    & python (Join-Path $PSScriptRoot 'gpu_scene_enemy_pixels.py') $Out
    if ($LASTEXITCODE -ne 0) { throw 'Enemy body pixel comparison failed' }
    Get-FileHash "$Package/rasterfall.exe", "$Out/*.bmp", "$Out/*.ppm" |
        Select-Object Path,Hash | ConvertTo-Json | Set-Content -Encoding UTF8 "$Out/hashes.json"
} finally {
    $env:Path=$SavedPath
    foreach ($Key in $SavedEnv.Keys) {
        [Environment]::SetEnvironmentVariable($Key,$SavedEnv[$Key],'Process')
    }
}
