[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $LogPath,
    [int] $WarmupFrames = 16,
    [int] $ExpectedFrames = 0,
    [string] $ExpectedPath = '',
    [switch] $RequireCampaignLoad,
    [int] $RunFromEnd = 1,
    [string] $OutputJson = ''
)

$ErrorActionPreference = 'Stop'
$Invariant = [Globalization.CultureInfo]::InvariantCulture
$ResolvedLog = (Resolve-Path -LiteralPath $LogPath).Path
$Frames = [Collections.Generic.List[object]]::new()
$GpuFrames = [Collections.Generic.List[object]]::new()
$Current = $null
$AllLines = @(Get-Content -LiteralPath $ResolvedLog -Encoding UTF8)
$RunStarts = [Collections.Generic.List[int]]::new()
for ($Index = 0; $Index -lt $AllLines.Count; $Index++) {
    if ($AllLines[$Index] -eq 'startup: first frame begin') { $RunStarts.Add($Index) }
}
if ($RunStarts.Count) {
    if ($RunFromEnd -lt 1 -or $RunFromEnd -gt $RunStarts.Count) {
        throw "RunFromEnd must be between 1 and $($RunStarts.Count)."
    }
    $SelectedStartIndex = $RunStarts.Count - $RunFromEnd
    $Start = $RunStarts[$SelectedStartIndex]
    $End = if ($SelectedStartIndex + 1 -lt $RunStarts.Count) {
        $RunStarts[$SelectedStartIndex + 1] - 1
    } else { $AllLines.Count - 1 }
    $SelectedLines = @($AllLines[$Start..$End])
} else {
    $SelectedStartIndex = 0
    $SelectedLines = $AllLines
}

function Number([string] $Line, [string] $Name) {
    if ($Line -match ("(?:^| )" + [regex]::Escape($Name) + '=(-?[0-9]+(?:\.[0-9]+)?)')) {
        return [double]::Parse($Matches[1], $Invariant)
    }
    return $null
}

function Percentile([double[]] $Values, [double] $Fraction) {
    if (-not $Values.Count) { return $null }
    $Sorted = @($Values | Sort-Object)
    return $Sorted[[Math]::Min($Sorted.Count - 1, [Math]::Ceiling($Sorted.Count * $Fraction) - 1)]
}

function Statistics([object[]] $Rows, [string] $Field) {
    $Values = @($Rows | ForEach-Object {
        $Value = $_.$Field
        if ($null -ne $Value) { [double]$Value }
    })
    if (-not $Values.Count) { return $null }
    $Sorted = @($Values | Sort-Object)
    $Middle = [Math]::Floor(($Sorted.Count - 1) / 2)
    $Median = if (($Sorted.Count % 2) -eq 0) {
        ($Sorted[$Middle] + $Sorted[$Middle + 1]) / 2.0
    } else { $Sorted[$Middle] }
    return [ordered]@{
        count = $Values.Count
        mean = ($Values | Measure-Object -Average).Average
        median = $Median
        p95 = Percentile $Values 0.95
        maximum = ($Values | Measure-Object -Maximum).Maximum
    }
}

