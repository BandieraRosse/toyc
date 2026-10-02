[CmdletBinding()]
param(
    [string]$OutputDirectory='tmp/performance-lab',
    [ValidateRange(1,10)][int]$Rounds=3,
    [ValidateSet('Isolated','Interference','All')][string]$Stage='Isolated',
    [ValidateRange(1,4)][int[]]$Scenes=@(1,2,3,4),
    [switch]$Capped
)
$ErrorActionPreference='Stop'
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
    [IO.File]::WriteAllText((Join-Path $Out $Name),($Value | ConvertTo-Json -Depth 8),$Encoding)
}
$Keys=@('RF_PERF_LAB_AUTORUN','RF_PERF_LAB_SCOPE','RF_PERF_LAB_UNCAPPED',
    'RF_PERF_LAB_INTERFERENCE','VK_INSTANCE_LAYERS')
$Saved=@{}
foreach ($Key in $Keys) { $Saved[$Key]=[Environment]::GetEnvironmentVariable($Key,'Process') }
$Runs=[Collections.Generic.List[object]]::new()
$Process=$null
$Files=@('rasterfall.exe','rasterfall/assets/maps/outpost.map',
    'rasterfall/assets/maps/performance_empty.map','rasterfall/assets/maps/performance_components.map',
    'rasterfall/assets/worlds/performance.content')
$Hashes=@($Files | ForEach-Object { Get-FileHash -LiteralPath (Join-Path $Package $_) })
Write-Json $Hashes 'hashes.json'
Write-Json @{rounds=$Rounds;stage=$Stage;scenes=$Scenes;capped=[bool]$Capped;argv=@('--skip-boot','--gpu-scene-play','--map','rasterfall/assets/maps/outpost.map');validation=$false} 'config.json'
$Cases=[Collections.Generic.List[object]]::new()
foreach ($Scene in ($Scenes | Select-Object -Unique)) {
    if ($Stage -in @('Isolated','All')) { $Cases.Add(@{scene=$Scene;scope='isolated';interference=0}) }
    if ($Stage -in @('Interference','All')) {
        $Cases.Add(@{scene=$Scene;scope='outpost';interference=1})
        $Cases.Add(@{scene=$Scene;scope='outpost';interference=0})
    }
}
try {
    [Environment]::SetEnvironmentVariable('VK_INSTANCE_LAYERS',$null,'Process')
    for ($Round=1;$Round -le $Rounds;$Round++) {
        $Ordered=@($Cases)
        if ($Round%2 -eq 0) { [array]::Reverse($Ordered) }
        foreach ($Case in $Ordered) {
            $Name="r$Round-s$($Case.scene)-$($Case.scope)-interference$($Case.interference)"
            $env:RF_PERF_LAB_AUTORUN=[string]$Case.scene
            $env:RF_PERF_LAB_SCOPE=$Case.scope
            $env:RF_PERF_LAB_UNCAPPED=if ($Capped) {'0'} else {'1'}
            $env:RF_PERF_LAB_INTERFERENCE=[string]$Case.interference
            Write-Host "[PERF-LAB] $Name starting"
            $Process=Start-Process -FilePath "$Package/rasterfall.exe" -WorkingDirectory $Package `
                -ArgumentList @('--skip-boot','--gpu-scene-play','--map','rasterfall/assets/maps/outpost.map') -PassThru -WindowStyle Hidden `
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
            $Log=Get-Content -Encoding UTF8 -LiteralPath "$Out/$Name.out" -Raw
            $ErrorLog=Get-Content -Encoding UTF8 -LiteralPath "$Out/$Name.err" -Raw
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
            foreach ($Match in [regex]::Matches($Result.Value+' '+$Config.Value,'(\w+)=([^\s]+)')) {
                $Values[$Match.Groups[1].Value]=$Match.Groups[2].Value
            }
            if ($Values.valid -ne '1' -or [int]$Values.frames -lt 1 -or
                [int]$Values.remaining -ne 0 -or [int]$Values.gpu_samples -lt 1) {
                throw "$Name invalid workload or missing GPU timing"
            }
            if ($Case.scope -eq 'isolated' -and ($Values.lights -ne '0' -or $Values.shadow_maps -ne '3')) {
                throw "$Name isolated lighting contract failed"
            }
            $Runs.Add(@{name=$Name;round=$Round;case=$Case;values=$Values;exit_code=$Process.ExitCode})
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
