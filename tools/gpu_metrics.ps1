[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $LogPath,
    [int] $WarmupFrames = 16,
    [int] $ExpectedFrames = 0,
    [string] $ExpectedPath = '',
    [switch] $RequireCampaignLoad,
    [switch] $RequireFixedTick,
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
        p99 = Percentile $Values 0.99
        maximum = ($Values | Measure-Object -Maximum).Maximum
    }
}

function Correlation([double[]] $X, [double[]] $Y) {
    if ($X.Count -ne $Y.Count -or $X.Count -lt 2) { return $null }
    $MeanX = ($X | Measure-Object -Average).Average
    $MeanY = ($Y | Measure-Object -Average).Average
    $Numerator = 0.0; $SquareX = 0.0; $SquareY = 0.0
    for ($Index = 0; $Index -lt $X.Count; $Index++) {
        $DeltaX = $X[$Index] - $MeanX; $DeltaY = $Y[$Index] - $MeanY
        $Numerator += $DeltaX * $DeltaY
        $SquareX += $DeltaX * $DeltaX; $SquareY += $DeltaY * $DeltaY
    }
    if ($SquareX -eq 0.0 -or $SquareY -eq 0.0) { return $null }
    return $Numerator / [Math]::Sqrt($SquareX * $SquareY)
}