foreach ($Line in $SelectedLines) {
    if ($Line -match '^FRAME-AUDIT frame=(\d+) path=([^ ]+) world=(\d+) ') {
        $Current = [pscustomobject][ordered]@{
            frame = [int]$Matches[1]
            path = $Matches[2]
            world = [int]$Matches[3]
            render_ms = Number $Line 'render_ms'
            present_wall_ms = Number $Line 'present_wall_ms'
            whole_loop_ms = Number $Line 'whole_loop_ms'
            frame_interval_ms = Number $Line 'frame_interval_ms'
            scene_ms = $null
            static_ms = $null
            enemies_ms = $null
            ai_teammates_ms = $null
            enemies_cmd = $null
            mixed_preflight_ms = $null
            mixed_freeze_ms = $null
            mixed_cache_collect_ms = $null
            mixed_texture_measure_ms = $null
            mixed_pack_ms = $null
            mixed_draw_encode_ms = $null
            mixed_draw_batch_prepare_ms = $null
            mixed_graphics_draw_ms = $null
            mixed_raster_segment_ms = $null
            native_acquire_ms = $null
            native_present_ms = $null
            native_queue_idle_ms = $null
        }
        $Frames.Add($Current)
        continue
    }
    if ($Line -match '^FRAME-AUDIT mixed-gpu frame=(\d+) supported=1 valid=1 ') {
        $GpuFrames.Add([pscustomobject][ordered]@{
            frame = [int]$Matches[1]
            raster_ms = Number $Line 'raster_ms'
            bridge_import_ms = Number $Line 'bridge_import_ms'
            draw_ms = Number $Line 'draw_ms'
            bridge_export_ms = Number $Line 'bridge_export_ms'
            post_ms = Number $Line 'post_ms'
            overlay_ms = Number $Line 'overlay_ms'
            present_copy_ms = Number $Line 'present_copy_ms'
        })
        continue
    }
    if ($null -eq $Current) { continue }
    if ($Line -match '^FRAME-AUDIT scene ') {
        $Current.static_ms = Number $Line 'static_ms'
        $Current.scene_ms =
            (Number $Line 'sky_floor_ms') + (Number $Line 'map_ms') +
            (Number $Line 'static_ms') + (Number $Line 'gallery_ms') +
            (Number $Line 'character_private_ms') + (Number $Line 'projectiles_ms')
    } elseif ($Line -match '^FRAME-AUDIT world-submission ') {
        $Current.enemies_cmd = Number $Line 'enemies_cmd'
        $Current.enemies_ms = Number $Line 'enemies_ms'
        $Current.ai_teammates_ms = Number $Line 'ai_teammates_ms'
    } elseif ($Line -match '^FRAME-AUDIT mixed-cpu ') {
        $Current.mixed_freeze_ms = Number $Line 'freeze_ms'
        $Current.mixed_cache_collect_ms = Number $Line 'cache_collect_ms'
        $Current.mixed_preflight_ms = Number $Line 'preflight_ms'
        $Current.mixed_texture_measure_ms = Number $Line 'texture_measure_ms'
        $Current.mixed_pack_ms = Number $Line 'pack_ms'
        $Current.mixed_draw_encode_ms = Number $Line 'draw_encode_ms'
        $Current.mixed_draw_batch_prepare_ms = Number $Line 'draw_batch_prepare_ms'
        $Current.mixed_graphics_draw_ms = Number $Line 'graphics_draw_ms'
        $Current.mixed_raster_segment_ms = Number $Line 'raster_segment_ms'
    } elseif ($Line -match '^FRAME-AUDIT gpu ') {
        $Current.native_acquire_ms = Number $Line 'native_acquire_ms'
        $Current.native_present_ms = Number $Line 'native_present_ms'
        $Current.native_queue_idle_ms = Number $Line 'native_present_queue_idle_ms'
    }
}

if ($ExpectedFrames -and $Frames.Count -ne $ExpectedFrames) {
    throw "Expected $ExpectedFrames frame audits, found $($Frames.Count)."
}
if ($ExpectedPath) {
    $BadPath = @($Frames | Where-Object { $_.path -ne $ExpectedPath })
    if ($BadPath.Count) { throw "Found $($BadPath.Count) frames outside path $ExpectedPath." }
}
if ($RequireCampaignLoad) {
    $WrongWorld = @($Frames | Where-Object { $_.world -ne 1 })
    if ($WrongWorld.Count) { throw "Campaign gate failed: $($WrongWorld.Count) frames have world != 1." }
    $EnemyFrames = @($Frames | Where-Object { $_.enemies_cmd -gt 0 })
    if (-not $EnemyFrames.Count) { throw 'Campaign gate failed: no frame submitted enemy commands.' }
}

