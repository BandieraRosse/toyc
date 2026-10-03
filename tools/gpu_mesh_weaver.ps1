<#
.SYNOPSIS
Capture a real native Mesh Weaver task and its accounting evidence.
.DESCRIPTION
TimeMs is the task simulation timeline, including a power-off interval.
PauseMs cuts power at the requested active manufacturing age; ResumeMs restores
it at the requested task timeline age. The JSON separately reports Game's
actual elapsed_ms, which stops while paused and when READY. Geometry, progress
and output are produced by the normal session/Game APIs. This does not build
or refresh the native package.
#>
[CmdletBinding()]
param(
    [string]$OutputDirectory='tmp/mesh-weaver/native',
    [ValidateSet('quarter','front','rear','head','gun','top','wide')][string]$View='quarter',
    [ValidateSet('idle','ak','pistol','smg','shotgun','awp')][string]$Blueprint='idle',
    [ValidateRange(-1,120000)][int]$TimeMs=-1,
    [ValidateRange(-1,120000)][int]$PauseMs=-1,
    [ValidateRange(-1,120000)][int]$ResumeMs=-1,
    [switch]$Menu,
    [ValidateRange(2,36000)][int]$Frames=30,
    [ValidateRange(30,900)][int]$TimeoutSeconds=240
)
$ErrorActionPreference='Stop'
if($ResumeMs -ge 0 -and ($PauseMs -lt 0 -or $ResumeMs -le $PauseMs)) {
    throw 'ResumeMs requires PauseMs and must be greater than PauseMs.'
}
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Out=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Exe=Join-Path $Package 'rasterfall.exe'
if(-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    throw 'Native package is missing; first stage it through windows/NativeCodex.ps1.'
}
$Saved=@{}
$SavedPath=$env:Path
$Keys=@('RF_WEAVER_VIEW','RF_WEAVER_BLUEPRINT','RF_WEAVER_TIME_MS','RF_WEAVER_PAUSE_MS',
    'RF_WEAVER_RESUME_MS','RF_WEAVER_MENU')