function Sha256([string] $Text) {
    $Algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $Bytes = [Text.Encoding]::UTF8.GetBytes($Text)
        return ([BitConverter]::ToString($Algorithm.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant()
    } finally {
        $Algorithm.Dispose()
    }
}

foreach ($Line in $SelectedLines) {
    if ($Line -match '^FRAME-AUDIT frame=(\d+) path=([^ ]+) world=(\d+) ') {
        $Current = [pscustomobject][ordered]@{
            frame = [int]$Matches[1]
            path = $Matches[2]
            world = [int]$Matches[3]
            ticks = Number $Line 'ticks'
            accumulator_us = Number $Line 'accumulator_us'
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
            mixed_dynamic_release_ms = $null
            mixed_target_setup_ms = $null
            mixed_dynamic_pack_ms = $null
            mixed_dynamic_resource_ms = $null
            mixed_plan_build_ms = $null
            mixed_raster_preflight_ms = $null
            mixed_freeze_ms = $null
            mixed_cache_collect_ms = $null
            mixed_texture_measure_ms = $null
            mixed_pack_ms = $null
            mixed_draw_encode_ms = $null
            mixed_draw_batch_prepare_ms = $null
            mixed_graphics_draw_ms = $null
            mixed_raster_segment_ms = $null
            mixed_graphics_wait_ms = $null
            prepare_ms = $null
            cpu_render_ms = $null
            execute_ms = $null
            remainder_ms = $null
            phase_whole_ms = $null
            graphics_submit = @{}
            graphics_submits = $null
            stale_submit_frame = $false
            gpu_timing = $null
            wait_predecessor_frame = $null
            predecessor_gpu_timing = $null
            native_acquire_ms = $null
            native_present_ms = $null
            native_queue_idle_ms = $null
            raster_spans = $null
            draw_spans = $null
            bridge_transfers = $null
            bridge_bytes = $null
            world_commands = $null
            transparent_commands = $null
            effects_commands = $null
            viewmodel_commands = $null
            world_submission = $null
            layers = $null
            producers = @{}
            bridges = [Collections.Generic.List[object]]::new()
        }
        $Frames.Add($Current)
        continue
    }
    if ($Line -match '^FRAME-AUDIT mixed-gpu frame=[1-9]\d* supported=1 ') {
        if ($Line -notmatch 'valid=1 .*requested=(\d+) recorded=(\d+) dropped=0$' -or $Matches[1] -ne $Matches[2]) {
            throw 'GPU timing is incomplete or missing coverage metadata; do not use as a complete-frame baseline.'
        }
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
    if ($Line -match '^FRAME-AUDIT cpu-phases frame=(\d+) ') {
        if ([int]$Matches[1] -ne $Current.frame) { throw 'CPU phase frame mismatch.' }
        $Current.prepare_ms = (Number $Line 'prepare_us') / 1000.0
        $Current.cpu_render_ms = (Number $Line 'render_us') / 1000.0
        $Current.execute_ms = (Number $Line 'execute_us') / 1000.0
        $Current.remainder_ms = (Number $Line 'remainder_us') / 1000.0
        $Current.phase_whole_ms = (Number $Line 'whole_us') / 1000.0
        $PhaseSum = $Current.prepare_ms + $Current.cpu_render_ms + $Current.execute_ms + $Current.remainder_ms
        if ([Math]::Abs($PhaseSum - $Current.phase_whole_ms) -gt 0.005) { throw 'CPU phases do not partition whole-loop time.' }
    } elseif ($Line -match '^FRAME-AUDIT graphics-submit frame=(\d+) caller=([^ ]+) ') {
        $SubmitFrame = [int]$Matches[1]; $Caller = $Matches[2]
        if ($SubmitFrame -ne $Current.frame) {
            # Older logs printed the last submit watermark even on zero-submit
            # frames. Accept only past watermarks with zero deltas; validate the
            # complete caller group and aggregate below before using the frame.
            if ($SubmitFrame -gt $Current.frame -or
                (Number $Line 'submits') -ne 0 -or (Number $Line 'wait_ms') -ne 0) {
                throw 'Graphics submit frame mismatch.'
            }
            $Current.stale_submit_frame = $true
        }
        if ($Current.graphics_submit.ContainsKey($Caller)) { throw 'Duplicate graphics submit caller.' }
        $Current.graphics_submit[$Caller] = [pscustomobject][ordered]@{
            submits = Number $Line 'submits'
            wait_ms = Number $Line 'wait_ms'
        }
        $Current.wait_predecessor_frame = Number $Line 'predecessor_frame'
    } elseif ($Line -match '^FRAME-AUDIT scene ') {
        $Current.static_ms = Number $Line 'static_ms'
        $Current.scene_ms =
            (Number $Line 'sky_floor_ms') + (Number $Line 'map_ms') +
            (Number $Line 'static_ms') + (Number $Line 'gallery_ms') +
            (Number $Line 'character_private_ms') + (Number $Line 'projectiles_ms')
    } elseif ($Line -match '^FRAME-AUDIT world-submission ') {
        $Current.enemies_cmd = Number $Line 'enemies_cmd'
        $Current.enemies_ms = Number $Line 'enemies_ms'
        $Current.ai_teammates_ms = Number $Line 'ai_teammates_ms'
        $Current.world_submission = [pscustomobject][ordered]@{
            enemies_cmd = Number $Line 'enemies_cmd'
            ai_teammates_cmd = Number $Line 'ai_teammates_cmd'
            managed_player_cmd = Number $Line 'managed_player_cmd'
            network_teammates_cmd = Number $Line 'network_teammates_cmd'
            text_cmd = Number $Line 'text_cmd'
            interactables_cmd = Number $Line 'interactables_cmd'
        }
    } elseif ($Line -match '^FRAME-AUDIT mixed-cpu ') {
        $Current.mixed_freeze_ms = Number $Line 'freeze_ms'
        $Current.mixed_cache_collect_ms = Number $Line 'cache_collect_ms'
        $Current.mixed_preflight_ms = Number $Line 'preflight_ms'
        $Current.mixed_dynamic_release_ms = Number $Line 'dynamic_release_ms'
        $Current.mixed_target_setup_ms = Number $Line 'target_setup_ms'
        $Current.mixed_dynamic_pack_ms = Number $Line 'dynamic_pack_ms'
        $Current.mixed_dynamic_resource_ms = Number $Line 'dynamic_resource_ms'
        $Current.mixed_plan_build_ms = Number $Line 'plan_build_ms'
        $Current.mixed_raster_preflight_ms = Number $Line 'raster_preflight_ms'
        $Current.mixed_texture_measure_ms = Number $Line 'texture_measure_ms'
        $Current.mixed_pack_ms = Number $Line 'pack_ms'
        $Current.mixed_draw_encode_ms = Number $Line 'draw_encode_ms'
        $Current.mixed_draw_batch_prepare_ms = Number $Line 'draw_batch_prepare_ms'
        $Current.mixed_graphics_draw_ms = Number $Line 'graphics_draw_ms'
        $Current.mixed_raster_segment_ms = Number $Line 'raster_segment_ms'
    } elseif ($Line -match '^FRAME-AUDIT mixed ') {
        $Current.raster_spans = Number $Line 'raster_spans'
        $Current.draw_spans = Number $Line 'draw_spans'
        $Current.bridge_transfers = Number $Line 'bridge_transfers'
        $Current.bridge_bytes = Number $Line 'bridge_bytes'
        $Current.graphics_submits = Number $Line 'graphics_submits'
        $Current.mixed_graphics_wait_ms = Number $Line 'graphics_wait_ms'
    } elseif ($Line -match '^FRAME-AUDIT producer name=([^ ]+) ') {
        $Current.producers[$Matches[1]] = [pscustomobject][ordered]@{
            raster_cmd = Number $Line 'raster_cmd'
            spans = Number $Line 'spans'
            opaque_cmd = Number $Line 'opaque_cmd'
            transparent_cmd = Number $Line 'transparent_cmd'
            bridge_events = 0.0
            bridge_bytes = 0.0
        }
    } elseif ($Line -match '^FRAME-AUDIT bridge direction=([^ ]+) .* previous=([^ ]+) next=([^ ]+) ') {
        $Bridge = [pscustomobject][ordered]@{
            direction = $Matches[1]
            color_bytes = Number $Line 'color_bytes'
            depth_bytes = Number $Line 'depth_bytes'
            layer = Number $Line 'layer'
            previous = $Matches[2]
            next = $Matches[3]
            target_generation = Number $Line 'target_generation'
        }
        $Current.bridges.Add($Bridge)
        $Participants = @($Bridge.previous, $Bridge.next | Sort-Object -Unique)
        foreach ($Producer in $Participants) {
            if ($Current.producers.ContainsKey($Producer)) {
                $Current.producers[$Producer].bridge_events++
                $Current.producers[$Producer].bridge_bytes +=
                    $Bridge.color_bytes + $Bridge.depth_bytes
            }
        }
    } elseif ($Line -match '^FRAME-AUDIT layers ') {
        $Current.world_commands = Number $Line 'world'
        $Current.transparent_commands = Number $Line 'transparent'
        $Current.effects_commands = Number $Line 'effects'
        $Current.viewmodel_commands = Number $Line 'viewmodel'
        $Current.layers = [pscustomobject][ordered]@{
            sky = Number $Line 'sky'
            world = Number $Line 'world'
            transparent = Number $Line 'transparent'
            effects = Number $Line 'effects'
            viewmodel = Number $Line 'viewmodel'
            overlay_pixels = Number $Line 'overlay_pixels'
        }
    } elseif ($Line -match '^FRAME-AUDIT gpu ') {
        $Current.native_acquire_ms = Number $Line 'native_acquire_ms'
        $Current.native_present_ms = Number $Line 'native_present_ms'
        $Current.native_queue_idle_ms = Number $Line 'native_present_queue_idle_ms'
    }
}

foreach ($Frame in $Frames) {
    if ($Frame.stale_submit_frame) {
        $Callers = @('upload','vertex-diff','skin-input','skinning','bridge','draw','readback')
        if ($Frame.graphics_submits -ne 0 -or $Frame.graphics_submit.Count -ne $Callers.Count) {
            throw 'Graphics submit frame mismatch.'
        }
        foreach ($Caller in $Callers) {
            $Row = $Frame.graphics_submit[$Caller]
            if ($null -eq $Row -or $Row.submits -ne 0 -or $Row.wait_ms -ne 0) {
                throw 'Graphics submit frame mismatch.'
            }
        }
        $Frame.wait_predecessor_frame = 0
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

$TickValues = @($Measured | ForEach-Object { $_.ticks } | Where-Object { $null -ne $_ } | Sort-Object -Unique)
$AccumulatorValues = @($Measured | ForEach-Object { $_.accumulator_us } | Where-Object { $null -ne $_ } | Sort-Object -Unique)
if ($RequireFixedTick) {
    if ($TickValues.Count -ne 1 -or $TickValues[0] -ne 1) {
        throw "Fixed tick gate failed: measured tick values are $($TickValues -join ',')."
    }
    if ($AccumulatorValues.Count -ne 1 -or $AccumulatorValues[0] -ne 0) {
        throw "Fixed tick gate failed: measured accumulator values are $($AccumulatorValues -join ',')."
    }
}
$CountFields = @('raster_spans','draw_spans','bridge_transfers','bridge_bytes','world_commands','transparent_commands','effects_commands','viewmodel_commands')
$CountRanges = [ordered]@{}
foreach ($Field in $CountFields) {
    $Values = @($Measured | ForEach-Object { $_.$Field } | Where-Object { $null -ne $_ })
    if ($Values.Count) {
        $CountRanges[$Field] = [ordered]@{
            minimum = ($Values | Measure-Object -Minimum).Minimum
            maximum = ($Values | Measure-Object -Maximum).Maximum
            distinct = @($Values | Sort-Object -Unique).Count
        }
    }
}

$ProducerNames = @($Measured | ForEach-Object { $_.producers.Keys } |
    Sort-Object -Unique)
$ProducerSummary = [ordered]@{}
foreach ($ProducerName in $ProducerNames) {
    $ProducerRows = @($Measured | ForEach-Object { $_.producers[$ProducerName] } |
        Where-Object { $null -ne $_ })
    $Summary = [ordered]@{}
    foreach ($Field in @('raster_cmd','spans','bridge_events','bridge_bytes',
                          'opaque_cmd','transparent_cmd')) {
        $Values = @($ProducerRows | ForEach-Object { $_.$Field })
        $Summary[$Field] = [ordered]@{
            minimum = ($Values | Measure-Object -Minimum).Minimum
            maximum = ($Values | Measure-Object -Maximum).Maximum
            distinct = @($Values | Sort-Object -Unique).Count
        }
        $Pairs = @($Measured | Where-Object {
            $null -ne $_.producers[$ProducerName] -and $null -ne $_.whole_loop_ms
        })
        $Summary[$Field].whole_loop_correlation = Correlation `
            @($Pairs | ForEach-Object { [double]$_.producers[$ProducerName].$Field }) `
            @($Pairs | ForEach-Object { [double]$_.whole_loop_ms })
    }
    $ProducerSummary[$ProducerName] = $Summary
}

$WorkloadFrames = @($Frames | ForEach-Object {
    $ProducerRows = [ordered]@{}
    foreach ($ProducerName in @($_.producers.Keys | Sort-Object)) {
        $Producer = $_.producers[$ProducerName]
        $ProducerRows[$ProducerName] = [ordered]@{
            raster_cmd = $Producer.raster_cmd
            spans = $Producer.spans
            opaque_cmd = $Producer.opaque_cmd
            transparent_cmd = $Producer.transparent_cmd
        }
    }
    $BridgeRows = @($_.bridges | ForEach-Object {
        [ordered]@{
            direction = $_.direction
            color_bytes = $_.color_bytes
            depth_bytes = $_.depth_bytes
            layer = $_.layer
            previous = $_.previous
            next = $_.next
        }
    })
    [ordered]@{
        frame = $_.frame
        raster_spans = $_.raster_spans
        draw_spans = $_.draw_spans
        producers = $ProducerRows
        layers = $_.layers
        world_submission = $_.world_submission
        bridges = $BridgeRows
    }
})
$WorkloadFrameHashes = @($WorkloadFrames | ForEach-Object {
    Sha256 ($_ | ConvertTo-Json -Depth 8 -Compress)
})
$WorkloadSequenceText = $WorkloadFrames | ConvertTo-Json -Depth 8 -Compress

function SlowFrameReason([object] $Frame) {
    $Candidates = [ordered]@{
        cpu_raster_recording = $Frame.mixed_raster_segment_ms
        gpu_raster_workload = $Frame.gpu_timing.raster_ms
        cpu_render = $Frame.cpu_render_ms
        cpu_prepare = $Frame.prepare_ms
        present_or_acquire = $Frame.native_acquire_ms + $Frame.native_present_ms
    }
    foreach ($Caller in $Frame.graphics_submit.Keys) { $Candidates["graphics_fence_$Caller"] = $Frame.graphics_submit[$Caller].wait_ms }
    $BestName = 'unclassified'
    $BestValue = 0.0
    foreach ($Entry in $Candidates.GetEnumerator()) {
        if ($null -ne $Entry.Value -and [double]$Entry.Value -gt $BestValue) {
            $BestName = $Entry.Name
            $BestValue = [double]$Entry.Value
        }
    }
    if ($BestValue * 2 -lt $Frame.whole_loop_ms) { $BestName = 'unclassified' }
    return [ordered]@{ reason = $BestName; attributed_ms = $BestValue }
}

# frame_interval[N] spans begin(N-1) -> begin(N).  Subtract the previous
# whole-loop sample, not the current one, to estimate audit/log/scheduler gap.
$ByFrame = @{}
foreach ($Frame in $Frames) { $ByFrame[$Frame.frame] = $Frame }
$GpuByFrame = @{}
foreach ($GpuFrame in $GpuFrames) {
    if ($GpuByFrame.ContainsKey($GpuFrame.frame)) { throw 'Duplicate GPU timing frame.' }
    $GpuByFrame[$GpuFrame.frame] = $GpuFrame
}
foreach ($Frame in $Frames) {
    $Frame.gpu_timing = $GpuByFrame[$Frame.frame]
    if ($Frame.wait_predecessor_frame -gt 0) {
        if ($Frame.wait_predecessor_frame -ge $Frame.frame) { throw 'Graphics wait predecessor is not a prior frame.' }
        $Frame.predecessor_gpu_timing = $GpuByFrame[[int]$Frame.wait_predecessor_frame]
    }
}
$AuditGaps = [Collections.Generic.List[object]]::new()
foreach ($Frame in $Measured) {
    $Previous = $ByFrame[$Frame.frame - 1]
    if ($null -ne $Previous -and $Frame.frame_interval_ms -gt 0) {
        $Gap = $Frame.frame_interval_ms - $Previous.whole_loop_ms
        $AuditGaps.Add([pscustomobject]@{
            frame = $Previous.frame
            audit_and_scheduler_gap_ms = $Gap
        })
        $Previous | Add-Member -NotePropertyName audit_and_scheduler_gap_ms -NotePropertyValue $Gap
    }
}

$SlowCount = [Math]::Max(1, [Math]::Ceiling($Measured.Count * 0.05))
$SlowFrames = @($Measured | Sort-Object whole_loop_ms -Descending | Select-Object -First $SlowCount | ForEach-Object {
    $Classification = SlowFrameReason $_
    [ordered]@{
        frame = $_.frame
        whole_loop_ms = $_.whole_loop_ms
        reason = $Classification.reason
        attributed_ms = $Classification.attributed_ms
        cpu_phases_ms = [ordered]@{ prepare=$_.prepare_ms; render=$_.cpu_render_ms; execute=$_.execute_ms; remainder=$_.remainder_ms }
        graphics_submit = $_.graphics_submit
        gpu_timing = $_.gpu_timing
        wait_predecessor_frame = $_.wait_predecessor_frame
        predecessor_gpu_timing = $_.predecessor_gpu_timing
        producers = $_.producers
        bridges = $_.bridges
    }
})

$CpuFields = @(
    'render_ms','present_wall_ms','whole_loop_ms','frame_interval_ms','scene_ms','static_ms',
    'enemies_ms','ai_teammates_ms','mixed_freeze_ms','mixed_cache_collect_ms',
    'mixed_preflight_ms','mixed_dynamic_release_ms','mixed_target_setup_ms',
    'mixed_dynamic_pack_ms','mixed_dynamic_resource_ms','mixed_plan_build_ms',
    'mixed_raster_preflight_ms','mixed_texture_measure_ms','mixed_pack_ms',
    'mixed_draw_encode_ms','mixed_draw_batch_prepare_ms','mixed_graphics_draw_ms',
    'mixed_raster_segment_ms','mixed_graphics_wait_ms',
    'prepare_ms','cpu_render_ms','execute_ms','remainder_ms',
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
$SubmitStats = [ordered]@{}
foreach ($Caller in @('upload','vertex-diff','skin-input','skinning','bridge','draw','readback')) {
    $Rows = @($Measured | ForEach-Object { if ($_.graphics_submit.ContainsKey($Caller)) { $_.graphics_submit[$Caller] } })
    $SubmitStats[$Caller] = [ordered]@{ submits = Statistics $Rows 'submits'; wait_ms = Statistics $Rows 'wait_ms' }
}
$WaitPairs = @($Measured | Where-Object { $null -ne $_.predecessor_gpu_timing })

$Result = [ordered]@{
    schema = 6
    log = $ResolvedLog
    selected_run_from_end = $RunFromEnd
    warmup_frames = $WarmupFrames
    total_frames = $Frames.Count
    measured_frame_range = "$($WarmupFrames + 1)-$(($Frames | Select-Object -Last 1).frame)"
    measured_frames = $Measured.Count
    gpu_timestamp_frames = $MeasuredGpu.Count
    fixed_workload = [ordered]@{
        tick_values = $TickValues
        accumulator_us_values = $AccumulatorValues
        count_ranges = $CountRanges
        normalized_sequence_sha256 = Sha256 $WorkloadSequenceText
        normalized_frame_sha256 = $WorkloadFrameHashes
        normalized_frames = $WorkloadFrames
    }
    paths = @($Frames | Group-Object path | ForEach-Object { [ordered]@{ path = $_.Name; frames = $_.Count } })
    worlds = @($Frames | Group-Object world | ForEach-Object { [ordered]@{ world = [int]$_.Name; frames = $_.Count } })
    enemy_command_frames = @($Frames | Where-Object { $_.enemies_cmd -gt 0 }).Count
    cpu_wall = $CpuStats
    gpu_timestamp = $GpuStats
    graphics_submit = $SubmitStats
    wait_predecessor = [ordered]@{
        paired_frames = $WaitPairs.Count
        graphics_wait_vs_predecessor_raster_correlation = Correlation @($WaitPairs | ForEach-Object { $_.mixed_graphics_wait_ms }) @($WaitPairs | ForEach-Object { $_.predecessor_gpu_timing.raster_ms })
        frames = @($Measured | ForEach-Object { [ordered]@{ frame=$_.frame; graphics_wait_ms=$_.mixed_graphics_wait_ms; predecessor_frame=$_.wait_predecessor_frame; predecessor_gpu_timing=$_.predecessor_gpu_timing } })
    }
    producer_attribution = $ProducerSummary
    audit_and_scheduler_gap_ms = Statistics $AuditGaps 'audit_and_scheduler_gap_ms'
    slowest_five_percent = $SlowFrames
    notes = @(
        'CPU wall-clock fields overlap and must not be summed.',
        'native_acquire_ms may be nested inside present_wall_ms and mixed_raster_segment_ms.',
        'GPU timestamp rows are selected by their historical mixed-gpu frame id.',
        'Only prepare/cpu_render/execute/remainder are mutually exclusive CPU phases; detail timers overlap.',
        'A graphics wait predecessor is queue ordering, not a GPU time apportionment or proof of causality.',
        'Per-caller rows share the frame-aggregated latest predecessor, not a per-submit trace.',
        'Frame-slot wait is absent from audit logs; use the separate low-overhead RB0 stats run.',
        'audit_and_scheduler_gap_ms is outside whole-loop and is not used to classify whole-loop slow frames.'
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
