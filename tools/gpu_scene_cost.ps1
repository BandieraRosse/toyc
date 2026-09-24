[CmdletBinding()]
param([string]$OutputDirectory='tmp/scene-cost-ab',
      [ValidateRange(16,1000)][int]$Frames=64,
      [ValidateRange(1,10)][int]$Rounds=3,
      [ValidateSet('Dynamic','ActorUpload','EnemyPrep','BindUpload','DrawBind','SkinBatch','LayerReuse','P1')][string]$Experiment='Dynamic',
      [switch]$DenseComponents)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Out=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
if (Test-Path -LiteralPath $Out) { throw 'Use a new evidence directory' }
New-Item -ItemType Directory -Path $Out | Out-Null
$Variable=switch ($Experiment) {
    'ActorUpload' {'RF_GPU_SKIN_LEGACY_UPLOAD'}
    'EnemyPrep' {'RF_GPU_SCENE_LEGACY_ENEMY_PREP'}
    'BindUpload' {'RF_GPU_SCENE_LEGACY_BIND_UPLOAD'}
    'DrawBind' {'RF_GPU_SCENE_LEGACY_BIND'}
    'SkinBatch' {'RF_GPU_SCENE_LEGACY_SKIN_BATCH'}
    'LayerReuse' {'RF_GPU_SCENE_REBUILD_LAYERS'}
    'P1' {'RF_GPU_SCENE_LEGACY_SKIN_BATCH'}
    default {'RF_GPU_SCENE_REBUILD_DYNAMIC'}
}
$Pair=if ($Experiment -ne 'Dynamic') {@('legacy','optimized')} else {@('rebuild','reuse')}
$Runs=[Collections.Generic.List[object]]::new()
$DenseMap=''
if ($DenseComponents) {
    $DenseMap=Join-Path $Out 'dense-components.map'
    & python "$PSScriptRoot/gpu_scene_dense_map.py" `
        (Join-Path $Root 'rasterfall/assets/maps/rasterfall.map') $DenseMap
    if ($LASTEXITCODE -ne 0) { throw 'Dense component map generation failed' }
}
$DiagnosticSaved=@{}
foreach ($Key in @('RF_GPU_SCENE_REBUILD_DYNAMIC','RF_GPU_SKIN_LEGACY_UPLOAD',
    'RF_GPU_SKIN_STAGING_UPLOAD','RF_GPU_SCENE_LEGACY_ENEMY_PREP',
    'RF_GPU_SCENE_LEGACY_BIND','RF_GPU_SCENE_LEGACY_SKIN_BATCH','RF_GPU_SCENE_REBUILD_LAYERS',
    'RF_GPU_SCENE_LEGACY_BIND_UPLOAD')) {
    $DiagnosticSaved[$Key]=[Environment]::GetEnvironmentVariable($Key,'Process')
}
try {
    foreach ($Key in $DiagnosticSaved.Keys) { [Environment]::SetEnvironmentVariable($Key,'0','Process') }
    if ($Experiment -eq 'ActorUpload') {
        $env:RF_GPU_SCENE_REBUILD_DYNAMIC='0'
        $env:RF_GPU_SKIN_STAGING_UPLOAD='0'
        $env:RF_GPU_SCENE_LEGACY_SKIN_BATCH='1'
    }
    foreach ($Enemies in @(0,30,60)) {
        for ($Round=1;$Round -le $Rounds;$Round++) {
            $Modes=if ($Round%2) {$Pair} else {@($Pair[1],$Pair[0])}
            foreach ($Mode in $Modes) {
                $View=if ($DenseComponents -and $Enemies -eq 60) {'near-heavy'} else {'near'}
                [Environment]::SetEnvironmentVariable($Variable,$(if ($Mode -eq $Pair[0]) {'1'} else {'0'}),'Process')
                if ($Experiment -eq 'P1') { $env:RF_GPU_SCENE_REBUILD_LAYERS=[Environment]::GetEnvironmentVariable($Variable,'Process') }
                $Name="$Enemies-$Round-$Mode"
                & "$PSScriptRoot/gpu_scene_preview.ps1" -Independent -Views $View -Enemies $Enemies `
                    -Frames $Frames -OutputDirectory "$OutputDirectory/$Name" -MapPath $DenseMap
                $Runs.Add(@{name=$Name;enemies=$Enemies;round=$Round;mode=$Mode;frames=$Frames;
                    dense_components=[bool]$DenseComponents;view=$View})
                $Runs | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 "$Out/runs.json"
            }
        }
    }
    & python "$PSScriptRoot/gpu_scene_cost_report.py" $Out
    if ($LASTEXITCODE -ne 0) { throw 'Cost report failed' }
} finally {
    foreach ($Key in $DiagnosticSaved.Keys) { [Environment]::SetEnvironmentVariable($Key,$DiagnosticSaved[$Key],'Process') }
}
