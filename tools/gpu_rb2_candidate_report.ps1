[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string] $AuditDirectory,
    [Parameter(Mandatory=$true)][string] $StatsDirectory,
    [Parameter(Mandatory=$true)][string] $OutputJson
)
$ErrorActionPreference = 'Stop'
$Audit = Get-Content -Raw -Encoding UTF8 (Join-Path $AuditDirectory 'manifest.json') | ConvertFrom-Json
$Stats = Get-Content -Raw -Encoding UTF8 (Join-Path $StatsDirectory 'manifest.json') | ConvertFrom-Json
if ($Audit.result -ne 'PASS' -or $Stats.result -ne 'PASS' -or
    $Audit.mode -ne 'audit' -or $Stats.mode -ne 'no-audit' -or
    $Audit.package_exe.sha256 -ne $Stats.package_exe.sha256) {
    throw 'Require successful audit/no-audit runs of the same package.'
}
function Range($Values) {
    $Values = @($Values)
    if (-not $Values.Count) { throw 'Missing measurement.' }
    return [ordered]@{ minimum=($Values | Measure-Object -Minimum).Minimum; maximum=($Values | Measure-Object -Maximum).Maximum }
}
$Scenes = foreach ($Scene in @('near-0','near-30','near-60','campaign-320')) {
    $AuditRuns = @($Audit.runs | Where-Object scene -eq $Scene)
    $StatsRuns = @($Stats.runs | Where-Object scene -eq $Scene)
    if (-not $AuditRuns.Count -or -not $StatsRuns.Count) { throw "Missing scene $Scene" }
    $Metrics = @($AuditRuns | ForEach-Object {
        Get-Content -Raw -Encoding UTF8 (Join-Path $AuditDirectory $_.metrics) | ConvertFrom-Json
    })
    $Hashes = @($Metrics | ForEach-Object { $_.fixed_workload.normalized_sequence_sha256 } | Sort-Object -Unique)
    if ($Hashes.Count -ne 1) { throw "Workload drift: $Scene" }
    $Producers = [ordered]@{}
    foreach ($Name in @($Metrics[0].producer_attribution.PSObject.Properties.Name)) {
        $Rows = @($Metrics | ForEach-Object { $_.producer_attribution.$Name })
        $Producers[$Name] = [ordered]@{
            raster_commands = Range @($Rows | ForEach-Object { $_.raster_cmd.minimum; $_.raster_cmd.maximum })
            opaque_commands = Range @($Rows | ForEach-Object { $_.opaque_cmd.minimum; $_.opaque_cmd.maximum })
            transparent_commands = Range @($Rows | ForEach-Object { $_.transparent_cmd.minimum; $_.transparent_cmd.maximum })
            gpu_raster_ms = $null
        }
    }
    # Preserve ordered boundaries, not just a count or the two endpoint labels.
    # These are bridge signatures; they do not enumerate intervening Raster spans.
    $Sequences = @($Metrics | ForEach-Object {
        $Warmup = $_.warmup_frames
        $_.fixed_workload.normalized_frames | Where-Object frame -gt $Warmup | ForEach-Object {
            ConvertTo-Json -InputObject @($_.bridges) -Depth 5 -Compress
        }
    } | Group-Object | ForEach-Object {
        [ordered]@{ observed_frames=$_.Count; bridges=@($_.Name | ConvertFrom-Json) }
    })
    [ordered]@{
        scene=$Scene
        audit_rounds=$AuditRuns.Count
        stats_rounds=$StatsRuns.Count
        workload_sha256=$Hashes[0]
        whole_median_ms=Range @($StatsRuns.rb0_whole_median_ms)
        whole_p95_ms=Range @($StatsRuns.rb0_whole_p95_ms)
        whole_p99_ms=Range @($StatsRuns.rb0_whole_p99_ms)
        gpu_raster_median_ms=Range @($StatsRuns.rb0_gpu_raster_median_ms)
        gpu_draw_median_ms=Range @($StatsRuns.rb0_gpu_draw_median_ms)
        gpu_bridge_median_ms=Range @($StatsRuns.rb0_gpu_bridge_median_ms)
        bridge_transfers=Range @($Metrics | ForEach-Object { $_.fixed_workload.count_ranges.bridge_transfers.minimum; $_.fixed_workload.count_ranges.bridge_transfers.maximum })
        bridge_bytes=Range @($Metrics | ForEach-Object { $_.fixed_workload.count_ranges.bridge_bytes.minimum; $_.fixed_workload.count_ranges.bridge_bytes.maximum })
        producers=$Producers
        bridge_sequences=$Sequences
    }
}
[ordered]@{
    schema=1
    package_sha256=$Audit.package_exe.sha256
    audit_directory=(Resolve-Path $AuditDirectory).Path
    stats_directory=(Resolve-Path $StatsDirectory).Path
    scenes=@($Scenes)
    limitations=@(
        'Producer GPU cost is not measured; command counts are not GPU time.',
        'A bridge endpoint label does not identify all intervening Raster content.',
        'Producer bridge attribution counts both endpoints; do not sum it as physical traffic.',
        'Performance uses only no-audit runs; audit timings are not combined with it.',
        'A baseline inventory does not prove migration benefit.'
    )
} | ConvertTo-Json -Depth 12 | Set-Content -Encoding UTF8 -LiteralPath $OutputJson
Write-Host "Candidate report: $OutputJson"
