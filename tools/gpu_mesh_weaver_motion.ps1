<#
.SYNOPSIS
Record a reproducible manufacturing cycle from actual native GPU frames.
.DESCRIPTION
Runs the real Game task through the fixed-step visual driver, with one second
of idle before startup. Explicit GPU readback is enabled only for this capture;
the result is visual evidence and must not be used as performance evidence.
#>
[CmdletBinding()]
param(
    [string]$OutputDirectory='tmp/mesh-weaver/motion',
    [ValidateSet('quarter','front','rear','head','gun','top','wide')][string]$View='quarter',
    [ValidateSet('ak','pistol','smg','shotgun')][string]$Blueprint='ak',
    [ValidateRange(1,8)][int]$Every=2,
    [ValidateRange(120,1800)][int]$TimeoutSeconds=600,
    [switch]$SkipPreview
)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Exe=Join-Path $Package 'rasterfall.exe'
if(-not (Test-Path -LiteralPath $Exe)){throw 'Build and stage the native package first.'}
if(Get-Process rasterfall -ErrorAction SilentlyContinue){throw 'Run native GPU checks serially.'}
$Out=Join-Path ([IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))) ([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $Out | Out-Null
$Frames=750;$TimelineMs=11000
$Stdout=Join-Path $Out 'native.out';$Stderr=Join-Path $Out 'native.err'
$SavedPath=$env:PATH;$Saved=@{}
$Keys=@('RF_WEAVER_VIEW','RF_WEAVER_BLUEPRINT','RF_WEAVER_TIME_MS','RF_WEAVER_PAUSE_MS',
    'RF_WEAVER_RESUME_MS','RF_WEAVER_MENU','RF_WEAVER_INTERACTION_AUDIT','RF_SCENE_PERF_FRAMES',
    'RF_WEAVER_CAPTURE_DIRECTORY','RF_WEAVER_CAPTURE_EVERY')
foreach($Key in $Keys){$Saved[$Key]=[Environment]::GetEnvironmentVariable($Key,'Process')}
$Record=[ordered]@{status='started';started_utc=[DateTime]::UtcNow.ToString('o');blueprint=$Blueprint;view=$View;
    simulation='Actual Game fixed ticks, 16 ms; explicit native GPU frame sequence';
    performance_evidence=$false;requested_frames=$Frames;capture_every=$Every;
    duration_ms=($Frames*16);exe_sha256=(Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash}
$Process=$null
try {
    Remove-Item Env:Path -ErrorAction SilentlyContinue;Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:PATH=$SavedPath
    foreach($Key in $Keys){[Environment]::SetEnvironmentVariable($Key,$null,'Process')}
    $env:RF_WEAVER_VIEW=$View;$env:RF_WEAVER_BLUEPRINT=$Blueprint
    $env:RF_WEAVER_TIME_MS=[string]$TimelineMs
    $env:RF_WEAVER_CAPTURE_DIRECTORY=$Out;$env:RF_WEAVER_CAPTURE_EVERY=[string]$Every
    $Argv=@('--renderer','gpu-scene','--skip-boot','--map','rasterfall/assets/maps/outpost.map',
        '--gpu-normal-scene','mesh-weaver','0','--gpu-normal-fixed-tick','--frames',"$Frames",'--frame-audit')
    $Record.arguments=$Argv
    $Quoted=($Argv|ForEach-Object {'"'+$_+'"'}) -join ' '
    $Process=Start-Process -FilePath $Exe -WorkingDirectory $Package -ArgumentList $Quoted -WindowStyle Hidden `
        -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr -PassThru
    $Handle=$Process.Handle;$Deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while(-not $Process.WaitForExit(1000)) {
        if([DateTime]::UtcNow -gt $Deadline){Stop-Process -Id $Process.Id -Force;throw 'Native motion capture timed out.'}
    }
    $Process.WaitForExit();$Process.Refresh();$Record.exit_code=$Process.ExitCode
    if($Process.ExitCode -ne 0){throw "Native capture exit $($Process.ExitCode)."}
    $Log=Get-Content -LiteralPath $Stdout,$Stderr -Encoding UTF8 | Out-String
    if($Log -match 'Validation Error|SYNC-HAZARD|VUID-|preparation/submit failed|MESH-WEAVER-ERROR'){throw 'Native capture reported an error.'}
    if($Log -notmatch 'SCENE-SOURCE.*independent=1.*legacy_producer=0' -or $Log -match 'SCENE-NATIVE[^\r\n]*(bridges|mixed_execute)=[1-9]'){throw 'Unexpected rendering path.'}
    $Captures=@([regex]::Matches($Log,'MESH-WEAVER-CAPTURE id=(\d+) frame=(\d+) phase=(\d+) progress=([0-9.]+) elapsed_ms=([0-9.]+)')|ForEach-Object {
        [ordered]@{id=[int]$_.Groups[1].Value;frame=[int]$_.Groups[2].Value;phase=[int]$_.Groups[3].Value;
            progress=[double]::Parse($_.Groups[4].Value,[Globalization.CultureInfo]::InvariantCulture);
            elapsed_ms=[double]::Parse($_.Groups[5].Value,[Globalization.CultureInfo]::InvariantCulture)}
    })
    if($Captures.Count -ne [Math]::Floor($Frames/$Every)){throw 'Incomplete capture sequence.'}
    for($i=0;$i -lt $Captures.Count;$i++) {
        $Capture=$Captures[$i];$Path=Join-Path $Out ('frame-{0:D6}.scene.ppm' -f $Capture.id)
        if($Capture.id -ne $i+1 -or $Capture.frame -ne ($i+1)*$Every -or -not (Test-Path -LiteralPath $Path)){throw 'Discontinuous native sequence.'}
        $Capture.file=[IO.Path]::GetFileName($Path)
        $Capture.sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    }
    if($Captures[0].phase -ne 0 -or $Captures[-1].phase -ne 4 -or $Captures[-1].progress -ne 1){throw 'Sequence does not cover idle through completed output.'}
    $Record.captures=$Captures;$Record.status='passed'
    Write-Host "[WEAVER-MOTION] PASS $($Captures.Count) actual native frames, $($Frames*16) ms; $Out"
} catch {$Record.status='failed';$Record.error=$_.Exception.Message;throw}
finally {
    if($Process -and -not $Process.HasExited){Stop-Process -Id $Process.Id -Force;$Process.WaitForExit()}
    $Record.finished_utc=[DateTime]::UtcNow.ToString('o')
    [IO.File]::WriteAllText((Join-Path $Out 'capture.json'),($Record|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
    Remove-Item Env:Path -ErrorAction SilentlyContinue;Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:PATH=$SavedPath
    foreach($Key in $Keys){[Environment]::SetEnvironmentVariable($Key,$Saved[$Key],'Process')}
}
if(-not $SkipPreview) {
    & python (Join-Path $PSScriptRoot 'mesh_weaver_motion_preview.py') $Out
    if($LASTEXITCODE -ne 0){throw 'Frames passed, but the optional animated preview failed.'}
}
