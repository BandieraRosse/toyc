[CmdletBinding()]
param([string]$OutputDirectory='tmp/gpu-scene-preview',
      [string]$ValidationLayerDirectory='', [int]$Frames=4,
      [string[]]$Views=@('near','mid','thin-far','campaign'), [int]$Enemies=30)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Out=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
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
        if ($View -eq 'campaign') { $Argv+=@('--gpu-wave-repro') }
        else { $Argv+=@('--gpu-normal-scene',$View,$Enemies) }
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
        $Native=[regex]::Matches($Log,'SCENE-NATIVE frame=(\d+) world_only=1 draws=\d+ bridges=0 readback=0 mixed_execute=0')
        if ($Native.Count -ne $Frames) { throw "$View missing native Scene frames" }
        for ($i=0;$i -lt $Frames;$i++) {
            if ([int]$Native[$i].Groups[1].Value -ne $i+1) { throw "$View noncontiguous frame IDs" }
        }
        if ($ValidationLayerDirectory -and ($Log -notmatch 'Insert instance layer.*VK_LAYER_KHRONOS_validation' -or
            $Log -notmatch 'Synchronization|VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT')) {
            throw "$View validation/sync not activated"
        }
        Write-Host "[SCENE-PREVIEW] $View PASS frames=$Frames WORLD-only"
    }
    Get-FileHash "$Package/rasterfall.exe" | ConvertTo-Json | Set-Content -Encoding UTF8 "$Out/executable.json"
} finally {
    foreach ($Key in $Saved.Keys) { [Environment]::SetEnvironmentVariable($Key,$Saved[$Key],'Process') }
}