foreach($Key in $Keys) {$Saved[$Key]=[Environment]::GetEnvironmentVariable($Key,'Process')}
$RunId=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
$Name="$Blueprint-$TimeMs-pause-$PauseMs-resume-$ResumeMs-$View-$RunId"
$Capture=Join-Path $Out "$Name.bmp"
$Stdout=Join-Path $Out "$Name.out"
$Stderr=Join-Path $Out "$Name.err"
$RecordPath=Join-Path $Out "$Name.json"
$Record=[ordered]@{status='started';view=$View;blueprint=$Blueprint;
    requested_time_ms=$TimeMs;pause_ms=$PauseMs;resume_ms=$ResumeMs;menu=[bool]$Menu;
    frames=$Frames;started_utc=[DateTime]::UtcNow.ToString('o');
    time_basis='Task timeline includes power-off intervals. Game elapsed_ms excludes them.';
    capture="$Capture.scene.ppm";stdout=$Stdout;stderr=$Stderr;
    exe_sha256=(Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash}
$Process=$null
try {
    [Environment]::SetEnvironmentVariable('PATH',$null,'Process')
    [Environment]::SetEnvironmentVariable('Path',$SavedPath,'Process')
    $env:RF_WEAVER_VIEW=$View
    $env:RF_WEAVER_BLUEPRINT=$Blueprint
    $env:RF_WEAVER_TIME_MS=[string]$TimeMs
    $env:RF_WEAVER_PAUSE_MS=[string]$PauseMs
    $env:RF_WEAVER_RESUME_MS=[string]$ResumeMs
    $env:RF_WEAVER_MENU=if($Menu){'1'}else{'0'}
    $Argv=@('--renderer','gpu-scene','--map','rasterfall/assets/maps/outpost.map',
        '--gpu-normal-scene','mesh-weaver','0','--gpu-normal-fixed-tick',
        '--frames',"$Frames",'--frame-audit','--gpu-frame-capture',$Capture,'--gpu-capture-frame',"$Frames")
    $Quoted=($Argv | ForEach-Object {'"'+$_+'"'}) -join ' '
    $Record.arguments=$Argv
    $Process=Start-Process -FilePath $Exe -WorkingDirectory $Package `
        -ArgumentList $Quoted -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr
    $ProcessHandle=$Process.Handle
    $Deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while(-not $Process.WaitForExit(1000)) {
        if([DateTime]::UtcNow -gt $Deadline) {
            Stop-Process -Id $Process.Id -Force
            throw 'Mesh Weaver native capture timed out'
        }
    }
    $Process.WaitForExit()
    $Record.exit_code=$Process.ExitCode
    $Lines=@(Get-Content -LiteralPath $Stdout,$Stderr -Encoding UTF8)
    $Log=$Lines | Out-String
    $Audits=@()
    $Diagnostic=$null
    $Geometry=$null
    foreach($Line in $Lines) {
        if($Line -match '^MESH-WEAVER-AUDIT (\{.*\})$') {
            $Audits+=($Matches[1] | ConvertFrom-Json)
        } elseif($Line -match '^MESH-WEAVER-DIAGNOSTIC (\{.*\})$') {
            $Diagnostic=$Matches[1] | ConvertFrom-Json
        } elseif($Line -match '^MESH-WEAVER-BLUEPRINT (\{.*\})$') {
            $Geometry=$Matches[1] | ConvertFrom-Json
        }
    }
    # Keep real accounting evidence in failed records as well as successful ones.
    $Record.diagnostic=$Diagnostic
    $Record.blueprint_geometry=$Geometry
    $Record.events=$Audits
    if($Process.ExitCode -ne 0){throw "Mesh Weaver exit $($Process.ExitCode); inspect $Stderr"}
    if($Log -notmatch 'SCENE-NATIVE.*bridges=0.*mixed_execute=0' -or
       $Log -match 'Validation Error|SYNC-HAZARD|VUID-|\bFAIL\b|preparation/submit failed|MESH-WEAVER-ERROR' -or
       -not (Test-Path -LiteralPath "$Capture.scene.ppm")) {throw 'Incomplete native capture evidence'}
    $Samples=@($Audits | Where-Object {$_.event -eq 'capture' -and $_.frame -eq $Frames})
    if(-not $Diagnostic -or $Samples.Count -ne 1) {
        throw 'Missing exact capture-frame manufacturing audit; this executable may lack diagnostic hooks.'
    }
    $Sample=$Samples[0]
    $Record.capture_state=$Sample
    if($Sample.blueprint -ne $Blueprint -or $Diagnostic.capture_frame -ne $Frames) {
        throw 'Capture audit does not match the requested blueprint/frame.'
    }
    if($TimeMs -ge 0 -and [Math]::Abs($Sample.timeline_ms-$TimeMs) -gt 16) {
        throw "Capture timeline mismatch: requested $TimeMs, actual $($Sample.timeline_ms)."
    }
    if($Blueprint -ne 'idle') {
        if(-not $Geometry -or $Geometry.id -ne $Blueprint) {throw 'Missing real blueprint geometry evidence.'}
        if($Geometry.manufacturable -and $Geometry.size_supported) {
            if($Sample.start_result -ne 0 -or $Sample.serial -lt 1) {throw 'Requested manufacturing task did not start.'}
        } elseif($Sample.start_result -eq 0 -or $Sample.produced -ne 0) {
            throw 'Unsupported blueprint did not retain its real start rejection.'
        }
    }
    if($Sample.energy_kj -lt 0 -or $Sample.progress -lt 0 -or $Sample.progress -gt 1) {
        throw 'Invalid manufacturing accounting in the captured state.'
    }
    if($Menu -and -not $Sample.menu) {throw 'Requested terminal panel is not open in the captured state.'}
    $Record.capture_sha256=(Get-FileHash -LiteralPath "$Capture.scene.ppm" -Algorithm SHA256).Hash
    $AssetPaths=@('rasterfall/assets/maps/outpost.map',
        'rasterfall/assets/models/props/mesh_weaver/rf_mesh_weaver_frame.rmesh',
        'rasterfall/assets/models/props/mesh_weaver/rf_mesh_weaver_tray.rmesh',
        'rasterfall/assets/models/props/mesh_weaver/rf_mesh_weaver_emitter_yoke.rmesh',
        'rasterfall/assets/models/props/mesh_weaver/rf_mesh_weaver_emitter_core.rmesh',
        'rasterfall/assets/models/props/mesh_weaver/rf_mesh_weaver_emitter_petal.rmesh')
    $Weapons=@{ak='ar_ak47';pistol='pg_glock1';smg='smg_mac10';shotgun='sg_pump_action';awp='rf_AWP'}
    $GunPath=$null
    if($Weapons.ContainsKey($Blueprint)) {
        $GunPath="rasterfall/assets/models/$($Weapons[$Blueprint]).rmesh"
        $AssetPaths+=$GunPath
    }
    $Record.assets=@($AssetPaths | ForEach-Object {
        $RelativePath=$_
        $Asset=Join-Path $Package $RelativePath
        if(-not (Test-Path -LiteralPath $Asset -PathType Leaf)) {throw "Missing staged asset: $RelativePath"}
        $Hash=(Get-FileHash -LiteralPath $Asset -Algorithm SHA256).Hash
        if($Geometry -and $RelativePath -eq $GunPath -and $Hash -ne $Geometry.runtime_sha256) {
            throw 'Staged weapon hash differs from its manufacturing statistics; refresh assets and blueprint cache.'
        }
        [ordered]@{path=$RelativePath;sha256=$Hash}
    })
    $Record.status='passed'
    Write-Host "[MESH-WEAVER] PASS timeline=$($Sample.timeline_ms) ms active=$($Sample.elapsed_ms) ms phase=$($Sample.phase) progress=$($Sample.progress) energy=$($Sample.energy_kj) kJ"
    Write-Host "[MESH-WEAVER] Evidence $RecordPath"
} catch {
    $Record.status='failed'
    $Record.error=$_.Exception.Message
    throw
} finally {
    $Record.finished_utc=[DateTime]::UtcNow.ToString('o')
    try {
        [IO.File]::WriteAllText($RecordPath,($Record | ConvertTo-Json -Depth 9),[Text.UTF8Encoding]::new($false))
    } finally {
        # An unwritable evidence file must never leak capture settings to a
        # later native run in the same PowerShell process.
        $env:Path=$SavedPath
        foreach($Key in $Keys) {[Environment]::SetEnvironmentVariable($Key,$Saved[$Key],'Process')}
    }
}
