[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $BaselineExe,
    [Parameter(Mandatory)][string] $CandidateExe,
    [Parameter(Mandatory)][string] $OutputDirectory,
    [ValidateRange(1,20)][int] $Rounds = 5,
    [ValidateRange(30,10000)][int] $NearFrames = 120,
    [ValidateSet('all','near-0','near-30','near-60','campaign-320')][string] $Scene = 'all'
)
$ErrorActionPreference = 'Stop'
$BaselineExe = (Resolve-Path -LiteralPath $BaselineExe).Path
$CandidateExe = (Resolve-Path -LiteralPath $CandidateExe).Path
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new output directory.' }
Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; public class RfMixedPower { [StructLayout(LayoutKind.Sequential)] public struct Status { public byte ACLineStatus, BatteryFlag, BatteryLifePercent, SystemStatusFlag; public uint BatteryLifeTime, BatteryFullLifeTime; } [DllImport("kernel32.dll")] public static extern bool GetSystemPowerStatus(out Status status); }'
$PowerStatus = New-Object RfMixedPower+Status
if (-not [RfMixedPower]::GetSystemPowerStatus([ref]$PowerStatus) -or $PowerStatus.ACLineStatus -ne 1) {
    throw 'Mixed A/B performance sampling requires AC power.'
}
if (Get-Process -Name @('rasterfall', [IO.Path]::GetFileNameWithoutExtension($BaselineExe),
        [IO.Path]::GetFileNameWithoutExtension($CandidateExe)) -ErrorAction SilentlyContinue) {
    throw 'A render test process is already running.'
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$Variants = @{ baseline=$BaselineExe; candidate=$CandidateExe }
$ExpectedHashes = @{}
foreach ($Variant in @('baseline','candidate')) {
    $ExpectedHashes[$Variant] = (Get-FileHash -LiteralPath $Variants[$Variant]).Hash
}
$PowerSource = $null
$Records = [Collections.Generic.List[object]]::new()
foreach ($Round in 1..$Rounds) {
    # Alternate AB/BA to reduce systematic order and temperature bias.
    $Order = if ($Round % 2) { @('baseline','candidate') } else { @('candidate','baseline') }
    foreach ($Variant in $Order) {
        $Directory = Join-Path $OutputDirectory ('{0:D2}-{1}' -f $Round,$Variant)
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'gpu_rb0_sampling.ps1') `
            -Rounds 1 -NoAudit -ExecutablePath $Variants[$Variant] -OutputDirectory $Directory `
            -NearFrames $NearFrames -Scene $Scene
        if ($LASTEXITCODE -ne 0) { throw "Sampling failed: $Directory" }
        $Manifest = Get-Content -Raw -Encoding UTF8 (Join-Path $Directory 'manifest.json') | ConvertFrom-Json
        $ExpectedRuns = if ($Scene -eq 'all') { 4 } else { 1 }
        if ($Manifest.result -ne 'PASS' -or $Manifest.runs.Count -ne $ExpectedRuns -or
            $Manifest.package_exe.sha256 -ne $ExpectedHashes[$Variant]) {
            throw "Incomplete sample or executable changed between rounds: $Directory"
        }
        if ($Manifest.ac_line_status -ne 1) { throw 'Mixed A/B performance sampling requires AC power.' }
        if ($null -eq $PowerSource) { $PowerSource = $Manifest.ac_line_status }
        if ($Manifest.ac_line_status -ne $PowerSource) { throw 'Power source changed between A/B samples.' }
        foreach ($Run in $Manifest.runs) {
            $Records.Add([pscustomobject]@{ round=$Round; variant=$Variant; scene=$Run.scene
                executable_sha256=$Manifest.package_exe.sha256
                whole_median_ms=$Run.rb0_whole_median_ms; whole_p95_ms=$Run.rb0_whole_p95_ms
                whole_p99_ms=$Run.rb0_whole_p99_ms; gpu_raster_median_ms=$Run.rb0_gpu_raster_median_ms
                gpu_draw_median_ms=$Run.rb0_gpu_draw_median_ms; gpu_bridge_median_ms=$Run.rb0_gpu_bridge_median_ms })
        }
        $Records | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 (Join-Path $OutputDirectory 'rounds.json')
    }
}
function Median($Values) {
    $Sorted = @($Values | Sort-Object)
    $Middle = [int][Math]::Floor($Sorted.Count/2)
    if ($Sorted.Count % 2) { return $Sorted[$Middle] }
    return ($Sorted[$Middle-1]+$Sorted[$Middle])/2
}
$SelectedScenes = if ($Scene -eq 'all') { @('near-0','near-30','near-60','campaign-320') } else { @($Scene) }
$Summary = foreach ($Scene in $SelectedScenes) {
    foreach ($Variant in @('baseline','candidate')) {
        $Rows = @($Records | Where-Object { $_.scene -eq $Scene -and $_.variant -eq $Variant })
        [pscustomobject]@{ scene=$Scene; variant=$Variant; rounds=$Rows.Count
            whole_median_ms=Median $Rows.whole_median_ms; whole_p95_ms=Median $Rows.whole_p95_ms
            whole_p99_ms=Median $Rows.whole_p99_ms; gpu_raster_median_ms=Median $Rows.gpu_raster_median_ms
            gpu_draw_median_ms=Median $Rows.gpu_draw_median_ms; gpu_bridge_median_ms=Median $Rows.gpu_bridge_median_ms }
    }
}
$Summary | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 (Join-Path $OutputDirectory 'summary.json')
$Summary | Format-Table -AutoSize
