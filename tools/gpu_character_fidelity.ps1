[CmdletBinding()]
param(
    [string]$OutputDirectory='tmp/character-fidelity/native',
    [string]$Model='',
    [string[]]$Views=@('front','quarter','side'),
    [string[]]$Displays=@('unlit','parts','lit','smooth','soft','material'),
    [ValidateSet('float','quantized','legacy')][string]$Depth='float',
    [ValidateRange(128,4096)][int[]]$Distances=@(384),
    [ValidateRange(1,36000)][int]$Frames=2,
    [switch]$Reverse
)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Out=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Saved=@{}
foreach ($Key in @('RF_GPU_CHARACTER_MODEL','RF_GPU_CHARACTER_VIEW','RF_GPU_CHARACTER_DISPLAY',
    'RF_GPU_CHARACTER_DEPTH','RF_GPU_CHARACTER_DISTANCE','RF_GPU_CHARACTER_FREEZE','RF_GPU_CHARACTER_REVERSE')) {
    $Saved[$Key]=[Environment]::GetEnvironmentVariable($Key,'Process')
}
$Results=@()
$SavedPath=$env:Path
try {
    [Environment]::SetEnvironmentVariable('PATH',$null,'Process')
    [Environment]::SetEnvironmentVariable('Path',$SavedPath,'Process')
    $env:RF_GPU_CHARACTER_MODEL=if ($Model) { (Resolve-Path -LiteralPath $Model).Path } else { '' }
    $ModelPath=if ($Model) { $env:RF_GPU_CHARACTER_MODEL } else {
        Join-Path $Package 'rasterfall/private-assets/models/rf_c01_v023a.rmesh'
    }
    $ModelHeader=[IO.File]::ReadAllBytes($ModelPath)
    $PositionScale=[BitConverter]::ToUInt32($ModelHeader,16)
    if ($Depth -eq 'legacy' -and $PositionScale -ne 512) { throw 'Legacy transform requires a 512 units/metre model' }
    $env:RF_GPU_CHARACTER_DEPTH=$Depth
    $env:RF_GPU_CHARACTER_FREEZE='1'
    $env:RF_GPU_CHARACTER_REVERSE=if ($Reverse) { '1' } else { '0' }
    foreach ($View in $Views) { foreach ($Display in $Displays) { foreach ($Distance in $Distances) {
        if ($View -notin @('front','quarter','side','orbit') -or
            $Display -notin @('unlit','parts','lit','smooth','soft','material')) { throw 'Invalid view/display' }
        $env:RF_GPU_CHARACTER_VIEW=$View
        $env:RF_GPU_CHARACTER_DISPLAY=$Display
        $env:RF_GPU_CHARACTER_DISTANCE=[string]$Distance
        $Name="$View-$Display-$Distance-$Depth"
        $Capture=Join-Path $Out "$Name.bmp"
        $Argv=@('--renderer','gpu-scene','--map','rasterfall/assets/maps/outpost.map',
            '--gpu-normal-scene','model-lab','0','--gpu-normal-fixed-tick','--frames',"$Frames",
            '--frame-audit','--gpu-frame-capture',$Capture,'--gpu-capture-frame',"$Frames")
        $Quoted=($Argv | ForEach-Object { '"'+$_+'"' }) -join ' '
        $p=Start-Process -FilePath "$Package/rasterfall.exe" -WorkingDirectory $Package `
            -ArgumentList $Quoted -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput "$Out/$Name.out" -RedirectStandardError "$Out/$Name.err"
        $ProcessHandle=$p.Handle
        $Deadline=[DateTime]::UtcNow.AddMinutes(3)
        while (-not $p.WaitForExit(1000)) {
            if ([DateTime]::UtcNow -gt $Deadline) { Stop-Process -Id $p.Id -Force; throw "$Name timeout" }
        }
        if ($p.ExitCode -ne 0) { throw "$Name exit $($p.ExitCode)" }
        $Log=Get-Content -Encoding UTF8 "$Out/$Name.out","$Out/$Name.err" | Out-String
        if ($Log -notmatch 'SCENE-NATIVE.*bridges=0.*mixed_execute=0' -or
            $Log -match 'resource unavailable|Validation Error|SYNC-HAZARD|VUID-') { throw "$Name invalid native frame" }
        if (-not (Test-Path -LiteralPath "$Capture.scene.ppm")) { throw "$Name missing capture" }
        $Results+=@{view=$View;display=$Display;distance_rfu=$Distance;depth=$Depth;reverse=[bool]$Reverse;
            image="$Capture.scene.ppm";sha256=(Get-FileHash "$Capture.scene.ppm").Hash;exit_code=$p.ExitCode}
        Write-Host "[CHARACTER-FIDELITY] $Name PASS"
    } } }
    @{model=$ModelPath;model_sha256=(Get-FileHash $ModelPath).Hash;position_scale=$PositionScale;
      exe_sha256=(Get-FileHash "$Package/rasterfall.exe").Hash;
      camera_target_rfu=@(16000,-114,-31400);focal_over_width=0.75;captures=$Results} |
        ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 "$Out/manifest.json"
} finally {
    $env:Path=$SavedPath
    foreach ($Key in $Saved.Keys) { [Environment]::SetEnvironmentVariable($Key,$Saved[$Key],'Process') }
}
