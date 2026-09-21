[CmdletBinding()]
param(
    [string] $OutputDirectory = '',
    [int] $Frames = 120,
    [int] $WarmupFrames = 16,
    [ValidateSet('near', 'mid')]
    [string] $View = 'near'
)

$ErrorActionPreference = 'Stop'
$ToolPath = $env:Path
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $ToolPath, 'Process')
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package = Join-Path $Root 'build-windows/rasterfall-windows'
$Exe = Join-Path $Package 'rasterfall.exe'
$RuntimeLog = Join-Path $Package 'rasterfall.log'
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $Root ('tmp/hg5-character-baseline-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if ($Frames -le $WarmupFrames) { throw 'Frames must be greater than WarmupFrames.' }
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new output directory.' }
if (-not (Test-Path -LiteralPath $Exe)) { throw 'Run windows/NativeCodex.ps1 package first.' }
if (Get-Process -Name rasterfall -ErrorAction SilentlyContinue) {
    throw 'A rasterfall process is already running; stop it before collecting the serial HG-5 baseline.'
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null

$Runs = [Collections.Generic.List[object]]::new()
foreach ($EnemyCount in @(30, 60)) {
    $Name = "$View-$EnemyCount"
    $Stdout = Join-Path $OutputDirectory "$Name.stdout.txt"
    $Stderr = Join-Path $OutputDirectory "$Name.stderr.txt"
    $BeforeLines = if (Test-Path -LiteralPath $RuntimeLog) {
        @(Get-Content -LiteralPath $RuntimeLog -Encoding UTF8).Count
    } else { 0 }
    $Arguments = @(
        '--renderer', 'gpu-compute', '--gpu-required', '--gpu-native-present',
        '--gpu-normal-scene', $View, "$EnemyCount", '--frames', "$Frames", '--frame-audit'
    )
    $Quoted = ($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $Process = Start-Process -FilePath $Exe -ArgumentList $Quoted -WorkingDirectory $Package `
        -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr
    if ($Process.ExitCode -ne 0) { throw "$Name exited $($Process.ExitCode)." }
    if (-not (Test-Path -LiteralPath $RuntimeLog)) { throw "$Name did not produce rasterfall.log." }
    $RunLog = Join-Path $OutputDirectory "$Name.runtime.log"
    @(Get-Content -LiteralPath $RuntimeLog -Encoding UTF8 | Select-Object -Skip $BeforeLines) |
        Set-Content -LiteralPath $RunLog -Encoding UTF8
    $Lines = @(Get-Content -LiteralPath $RunLog -Encoding UTF8)
    $StdoutLines = @(Get-Content -LiteralPath $Stdout -Encoding UTF8)
    if (-not ($StdoutLines -match "GPU-NORMAL scene=$View enemies=$EnemyCount seed=1")) {
        throw "$Name did not confirm the requested deterministic fixture."
    }
    $Headers = @($Lines | Where-Object { $_ -match '^FRAME-AUDIT frame=' })
    if ($Headers.Count -ne $Frames) { throw "$Name expected $Frames frame audits, found $($Headers.Count)." }
    $NonNative = @($Headers | Where-Object { $_ -notmatch ' path=gpu-native ' })
    if ($NonNative.Count) { throw "$Name contains $($NonNative.Count) non-native frames." }
    $GpuAudit = @($Lines | Where-Object { $_ -match '^FRAME-AUDIT gpu ' })
    $LayerAudit = @($Lines | Where-Object { $_ -match '^FRAME-AUDIT layers ' })
    $CharacterAudit = @($Lines | Where-Object { $_ -match '^FRAME-AUDIT character-draw ' })
    if ($GpuAudit.Count -ne $Frames -or $LayerAudit.Count -ne $Frames -or
        $CharacterAudit.Count -ne $Frames) {
        throw "$Name has incomplete GPU/layer/character audit rows."
    }
    $ForbiddenGpu = @($GpuAudit | Where-Object {
        $_ -notmatch 'readback_bytes=0' -or $_ -notmatch 'cpu_framebuffer_copy_bytes=0'
    })
    $ForbiddenFallback = @($LayerAudit | Where-Object {
        $_ -notmatch 'pre_post_cpu_fallback=0' -or $_ -notmatch 'fallback_reason=0x0'
    })
    if ($ForbiddenGpu.Count -or $ForbiddenFallback.Count) {
        throw "$Name violated fallback/readback/copy gates."
    }
    $InvalidCharacter = @($CharacterAudit | Where-Object {
        if ($_ -notmatch 'instances=(\d+) items=(\d+) triangles=(\d+) upload_vertices=(\d+) legacy_items=(\d+)') {
            return $true
        }
        [int64]$Matches[1] -le 0 -or [int64]$Matches[2] -le 0 -or
            [int64]$Matches[3] -le 0 -or
            [int64]$Matches[4] -ne [int64]$Matches[3] * 3
    })
    if ($InvalidCharacter.Count) { throw "$Name contains invalid HG-5A character Draw rows." }
    $Metrics = Join-Path $OutputDirectory "$Name.metrics.json"
    & powershell -ExecutionPolicy Bypass -File (Join-Path $Root 'tools/hardware_graphics_metrics.ps1') `
        -LogPath $RunLog -WarmupFrames $WarmupFrames -ExpectedFrames $Frames `
        -ExpectedPath 'gpu-native' -OutputJson $Metrics | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "$Name metrics failed with exit code $LASTEXITCODE." }
    $Runs.Add([ordered]@{
        name = $Name
        enemies = $EnemyCount
        frames = $Frames
        warmup_frames = $WarmupFrames
        argv = $Arguments
        runtime_log = [IO.Path]::GetFileName($RunLog)
        metrics = [IO.Path]::GetFileName($Metrics)
    })
}

$Manifest = [ordered]@{
    schema = 1
    checkpoint = 'HG-5A-character-baseline'
    created_at = (Get-Date).ToString('o')
    executable = $Exe
    executable_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $Exe).Hash
    view = $View
    result = 'PASS'
    runs = $Runs
    notes = @(
        'Runs are serial because Windows native-present evidence must not overlap.',
        'This gate measures CPU frontend and GPU mixed-frame cost after the HG-5A dynamic character Draw vertical slice.',
        'Use the metrics JSON files to compare enemies_ms, whole_loop_ms and GPU raster/draw timestamps.'
    )
}
$Manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'manifest.json') -Encoding UTF8
Write-Host "[HG-5] 30/60 character baseline complete: $OutputDirectory"
