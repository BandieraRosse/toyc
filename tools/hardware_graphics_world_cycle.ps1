[CmdletBinding()]
param([string] $OutputDirectory = '', [switch] $NoRedirect)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package = Join-Path $Root 'build-windows/rasterfall-windows'
$Exe = Join-Path $Package 'rasterfall.exe'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $Root ('tmp/hg-world-cycle-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new evidence directory.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$ToolPath = $env:Path
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $ToolPath, 'Process')
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class HgWorldCycleProcess {
    [DllImport("kernel32.dll")] public static extern bool GetExitCodeProcess(IntPtr process, out uint code);
}
'@
$RuntimeLog = Join-Path $Package 'rasterfall.log'
$PreviousLines = if (Test-Path -LiteralPath $RuntimeLog) { @(Get-Content -LiteralPath $RuntimeLog).Count } else { 0 }
$ProgramArgs = @('--renderer','gpu-compute','--gpu-required','--gpu-native-present','--world-cycle-gate','--frame-audit','--frames','120')
$Record = [ordered]@{ executable = (Get-FileHash -LiteralPath $Exe).Hash; argv = $ProgramArgs; result = 'FAIL' }
$Process = $null
try {
    if ($NoRedirect) {
        $Process = Start-Process -FilePath $Exe -ArgumentList $ProgramArgs -WorkingDirectory $Package -WindowStyle Hidden -PassThru
    } else {
        $Process = Start-Process -FilePath $Exe -ArgumentList $ProgramArgs -WorkingDirectory $Package -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $OutputDirectory 'stdout.txt') -RedirectStandardError (Join-Path $OutputDirectory 'stderr.txt')
    }
    $ProcessHandle = $Process.Handle
    if (-not $Process.WaitForExit(300000)) { throw 'World cycle gate timed out.' }
    $Process.WaitForExit()
    [uint32] $ExitCode = 0
    if (-not [HgWorldCycleProcess]::GetExitCodeProcess($ProcessHandle, [ref] $ExitCode)) { throw 'Cannot read process exit code.' }
    $Record.exit_code = $ExitCode
    if ($ExitCode -ne 0) { throw "World cycle gate exited $ExitCode." }
    $Log = @(Get-Content -LiteralPath $RuntimeLog | Select-Object -Skip $PreviousLines)
    $Log | Set-Content -Encoding UTF8 (Join-Path $OutputDirectory 'runtime.log')
    $Frames = @($Log | Select-String 'FRAME-AUDIT frame=')
    $Gpu = @($Log | Select-String 'FRAME-AUDIT gpu ')
    $Mixed = @($Log | Select-String 'FRAME-AUDIT mixed ')
    $Ground = @($Log | Select-String 'FRAME-AUDIT ground-draw ')
    $Layers = @($Log | Select-String 'FRAME-AUDIT layers ')
    $Resources = @($Log | Select-String 'FRAME-AUDIT draw-resources ')
    $Cycles = @($Log | Select-String 'WORLD-CYCLE-RESOURCES ')
    $Present = @($Log | Select-String 'PRESENT-AUDIT frame=')
    if ($Frames.Count -ne 120 -or $Gpu.Count -ne 120 -or $Mixed.Count -ne 120 -or
        $Ground.Count -ne 120 -or $Layers.Count -ne 120 -or $Resources.Count -ne 120 -or
        $Cycles.Count -ne 3 -or $Present.Count -ne 120) {
        throw 'Incomplete frame audit.'
    }
    $ExpectedWorlds = @(0, 1, 2, 1)
    $GroundBuilds = 0
    $SawRetired = $false
    $LastLoads = -1
    $LastReleases = -1
    for ($Index = 0; $Index -lt 120; ++$Index) {
        $Phase = [Math]::Min([int][Math]::Floor($Index / 30.0), 3)
        if ($Frames[$Index].Line -notmatch "path=gpu-native world=$($ExpectedWorlds[$Phase]) ") { throw "Unexpected path/world at frame $($Index + 1)." }
        if ($Gpu[$Index].Line -notmatch 'readback_bytes=0 cpu_framebuffer_copy_bytes=0') { throw 'Readback/copy detected.' }
        if ($Layers[$Index].Line -notmatch 'invalid_transitions=0 .*pre_post_cpu_fallback=0 fallback_reason=0x0 ') { throw 'Fallback/order failure.' }
        if ($Present[$Index].Line -notmatch 'hot_queue_idle_count=0 .*presenter_poisoned=0 ') { throw 'Hot queue-idle or poisoned presenter detected.' }
        if ($Ground[$Index].Line -notmatch 'legacy_commands=0 mesh_builds=(\d+)') { throw 'Ground Draw audit missing.' }
        $Builds = [int]$Matches[1]
        $GroundBuilds += $Builds
        if (($Index % 30) -ge 2 -and $Mixed[$Index].Line -notmatch 'gpu_upload_bytes=0') { throw 'Stable world re-uploaded GPU resources.' }
        if ($Resources[$Index].Line -notmatch 'live=(\d+) retired=(\d+) pinned=(\d+) failed=0 loads=(\d+) releases=(\d+)') { throw 'Resource lifetime audit changed.' }
        $Live = [int]$Matches[1]; $Retired = [int]$Matches[2]; $Pinned = [int]$Matches[3]
        $Loads = [int]$Matches[4]; $Releases = [int]$Matches[5]
        if ($Pinned -gt ($Live + $Retired)) { throw 'Pinned resources exceed owned resources.' }
        if ($Retired -gt 0) { $SawRetired = $true }
        if ($Loads -lt $LastLoads -or $Releases -lt $LastReleases) { throw 'Resource counters regressed.' }
        $LastLoads = $Loads; $LastReleases = $Releases
    }
    if ($GroundBuilds -ne 4) { throw "Expected four ground mesh generations, saw $GroundBuilds." }
    foreach ($Cycle in $Cycles) {
        if ($Cycle.Line -notmatch 'retired=([1-9]\d*) pinned=([1-9]\d*)') {
            throw 'World switch did not expose an in-flight retired generation.'
        }
    }
    $FinalResource = $Resources[-1].Line
    if ($FinalResource -notmatch 'live=(\d+) retired=0 pinned=(\d+) failed=0') { throw 'Retired generation did not drain.' }
    if ([int]$Matches[2] -gt [int]$Matches[1]) { throw 'Final pinned resources exceed live resources.' }
    if ($LastReleases -le 0) { throw 'No old world resources were released.' }
    $Record.frames = 120
    $Record.worlds = $ExpectedWorlds
    $Record.ground_mesh_builds = $GroundBuilds
    $Record.saw_retired_in_flight = $true
    $Record.final_loads = $LastLoads
    $Record.final_releases = $LastReleases
    $Record.result = 'PASS'
} catch {
    $Record.error = $_.Exception.Message
    throw
} finally {
    if ($Process -and -not $Process.HasExited) { $Process.Kill(); $Process.WaitForExit() }
    if (Test-Path -LiteralPath $RuntimeLog) {
        Get-Content -LiteralPath $RuntimeLog | Select-Object -Skip $PreviousLines | Set-Content -Encoding UTF8 (Join-Path $OutputDirectory 'runtime.log')
    }
    $Record | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (Join-Path $OutputDirectory 'manifest.json')
}
Write-Host "HG world cycle PASS: $OutputDirectory"
