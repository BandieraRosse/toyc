[CmdletBinding()]
param(
    [ValidateRange(1, 20)]
    [int] $Rounds = 1,
    [string] $OutputDirectory = '',
    [string] $DriverLibraryPath = '',
    [string] $ExecutablePath = '',
    [ValidateRange(30,10000)][int] $NearFrames = 120,
    [ValidateSet('all','near-0','near-30','near-60','campaign-320')][string] $Scene = 'all',
    [switch] $NoAudit
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package = Join-Path $Root 'build-windows/rasterfall-windows'
$Exe = if ($ExecutablePath) { (Resolve-Path -LiteralPath $ExecutablePath).Path } else { Join-Path $Package 'rasterfall.exe' }
if ([IO.Path]::GetDirectoryName($Exe) -ne $Package) {
    throw 'The executable must be in the package directory: Windows resolves assets relative to its executable.'
}
$RuntimeLog = Join-Path $Package 'rasterfall.log'
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $Root ('tmp/gpu-rb0-sampling-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new output directory.' }
if (-not (Test-Path -LiteralPath $Exe)) {
    throw 'Missing packaged rasterfall.exe. Run windows/NativeCodex.ps1 package first.'
}
if (Get-Process -Name @('rasterfall', [IO.Path]::GetFileNameWithoutExtension($Exe)) -ErrorAction SilentlyContinue) {
    throw 'A rasterfall process is already running; stop it before starting serial RB-0 sampling.'
}

$ToolPath = $env:Path
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $ToolPath, 'Process')
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$Runs = [Collections.Generic.List[object]]::new()
$Mode = if ($NoAudit) { 'no-audit' } else { 'audit' }
$Manifest = [ordered]@{
    schema = 1
    suite = 'gpu-rb0-sampling'
    mode = $Mode
    rounds = $Rounds
    near_frames = $NearFrames
    selected_scene = $Scene
    started_at = (Get-Date).ToString('o')
    commit = (& git -C $Root rev-parse HEAD)
    worktree = @(& git -C $Root status --short)
    package_exe = [ordered]@{
        path = $Exe
        length = (Get-Item -LiteralPath $Exe).Length
        last_write_time = (Get-Item -LiteralPath $Exe).LastWriteTime.ToString('o')
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $Exe).Hash
    }
    power_source = 'unknown'
    runs = $Runs
    result = 'FAIL'
}
try {
    $Battery = Get-CimInstance Win32_Battery -ErrorAction Stop | Select-Object -First 1
    if ($null -eq $Battery) { $Manifest.power_source = 'ac-no-battery-reported' }
    elseif ($Battery.BatteryStatus -eq 2) { $Manifest.power_source = 'ac-charging-or-full' }
    else { $Manifest.power_source = "battery-status-$($Battery.BatteryStatus)" }
} catch {
    $Manifest.power_query_error = $_.Exception.Message
}
try { $Manifest.adapters = @(Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion, PNPDeviceID) }
catch { $Manifest.adapter_query_error = $_.Exception.Message }
try { $Manifest.active_power_scheme = (& powercfg /getactivescheme 2>&1 | Out-String).Trim() }
catch { $Manifest.power_scheme_query_error = $_.Exception.Message }
if (-not ('RfRb0Power' -as [type])) {
    Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public class RfRb0Power { [StructLayout(LayoutKind.Sequential)] public struct Status { public byte ACLineStatus, BatteryFlag, BatteryLifePercent, SystemStatusFlag; public uint BatteryLifeTime, BatteryFullLifeTime; } [DllImport("kernel32.dll")] public static extern bool GetSystemPowerStatus(out Status status); }'
}
function Read-Power {
    $Status = New-Object RfRb0Power+Status
    if (-not [RfRb0Power]::GetSystemPowerStatus([ref]$Status)) { throw 'GetSystemPowerStatus failed.' }
    return [int]$Status.ACLineStatus
}
$Manifest.ac_line_status = Read-Power
$Manifest.power_source = switch ($Manifest.ac_line_status) { 0 { 'battery' } 1 { 'ac' } default { 'unknown' } }
if ($DriverLibraryPath) {
    $Driver = Get-Item -LiteralPath $DriverLibraryPath
    $Manifest.driver_library = [ordered]@{ path=$Driver.FullName; file_version=$Driver.VersionInfo.FileVersion; sha256=(Get-FileHash -LiteralPath $Driver.FullName).Hash; source='explicit path from validation loader evidence' }
}

