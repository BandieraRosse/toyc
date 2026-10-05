[CmdletBinding()]
param(
    [string]$OutputDirectory='tmp/outpost-performance',
    [ValidateRange(1,5)][int]$Rounds=5,
    [ValidateRange(120,4096)][int]$Samples=360,
    [string[]]$Views=@('sky-north','lab-computer-side','electronics-lab','lighting-lab'),
    [switch]$Compare,
    [switch]$ComparePreparation,
    [switch]$CompareArchitecture,
    [switch]$AllLabs,
    [ValidateRange(640,7680)][int]$Width=1920,
    [ValidateRange(480,4320)][int]$Height=1080
)
$ErrorActionPreference='Stop'
if(([int][bool]$Compare+[int][bool]$ComparePreparation+[int][bool]$CompareArchitecture) -gt 1){throw 'Choose one comparison axis'}
if($AllLabs -and !$PSBoundParameters.ContainsKey('Views')) {
    $Views=@('sky-north','electronics-lab','lighting-lab')
}
if($AllLabs -and @($Views | Where-Object {$_ -like 'lab-computer*'}).Count) {
    throw 'Computer inspection views override exhibit switches; use electronics-case or electronics-lab for all-labs sampling'
}
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
$SavedPath=$env:Path
$Keys=@('RF_SCENE_PERF_FRAMES','RF_LABS_ALL','RF_GPU_SCENE_LEGACY_LAYER_COLORS',
    'RF_GPU_SCENE_DISABLE_LAYER_CULL','RF_GPU_SCENE_DISABLE_DRAW_CULL','RF_GPU_SCENE_LEGACY_BIND',
    'RF_GPU_SCENE_LEGACY_ORIGIN_BOUNDS','RF_GPU_SCENE_LEGACY_LAYER_PACKING',
    'RF_GPU_VULKAN_VENDOR_ID','RF_GPU_SKY_TIME','RF_GPU_SKY_SCALE','VK_INSTANCE_LAYERS','RF_GPU_ARCHITECTURE')
$Saved=@{}
foreach($Key in $Keys){$Saved[$Key]=[Environment]::GetEnvironmentVariable($Key,'Process')}
$Runs=[Collections.Generic.List[object]]::new()
$Process=$null
$Hash=(Get-FileHash -LiteralPath "$Package/rasterfall.exe").Hash
Write-Json @{exe=$Hash;map=(Get-FileHash -LiteralPath "$Package/rasterfall/assets/maps/outpost.map").Hash;
    rounds=$Rounds;samples=$Samples;views=$Views;all_labs=[bool]$AllLabs;compare=[bool]$Compare;
    compare_preparation=[bool]$ComparePreparation;compare_architecture=[bool]$CompareArchitecture;gpu_vendor=$env:RF_GPU_VULKAN_VENDOR_ID;
    width=$Width;height=$Height;warmup=120;sky_scale=4;sky_time=0;clock='realtime';cap=120} 'config.json'
try {
    [Environment]::SetEnvironmentVariable('PATH',$null,'Process')
    [Environment]::SetEnvironmentVariable('Path',$SavedPath,'Process')
    $env:RF_SCENE_PERF_FRAMES=[string]$Samples
    $env:RF_LABS_ALL=if($AllLabs){'1'}else{'0'}
    $env:RF_GPU_SKY_TIME='0';$env:RF_GPU_SKY_SCALE='4'
    $env:RF_GPU_SCENE_LEGACY_BIND='0'
    [Environment]::SetEnvironmentVariable('VK_INSTANCE_LAYERS',$null,'Process')
    for($Round=1;$Round -le $Rounds;$Round++) {
        foreach($View in $Views) {
            $Modes=if($Compare -or $ComparePreparation){@('reference','optimized')}else{@('optimized')}
            if($CompareArchitecture){$Modes=@('software','hardware')}
            if($Round%2 -eq 0){[array]::Reverse($Modes)}
            foreach($Mode in $Modes) {
                $Name="r$Round-$View-$Mode"
                if($CompareArchitecture){$env:RF_GPU_ARCHITECTURE=$Mode}
                foreach($Key in @('RF_GPU_SCENE_LEGACY_LAYER_COLORS','RF_GPU_SCENE_DISABLE_LAYER_CULL',
                    'RF_GPU_SCENE_DISABLE_DRAW_CULL')) {
                    [Environment]::SetEnvironmentVariable($Key,$(if($Compare -and $Mode -eq 'reference'){'1'}else{'0'}),'Process')
                }
                foreach($Key in @('RF_GPU_SCENE_LEGACY_ORIGIN_BOUNDS','RF_GPU_SCENE_LEGACY_LAYER_PACKING')) {
                    [Environment]::SetEnvironmentVariable($Key,$(if($ComparePreparation -and $Mode -eq 'reference'){'1'}else{'0'}),'Process')
                }
                $Argv=@('--skip-boot','--renderer','gpu-scene','--map','rasterfall/assets/maps/outpost.map',
                    '--gpu-normal-scene',$View,'0','--window-size',[string]$Width,[string]$Height)
                $Process=Start-Process -FilePath "$Package/rasterfall.exe" -WorkingDirectory $Package -WindowStyle Hidden -PassThru `
                    -ArgumentList (($Argv | ForEach-Object {'"'+$_+'"'}) -join ' ') `
                    -RedirectStandardOutput "$Out/$Name.out" -RedirectStandardError "$Out/$Name.err"
                $Handle=$Process.Handle
                $Timer=[Diagnostics.Stopwatch]::StartNew()
                while(-not $Process.WaitForExit(500)) {
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
                if($Result.valid -ne '1' -or [int]$Result.samples -ne $Samples -or [int]$Result.gpu_p50_us -le 0){throw "$Name invalid samples"}
                if($Result.extent -ne "${Width}x${Height}"){throw "$Name extent differs from requested window size"}
                $Runs.Add(@{round=$Round;view=$View;mode=$Mode;result=$Result;argv=$Argv;exit=$Process.ExitCode})
                Write-Json @($Runs.ToArray()) 'report.json'
                Write-Output "$Name p50=$($Result.p50_us) p95=$($Result.p95_us) p99=$($Result.p99_us) us; GPU=$($Result.gpu_p50_us) us"
                $Process=$null
            }
        }
    }
    if((Get-FileHash -LiteralPath "$Package/rasterfall.exe").Hash -ne $Hash){throw 'Executable changed while sampling'}
} finally {
    if($Process -and !$Process.HasExited){Stop-Process -Id $Process.Id -Force}
    foreach($Key in $Keys){[Environment]::SetEnvironmentVariable($Key,$Saved[$Key],'Process')}
    [Environment]::SetEnvironmentVariable('Path',$SavedPath,'Process')
}
