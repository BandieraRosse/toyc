[CmdletBinding()]
param([string]$OutputDirectory='tmp/scene-cost-ab',
      [ValidateRange(16,1000)][int]$Frames=64,
      [ValidateRange(1,10)][int]$Rounds=3,
      [ValidateSet('Dynamic','ActorUpload')][string]$Experiment='Dynamic')
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Out=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
if (Test-Path -LiteralPath $Out) { throw 'Use a new evidence directory' }
New-Item -ItemType Directory -Path $Out | Out-Null
$Variable=if ($Experiment -eq 'ActorUpload') {'RF_GPU_SKIN_LEGACY_UPLOAD'} else {'RF_GPU_SCENE_REBUILD_DYNAMIC'}
$Saved=[Environment]::GetEnvironmentVariable($Variable,'Process')
$SavedDynamic=$env:RF_GPU_SCENE_REBUILD_DYNAMIC
$SavedStaging=$env:RF_GPU_SKIN_STAGING_UPLOAD
$Pair=if ($Experiment -eq 'ActorUpload') {@('legacy','optimized')} else {@('rebuild','reuse')}
$Runs=[Collections.Generic.List[object]]::new()
try {
    if ($Experiment -eq 'ActorUpload') {
        $env:RF_GPU_SCENE_REBUILD_DYNAMIC='0'
        $env:RF_GPU_SKIN_STAGING_UPLOAD='0'
    }
    foreach ($Enemies in @(0,30,60)) {
        for ($Round=1;$Round -le $Rounds;$Round++) {
            $Modes=if ($Round%2) {$Pair} else {@($Pair[1],$Pair[0])}
            foreach ($Mode in $Modes) {
                [Environment]::SetEnvironmentVariable($Variable,$(if ($Mode -eq $Pair[0]) {'1'} else {'0'}),'Process')
                $Name="$Enemies-$Round-$Mode"
                & "$PSScriptRoot/gpu_scene_preview.ps1" -Independent -Views near -Enemies $Enemies `
                    -Frames $Frames -OutputDirectory "$OutputDirectory/$Name"
                $Runs.Add(@{name=$Name;enemies=$Enemies;round=$Round;mode=$Mode;frames=$Frames})
                $Runs | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 "$Out/runs.json"
            }
        }
    }
    & python "$PSScriptRoot/gpu_scene_cost_report.py" $Out
    if ($LASTEXITCODE -ne 0) { throw 'Cost report failed' }
} finally {
    [Environment]::SetEnvironmentVariable($Variable,$Saved,'Process')
    $env:RF_GPU_SCENE_REBUILD_DYNAMIC=$SavedDynamic
    $env:RF_GPU_SKIN_STAGING_UPLOAD=$SavedStaging
}