$Measured = @($Frames | Where-Object { $_.frame -gt $WarmupFrames })
if (-not $Measured.Count) { throw 'No measured frames remain after warmup.' }
$MeasuredGpu = @($GpuFrames | Where-Object { $_.frame -gt $WarmupFrames })

# frame_interval[N] spans begin(N-1) -> begin(N).  Subtract the previous
# whole-loop sample, not the current one, to estimate audit/log/scheduler gap.
$ByFrame = @{}
foreach ($Frame in $Frames) { $ByFrame[$Frame.frame] = $Frame }
$AuditGaps = [Collections.Generic.List[object]]::new()
foreach ($Frame in $Measured) {
    $Previous = $ByFrame[$Frame.frame - 1]
    if ($null -ne $Previous -and $Frame.frame_interval_ms -gt 0) {
        $AuditGaps.Add([pscustomobject]@{
            frame = $Frame.frame
            audit_and_scheduler_gap_ms = $Frame.frame_interval_ms - $Previous.whole_loop_ms
        })
    }
}

$CpuFields = @(
    'render_ms','present_wall_ms','whole_loop_ms','frame_interval_ms','scene_ms','static_ms',
    'enemies_ms','ai_teammates_ms','mixed_freeze_ms','mixed_cache_collect_ms',
    'mixed_preflight_ms','mixed_texture_measure_ms','mixed_pack_ms',
    'mixed_draw_encode_ms','mixed_draw_batch_prepare_ms','mixed_graphics_draw_ms',
    'mixed_raster_segment_ms',
    'native_acquire_ms','native_present_ms','native_queue_idle_ms'
)
$GpuFields = @(
    'raster_ms','bridge_import_ms','draw_ms','bridge_export_ms','post_ms',
    'overlay_ms','present_copy_ms'
)
$CpuStats = [ordered]@{}
foreach ($Field in $CpuFields) { $CpuStats[$Field] = Statistics $Measured $Field }
$GpuStats = [ordered]@{}
foreach ($Field in $GpuFields) { $GpuStats[$Field] = Statistics $MeasuredGpu $Field }

$Result = [ordered]@{
    schema = 1
    log = $ResolvedLog
    selected_run_from_end = $RunFromEnd
    warmup_frames = $WarmupFrames
    total_frames = $Frames.Count
    measured_frame_range = "$($WarmupFrames + 1)-$(($Frames | Select-Object -Last 1).frame)"
    measured_frames = $Measured.Count
    gpu_timestamp_frames = $MeasuredGpu.Count
    paths = @($Frames | Group-Object path | ForEach-Object { [ordered]@{ path = $_.Name; frames = $_.Count } })
    worlds = @($Frames | Group-Object world | ForEach-Object { [ordered]@{ world = [int]$_.Name; frames = $_.Count } })
    enemy_command_frames = @($Frames | Where-Object { $_.enemies_cmd -gt 0 }).Count
    cpu_wall = $CpuStats
    gpu_timestamp = $GpuStats
    audit_and_scheduler_gap_ms = Statistics $AuditGaps 'audit_and_scheduler_gap_ms'
    notes = @(
        'CPU wall-clock fields overlap and must not be summed.',
        'native_acquire_ms may be nested inside present_wall_ms and mixed_raster_segment_ms.',
        'GPU timestamp rows are selected by their historical mixed-gpu frame id.',
        'audit_and_scheduler_gap_ms includes frame-audit logging plus unsampled scheduler/event-loop time.'
    )
}

$Json = $Result | ConvertTo-Json -Depth 10
if ($OutputJson) {
    $Parent = Split-Path -Parent ([IO.Path]::GetFullPath($OutputJson))
    if ($Parent -and -not (Test-Path -LiteralPath $Parent)) {
        New-Item -ItemType Directory -Path $Parent | Out-Null
    }
    $Json | Set-Content -Encoding UTF8 -LiteralPath $OutputJson
}
$Json