function Save-Manifest {
    $Manifest | ConvertTo-Json -Depth 12 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $OutputDirectory 'manifest.json')
}

function Run-HiddenProcess([string] $Name, [string] $Program, [string[]] $Arguments, [string] $WorkingDirectory) {
    if ((powercfg /getactivescheme | Out-String).Trim() -ne $Manifest.active_power_scheme -or
        (Read-Power) -ne $Manifest.ac_line_status) { throw 'Power scheme or AC state changed during sampling.' }
    Write-Host "[RB0] $Name"
    $Stdout = Join-Path $OutputDirectory "$Name.stdout.txt"
    $Stderr = Join-Path $OutputDirectory "$Name.stderr.txt"
    foreach ($Argument in $Arguments) { if ($Argument.Contains('"')) { throw 'Embedded quotes are not supported.' } }
    $Quoted = ($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $Start = @{
        FilePath = $Program
        WorkingDirectory = $WorkingDirectory
        WindowStyle = 'Hidden'
        Wait = $true
        PassThru = $true
        RedirectStandardOutput = $Stdout
        RedirectStandardError = $Stderr
    }
    if ($Quoted) { $Start.ArgumentList = $Quoted }
    $Process = Start-Process @Start
    if ($Process.ExitCode -ne 0) { throw "$Name exited $($Process.ExitCode)." }
}

function Run-Sample([int] $Round, [string] $Scene, [string[]] $Arguments, [int] $Frames, [switch] $Campaign) {
    $Name = ('round-{0:D2}-{1}' -f $Round, $Scene)
    $Before = if (Test-Path -LiteralPath $RuntimeLog) { @(Get-Content -Encoding UTF8 -LiteralPath $RuntimeLog).Count } else { 0 }
    Run-HiddenProcess $Name $Exe $Arguments $Package
    $StdoutPath = Join-Path $OutputDirectory "$Name.stdout.txt"
    if ($NoAudit) {
        $Stdout = @(Get-Content -Encoding UTF8 -LiteralPath $StdoutPath)
        $Stats = @($Stdout | Select-String '^\[stats:total\] window=')
        $Rb0 = @($Stdout | Select-String '^RB0-STATS ')
        $Coverage = @($Stdout | Select-String '^RB0-COVERAGE ')
        $ExpectedComplete = $Frames - 18
        if ($Coverage.Count -ne 1 -or $Coverage[0].Line -notmatch "complete=$ExpectedComplete incomplete=0 pending=2 dropped=0 ") {
            throw "$Name has incomplete GPU timestamp coverage."
        }
        $Gpu = @($Stdout | Select-String '^GPU-FRAME attempted=')
        $Native = @($Stdout | Select-String '^GPU-NATIVE ')
        if ($Stats.Count -ne 1 -or $Rb0.Count -ne 1 -or $Gpu.Count -ne 1 -or $Native.Count -ne 1) {
            throw "$Name has incomplete no-audit summary output."
        }
        if ($Gpu[0].Line -notmatch "attempted=$Frames rendered=$Frames cpu_fallback=0") {
            throw "$Name did not render every frame through the required GPU path."
        }
        if ($Native[0].Line -notmatch 'color-readback=0 cpu-framebuffer-copy=0') {
            throw "$Name performed a readback or CPU framebuffer copy."
        }
        if ($Stats[0].Line -notmatch 'frame_us avg=(\d+) p95=(\d+) p99=(\d+) max=(\d+) wall_us avg=(\d+) wait_us avg=(\d+)') {
            throw "$Name has a malformed no-audit timing summary."
        }
        $StatsValues = @($Matches[1..6] | ForEach-Object { [int64]$_ })
        if ($Rb0[0].Line -notmatch 'warmup=(\d+) frames=(\d+) whole_us median=(\d+) p95=(\d+) p99=(\d+) max=(\d+) raster_us p95=(\d+) cpu_producer_us p95=(\d+) wait_us p95=(\d+) acquire_us p95=(\d+) present_us p95=(\d+)(?: gpu_raster_us median=(\d+) p95=(\d+) p99=(\d+) gpu_draw_us median=(\d+) p95=(\d+) p99=(\d+) gpu_bridge_us median=(\d+) p95=(\d+) p99=(\d+) slot_wait_us median=(\d+) p95=(\d+) p99=(\d+) presenter_completion_us median=(\d+) p95=(\d+) p99=(\d+) unattributed_us median=(\d+) p95=(\d+) p99=(\d+))?') {
            throw "$Name has a malformed RB-0 timing summary."
        }
        $Rb0Values = @($Matches[1..29] | ForEach-Object { if ($_ -eq '') { 0 } else { [int64]$_ } })
        $Runs.Add([pscustomobject][ordered]@{
            round = $Round
            scene = $Scene
            frames = $Frames
            frame_average_ms = $StatsValues[0] / 1000.0
            frame_p95_ms = $StatsValues[1] / 1000.0
            frame_p99_ms = $StatsValues[2] / 1000.0
            frame_maximum_ms = $StatsValues[3] / 1000.0
            wall_average_ms = $StatsValues[4] / 1000.0
            wait_average_ms = $StatsValues[5] / 1000.0
            rb0_warmup_frames = $Rb0Values[0]
            rb0_measured_frames = $Rb0Values[1]
            rb0_whole_median_ms = $Rb0Values[2] / 1000.0
            rb0_whole_p95_ms = $Rb0Values[3] / 1000.0
            rb0_whole_p99_ms = $Rb0Values[4] / 1000.0
            rb0_whole_maximum_ms = $Rb0Values[5] / 1000.0
            rb0_raster_p95_ms = $Rb0Values[6] / 1000.0
            rb0_cpu_producer_p95_ms = $Rb0Values[7] / 1000.0
            rb0_wait_p95_ms = $Rb0Values[8] / 1000.0
            rb0_acquire_p95_ms = $Rb0Values[9] / 1000.0
            rb0_present_p95_ms = $Rb0Values[10] / 1000.0
            rb0_gpu_raster_median_ms = $Rb0Values[11] / 1000.0
            rb0_gpu_raster_p95_ms = $Rb0Values[12] / 1000.0
            rb0_gpu_raster_p99_ms = $Rb0Values[13] / 1000.0
            rb0_gpu_draw_median_ms = $Rb0Values[14] / 1000.0
            rb0_gpu_draw_p95_ms = $Rb0Values[15] / 1000.0
            rb0_gpu_draw_p99_ms = $Rb0Values[16] / 1000.0
            rb0_gpu_bridge_median_ms = $Rb0Values[17] / 1000.0
            rb0_gpu_bridge_p95_ms = $Rb0Values[18] / 1000.0
            rb0_gpu_bridge_p99_ms = $Rb0Values[19] / 1000.0
            rb0_slot_wait_median_ms = $Rb0Values[20] / 1000.0
            rb0_slot_wait_p95_ms = $Rb0Values[21] / 1000.0
            rb0_slot_wait_p99_ms = $Rb0Values[22] / 1000.0
            rb0_presenter_completion_median_ms = $Rb0Values[23] / 1000.0
            rb0_presenter_completion_p95_ms = $Rb0Values[24] / 1000.0
            rb0_presenter_completion_p99_ms = $Rb0Values[25] / 1000.0
            rb0_unattributed_median_ms = $Rb0Values[26] / 1000.0
            rb0_unattributed_p95_ms = $Rb0Values[27] / 1000.0
            rb0_unattributed_p99_ms = $Rb0Values[28] / 1000.0
        })
        Save-Manifest
        return
    }
    $Log = if (Test-Path -LiteralPath $RuntimeLog) {
        @(Get-Content -Encoding UTF8 -LiteralPath $RuntimeLog | Select-Object -Skip $Before)
    } else { @() }
    $LogPath = Join-Path $OutputDirectory "$Name.runtime.log"
    $Log | Set-Content -Encoding UTF8 -LiteralPath $LogPath
    $FrameRows = @($Log | Select-String '^FRAME-AUDIT frame=')
    $GpuRows = @($Log | Select-String '^FRAME-AUDIT gpu ')
    $PresentRows = @($Log | Select-String '^PRESENT-AUDIT frame=')
    if ($FrameRows.Count -ne $Frames -or $GpuRows.Count -ne $Frames -or $PresentRows.Count -ne $Frames) {
        throw "$Name has an incomplete frame/presenter audit."
    }
    foreach ($Line in $FrameRows) {
        if ($Line.Line -notmatch 'path=gpu-native ') { throw "$Name used a non-native render path." }
    }
    foreach ($Line in $GpuRows) {
        if ($Line.Line -notmatch 'readback_bytes=0 cpu_framebuffer_copy_bytes=0') {
            throw "$Name performed a readback or CPU framebuffer copy."
        }
    }
    foreach ($Line in $PresentRows) {
        if ($Line.Line -notmatch 'hot_queue_idle_count=0 .*presenter_poisoned=0 ') {
            throw "$Name used hot queue-idle or poisoned the presenter."
        }
    }

    $MetricsPath = Join-Path $OutputDirectory "$Name.metrics.json"
    $MetricArguments = @(
        '-ExecutionPolicy','Bypass','-File',(Join-Path $Root 'tools/gpu_metrics.ps1'),
        '-LogPath',$LogPath,'-WarmupFrames','16','-ExpectedFrames',"$Frames",
        '-ExpectedPath','gpu-native','-RequireFixedTick','-OutputJson',$MetricsPath
    )
    if ($Campaign) { $MetricArguments += '-RequireCampaignLoad' }
    Run-HiddenProcess "$Name-metrics" 'powershell.exe' $MetricArguments $Root
    $Metrics = Get-Content -Raw -Encoding UTF8 -LiteralPath $MetricsPath | ConvertFrom-Json
    $Record = [pscustomobject][ordered]@{
        round = $Round
        scene = $Scene
        frames = $Frames
        metrics = [IO.Path]::GetFileName($MetricsPath)
        whole_loop_median_ms = $Metrics.cpu_wall.whole_loop_ms.median
        whole_loop_p95_ms = $Metrics.cpu_wall.whole_loop_ms.p95
        whole_loop_p99_ms = $Metrics.cpu_wall.whole_loop_ms.p99
        gpu_raster_median_ms = $Metrics.gpu_timestamp.raster_ms.median
        gpu_raster_p95_ms = $Metrics.gpu_timestamp.raster_ms.p95
        gpu_bridge_import_median_ms = $Metrics.gpu_timestamp.bridge_import_ms.median
        gpu_bridge_export_median_ms = $Metrics.gpu_timestamp.bridge_export_ms.median
        bridge_transfers = $Metrics.fixed_workload.count_ranges.bridge_transfers
        bridge_bytes = $Metrics.fixed_workload.count_ranges.bridge_bytes
        workload_sequence_sha256 = $Metrics.fixed_workload.normalized_sequence_sha256
    }
    $Runs.Add($Record)
    Save-Manifest
}

try {
    for ($Round = 1; $Round -le $Rounds; $Round++) {
        foreach ($Enemies in @(0, 30, 60)) {
            if ($Scene -ne 'all' -and $Scene -ne "near-$Enemies") { continue }
            $NearArguments = @(
                '--renderer','gpu-compute','--gpu-required','--gpu-native-present',
                '--gpu-normal-scene','near',"$Enemies",'--gpu-normal-fixed-tick'
            )
            if (-not $NoAudit) { $NearArguments += '--frame-audit' }
            if ($NoAudit) { $NearArguments += '--gpu-rb0-stats' }
            $NearArguments += @('--frames',"$NearFrames")
            Run-Sample $Round "near-$Enemies" $NearArguments $NearFrames
        }
        if ($Scene -ne 'all' -and $Scene -ne 'campaign-320') { continue }
        $CampaignArguments = @(
            '--map','rasterfall/assets/maps/rasterfall.map','--gpu-wave-repro',
            '--gpu-normal-fixed-tick','--renderer','gpu-compute','--gpu-required',
            '--gpu-native-present'
        )
        if (-not $NoAudit) { $CampaignArguments += '--frame-audit' }
        if ($NoAudit) { $CampaignArguments += '--gpu-rb0-stats' }
        $CampaignArguments += @('--frames','320')
        Run-Sample $Round 'campaign-320' $CampaignArguments 320 -Campaign
    }
    if ((Get-FileHash -LiteralPath $Exe).Hash -ne $Manifest.package_exe.sha256) { throw 'Package changed during sampling.' }
    if ((powercfg /getactivescheme | Out-String).Trim() -ne $Manifest.active_power_scheme -or
        (Read-Power) -ne $Manifest.ac_line_status) { throw 'Power scheme or AC state changed during sampling.' }
    $Manifest.result = 'PASS'
} catch {
    $Manifest.error = $_.Exception.Message
    throw
} finally {
    $Manifest.finished_at = (Get-Date).ToString('o')
    Save-Manifest
}

$Summary = $Runs | Group-Object scene | ForEach-Object {
    $Rows = @($_.Group)
    $SceneSummary = [ordered]@{
        scene = $_.Name
        rounds = $Rows.Count
    }
    if ($NoAudit) {
        $SceneSummary.frame_average_ms = [ordered]@{
            minimum = ($Rows.frame_average_ms | Measure-Object -Minimum).Minimum
            median = ($Rows.frame_average_ms | Sort-Object)[[Math]::Floor(($Rows.Count - 1) / 2)]
            maximum = ($Rows.frame_average_ms | Measure-Object -Maximum).Maximum
        }
        $SceneSummary.frame_p95_ms = [ordered]@{
            minimum = ($Rows.frame_p95_ms | Measure-Object -Minimum).Minimum
            maximum = ($Rows.frame_p95_ms | Measure-Object -Maximum).Maximum
        }
        $SceneSummary.rb0_whole_median_ms = [ordered]@{
            minimum = ($Rows.rb0_whole_median_ms | Measure-Object -Minimum).Minimum
            median = ($Rows.rb0_whole_median_ms | Sort-Object)[[Math]::Floor(($Rows.Count - 1) / 2)]
            maximum = ($Rows.rb0_whole_median_ms | Measure-Object -Maximum).Maximum
        }
        $SceneSummary.rb0_whole_p95_ms = [ordered]@{
            minimum = ($Rows.rb0_whole_p95_ms | Measure-Object -Minimum).Minimum
            maximum = ($Rows.rb0_whole_p95_ms | Measure-Object -Maximum).Maximum
        }
        $SceneSummary.rb0_whole_p99_ms = [ordered]@{
            minimum = ($Rows.rb0_whole_p99_ms | Measure-Object -Minimum).Minimum
            maximum = ($Rows.rb0_whole_p99_ms | Measure-Object -Maximum).Maximum
        }
    } else {
        $SceneSummary.whole_loop_median_ms = [ordered]@{
            minimum = ($Rows.whole_loop_median_ms | Measure-Object -Minimum).Minimum
            median = ($Rows.whole_loop_median_ms | Sort-Object)[[Math]::Floor(($Rows.Count - 1) / 2)]
            maximum = ($Rows.whole_loop_median_ms | Measure-Object -Maximum).Maximum
        }
        $SceneSummary.whole_loop_p95_ms = [ordered]@{
            minimum = ($Rows.whole_loop_p95_ms | Measure-Object -Minimum).Minimum
            maximum = ($Rows.whole_loop_p95_ms | Measure-Object -Maximum).Maximum
        }
        $SceneSummary.gpu_raster_median_ms = [ordered]@{
            minimum = ($Rows.gpu_raster_median_ms | Measure-Object -Minimum).Minimum
            maximum = ($Rows.gpu_raster_median_ms | Measure-Object -Maximum).Maximum
        }
        $SceneSummary.bridge_transfers_distinct = @($Rows.bridge_transfers | ConvertTo-Json -Compress | Sort-Object -Unique)
        $SceneSummary.bridge_bytes_distinct = @($Rows.bridge_bytes | ConvertTo-Json -Compress | Sort-Object -Unique)
        $Hashes = @($Rows.workload_sequence_sha256 | Sort-Object -Unique)
        $SceneSummary.workload_sequence_hashes = $Hashes
        if ($Hashes.Count -eq 1) {
            $SceneSummary.workload_sequence = 'identical'
        } else {
            $SceneSummary.workload_sequence = 'diverged'
            $Baseline = Get-Content -Raw -Encoding UTF8 -LiteralPath `
                (Join-Path $OutputDirectory $Rows[0].metrics) | ConvertFrom-Json
            $FirstDivergence = $null
            foreach ($Row in @($Rows | Select-Object -Skip 1)) {
                $Candidate = Get-Content -Raw -Encoding UTF8 -LiteralPath `
                    (Join-Path $OutputDirectory $Row.metrics) | ConvertFrom-Json
                $Limit = [Math]::Min(
                    $Baseline.fixed_workload.normalized_frame_sha256.Count,
                    $Candidate.fixed_workload.normalized_frame_sha256.Count)
                for ($FrameIndex = 0; $FrameIndex -lt $Limit; $FrameIndex++) {
                    if ($Baseline.fixed_workload.normalized_frame_sha256[$FrameIndex] -ne
                        $Candidate.fixed_workload.normalized_frame_sha256[$FrameIndex]) {
                        $FirstDivergence = [ordered]@{
                            baseline_round = $Rows[0].round
                            candidate_round = $Row.round
                            frame = $Baseline.fixed_workload.normalized_frames[$FrameIndex].frame
                            baseline = $Baseline.fixed_workload.normalized_frames[$FrameIndex]
                            candidate = $Candidate.fixed_workload.normalized_frames[$FrameIndex]
                        }
                        break
                    }
                }
                if ($null -eq $FirstDivergence -and
                    $Baseline.fixed_workload.normalized_frame_sha256.Count -ne
                    $Candidate.fixed_workload.normalized_frame_sha256.Count) {
                    $FirstDivergence = [ordered]@{
                        baseline_round = $Rows[0].round
                        candidate_round = $Row.round
                        frame = $Limit + 1
                        baseline_frame_count = $Baseline.fixed_workload.normalized_frame_sha256.Count
                        candidate_frame_count = $Candidate.fixed_workload.normalized_frame_sha256.Count
                    }
                }
                if ($null -ne $FirstDivergence) { break }
            }
            $SceneSummary.first_workload_divergence = $FirstDivergence
        }
    }
    $SceneSummary
}
[ordered]@{ schema = 1; suite = 'gpu-rb0-sampling'; mode = $Mode; scenes = @($Summary) } |
    ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $OutputDirectory 'summary.json')
Write-Host "RB-0 sampling PASS: $OutputDirectory"
