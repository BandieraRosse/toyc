[CmdletBinding(DefaultParameterSetName = 'Quick')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Quick')][switch] $Quick,
    [Parameter(Mandatory, ParameterSetName = 'Full')][switch] $Full,
    [string] $OutputDirectory = '',
    [switch] $NoRedirect
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package = Join-Path $Root 'build-windows/rasterfall-windows'
$Exe = Join-Path $Package 'rasterfall.exe'
$RuntimeLog = Join-Path $Package 'rasterfall.log'
$Diff = Join-Path $Root 'build-windows/rf-gpu-raster-diff-test.exe'
$Graphics = Join-Path $Root 'build-windows/rf-gpu-graphics-test.exe'
$Raster = Join-Path $Root 'build-windows/rf-gpu-raster-test.exe'
$Cache = Join-Path $Root 'build-windows/rasterfall-gpu-cache-test.exe'
$Mixed = Join-Path $Root 'build-windows/rasterfall-gpu-mixed-test.exe'
$Mode = if ($Full) { 'full' } else { 'quick' }
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $Root ('tmp/gpu-acceptance-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new output directory.' }
foreach ($Required in @($Exe, $Diff, $Graphics, $Raster, $Cache, $Mixed)) {
    if (-not (Test-Path -LiteralPath $Required)) { throw "Missing required executable: $Required. Run windows/NativeCodex.ps1 package and the GPU test targets first." }
}
if (Get-Process -Name rasterfall -ErrorAction SilentlyContinue) {
    throw 'A rasterfall process is already running; stop it before starting serial GPU acceptance.'
}
$ToolPath = $env:Path
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $ToolPath, 'Process')
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$Runs = [Collections.Generic.List[object]]::new()
$Manifest = [ordered]@{
    schema = 1; suite = 'gpu-acceptance'; mode = $Mode
    started_at = (Get-Date).ToString('o'); commit = (& git -C $Root rev-parse HEAD)
    worktree = @(& git -C $Root status --short); runs = $Runs; result = 'FAIL'
}
$Summary = [ordered]@{ schema = 1; suite = 'gpu-acceptance'; mode = $Mode; result = 'FAIL'; passed = 0; failed = 0 }
try { $Manifest.adapters = @(Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion, PNPDeviceID) }
catch { $Manifest.adapter_query_error = $_.Exception.Message }
function Save-Results {
    $Manifest | ConvertTo-Json -Depth 12 | Set-Content -Encoding UTF8 (Join-Path $OutputDirectory 'manifest.json')
    $Summary | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $OutputDirectory 'summary.json')
}
function Run-Process([string] $Name, [string] $Program, [string[]] $Arguments, [string] $WorkingDirectory = $Root) {
    Write-Host "[GPU-$($Mode.ToUpperInvariant())] $Name"
    $Stdout = Join-Path $OutputDirectory "$Name.stdout.txt"
    $Stderr = Join-Path $OutputDirectory "$Name.stderr.txt"
    foreach ($Argument in $Arguments) { if ($Argument.Contains('"')) { throw 'Embedded quotes are not supported.' } }
    $Quoted = ($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $Start = @{ FilePath = $Program; WorkingDirectory = $WorkingDirectory; WindowStyle = 'Hidden'; Wait = $true
        PassThru = $true; RedirectStandardOutput = $Stdout; RedirectStandardError = $Stderr }
    if ($Quoted) { $Start.ArgumentList = $Quoted }
    $Process = Start-Process @Start
    $Record = [ordered]@{ name = $Name; argv = @($Program) + $Arguments; exit_code = $Process.ExitCode }
    $Runs.Add($Record); Save-Results
    if ($Process.ExitCode -ne 0) { throw "$Name exited $($Process.ExitCode)." }
    return $Record
}
function Run-Game([string] $Name, [string[]] $Arguments, [int] $ExpectedFrames = 0, [string] $ExpectedPath = '') {
    $Before = if (Test-Path -LiteralPath $RuntimeLog) { @(Get-Content -Encoding UTF8 -LiteralPath $RuntimeLog).Count } else { 0 }
    $Record = Run-Process $Name $Exe $Arguments $Package
    $Log = if (Test-Path -LiteralPath $RuntimeLog) { @(Get-Content -Encoding UTF8 -LiteralPath $RuntimeLog | Select-Object -Skip $Before) } else { @() }
    $LogPath = Join-Path $OutputDirectory "$Name.runtime.log"
    $Log | Set-Content -Encoding UTF8 -LiteralPath $LogPath
    if ($ExpectedFrames) {
        $Frames = @($Log | Select-String 'FRAME-AUDIT frame=')
        $Gpu = @($Log | Select-String 'FRAME-AUDIT gpu ')
        $Layers = @($Log | Select-String 'FRAME-AUDIT layers ')
        $Present = @($Log | Select-String 'PRESENT-AUDIT frame=')
        if ($Frames.Count -ne $ExpectedFrames -or $Gpu.Count -ne $ExpectedFrames -or $Layers.Count -ne $ExpectedFrames) { throw "$Name has an incomplete frame audit." }
        if ($ExpectedPath -eq 'gpu-native' -and $Present.Count -ne $ExpectedFrames) { throw "$Name has an incomplete presenter audit." }
        foreach ($Line in $Frames) { if ($ExpectedPath -and $Line.Line -notmatch "path=$ExpectedPath ") { throw "$Name used an unexpected render path." } }
        foreach ($Line in $Gpu) { if ($Line.Line -notmatch 'readback_bytes=0 cpu_framebuffer_copy_bytes=0') { throw "$Name performed a readback or CPU framebuffer copy." } }
        foreach ($Line in $Layers) { if ($Line.Line -notmatch 'invalid_transitions=0 .*pre_post_cpu_fallback=0 fallback_reason=0x0 ') { throw "$Name used fallback or invalid layer ordering." } }
        foreach ($Line in $Present) { if ($Line.Line -notmatch 'hot_queue_idle_count=0 .*presenter_poisoned=0 ') { throw "$Name used hot queue-idle or poisoned the presenter." } }
        $Record.frames = $ExpectedFrames
        $Record.path = $ExpectedPath
    }
    return @{ record = $Record; log = $Log; log_path = $LogPath }
}
function Assert-Character([object[]] $Log, [switch] $RequireReference, [switch] $ForbidGpuSkin) {
    $Draw = @($Log | Select-String 'FRAME-AUDIT character-draw ')
    $Skin = @($Log | Select-String 'FRAME-AUDIT character-gpu-skin ')
    if (-not $Draw.Count) { throw 'Missing character Draw audit.' }
    $ExpectedSkinRows = 0
    foreach ($Line in $Draw) {
        if ($Line.Line -notmatch 'reference_vertices=(\d+) output_vertices=(\d+).*bind_vertices=(\d+)') { throw 'Malformed character Draw audit.' }
        $Reference = [int64]$Matches[1]; $Output = [int64]$Matches[2]; $Bind = [int64]$Matches[3]
        if ($Bind -gt 0 -and $Output -ne $Bind) { throw 'Character bind/output vertex counts differ.' }
        if ($RequireReference -and $Bind -gt 0 -and $Reference -ne $Bind) { throw 'Character rollback/reference vertex count differs.' }
        if (-not $RequireReference -and $Reference -ne 0) { throw 'Normal GPU character frame retained CPU reference vertices.' }
        if ($Bind -gt 0) { ++$ExpectedSkinRows }
    }
    if ($ForbidGpuSkin) { if ($Skin.Count) { throw 'Character rollback dispatched GPU skinning.' } }
    elseif ($Skin.Count -ne $ExpectedSkinRows) { throw 'GPU skin audit count differs from character Draw input count.' }
}
function Assert-VertexDiff([object[]] $Log) {
    $Rows = @($Log | Select-String 'FRAME-AUDIT character-vertex-diff ')
    if ($Rows.Count -ne 1 -or $Rows[0].Line -notmatch 'position_mismatches=0 normal_mismatches=0 uv_mismatches=0') { throw 'GPU character vertex diff failed.' }
}
function Measure-Run([string] $Name, [string] $LogPath, [int] $Frames, [switch] $Campaign) {
    $Json = Join-Path $OutputDirectory "$Name.metrics.json"
    $Arguments = @('-ExecutionPolicy','Bypass','-File',(Join-Path $Root 'tools/gpu_metrics.ps1'),'-LogPath',$LogPath,'-WarmupFrames','16','-ExpectedFrames',"$Frames",'-ExpectedPath','gpu-native','-OutputJson',$Json)
    if ($Campaign) { $Arguments += '-RequireCampaignLoad' }
    Run-Process "$Name-metrics" 'powershell.exe' $Arguments $Root | Out-Null
}

try {
    Run-Process 'logic' $Exe @('--logic-test') $Package | Out-Null
    Run-Process 'hosted-graphics' $Graphics @((Join-Path $OutputDirectory 'graphics-proof')) | Out-Null
    Run-Process 'resource-cache' $Cache @() | Out-Null
    Run-Process 'raster-differential' $Diff @('--artifact-dir',(Join-Path $OutputDirectory 'differential-failure')) | Out-Null
    Run-Process 'mixed-executor' $Mixed @() | Out-Null
    Run-Process 'mixed-native-resize' $Mixed @('--native-window') | Out-Null

    $Near = Run-Game 'near-native' @('--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene','near','0','--frame-audit','--frames','30') 30 'gpu-native'
    Assert-Character $Near.log
    $Vertex = Run-Game 'character-vertex-diff' @('--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene','near','0','--gpu-normal-fixed-tick','--gpu-character-vertex-diff','--frame-audit','--no-stats','--frames','30') 30 'gpu-native'
    Assert-VertexDiff $Vertex.log

    if ($Full) {
        foreach ($View in @('near','mid')) {
            $Stream = Join-Path $OutputDirectory "$View.bin"
            Run-Game "stream-$View" @('--gpu-world-raster-test',$View,'0',$Stream) | Out-Null
            Run-Process "diff-$View" $Diff @('--replay-raster-stream',$Stream,'--artifact-dir',(Join-Path $OutputDirectory "diff-$View"),'--capture-success') | Out-Null
            Run-Game "cpu-$View" @('--renderer','cpu','--gpu-normal-scene',$View,'0','--frame-audit','--frames','30') 30 'cpu' | Out-Null
            $Capture = Join-Path $OutputDirectory "$View-gpu.bmp"
            $GpuRun = Run-Game "gpu-$View" @('--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene',$View,'0','--gpu-frame-capture',$Capture,'--gpu-capture-frame','30','--frame-audit','--frames','30') 30 'gpu-native'
            Assert-Character $GpuRun.log
            if (-not (Test-Path -LiteralPath $Capture)) { throw "Missing $View GPU capture." }
        }
        $ThinFar = Run-Game 'thin-far' @('--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene','thin-far','0','--frame-audit','--frames','30') 30 'gpu-native'
        Assert-Character $ThinFar.log
        $Stable = Run-Game 'presenter-300' @('--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene','near','0','--frame-audit','--frames','300') 300 'gpu-native'
        Measure-Run 'presenter-300' $Stable.log_path 300
        $Cycle = Run-Game 'world-cycle' @('--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-world-cycle-test','--frame-audit','--frames','120') 120 'gpu-native'
        $CycleRows = @($Cycle.log | Select-String 'GPU-WORLD-CYCLE-RESOURCES ')
        $Resources = @($Cycle.log | Select-String 'FRAME-AUDIT draw-resources ')
        $Ground = @($Cycle.log | Select-String 'FRAME-AUDIT ground-draw ')
        $Map = @($Cycle.log | Select-String 'FRAME-AUDIT map-draw ')
        if ($CycleRows.Count -ne 3 -or $Resources.Count -ne 120 -or $Ground.Count -ne 120 -or $Map.Count -ne 120) { throw 'World-cycle lifetime/mesh audit is incomplete.' }
        foreach ($Row in $CycleRows) { if ($Row.Line -notmatch 'retired=([1-9]\d*) pinned=([1-9]\d*)') { throw 'World switch did not retain an in-flight retired generation.' } }
        if ($Resources[-1].Line -notmatch 'retired=0 .*failed=0') { throw 'Retired world resources did not drain.' }
        $Wave = Run-Game 'campaign-320' @('--map','rasterfall/assets/maps/rasterfall.map','--gpu-wave-repro','--renderer','gpu-compute','--gpu-required','--gpu-native-present','--frame-audit','--frames','320') 320 'gpu-native'
        Measure-Run 'campaign-320' $Wave.log_path 320 -Campaign
        foreach ($Enemies in @(30,60)) {
            $EnemyRun = Run-Game "near-$Enemies" @('--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene','near',"$Enemies",'--frame-audit','--frames','120') 120 'gpu-native'
            Assert-Character $EnemyRun.log
            Measure-Run "near-$Enemies" $EnemyRun.log_path 120
        }
        $Rollback = Run-Game 'character-rollback' @('--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene','near','0','--gpu-character-skinning-off','--frame-audit','--frames','30') 30 'gpu-native'
        Assert-Character $Rollback.log -RequireReference -ForbidGpuSkin
        foreach ($Scene in @('base','map-wall','map-ramp','map-platform')) {
            $Capture = Join-Path $OutputDirectory "$Scene.bmp"
            Run-Game "capture-$Scene" @('--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene',$Scene,'0','--gpu-frame-capture',$Capture,'--gpu-capture-frame','30','--frame-audit','--frames','30') 30 'gpu-native' | Out-Null
            if (-not (Test-Path -LiteralPath $Capture)) { throw "Missing $Scene visual capture." }
        }
    }
    $Manifest.result = 'PASS'; $Summary.result = 'PASS'; $Summary.passed = $Runs.Count
} catch {
    $Manifest.error = $_.Exception.Message; $Summary.error = $_.Exception.Message; $Summary.failed = 1
    throw
} finally {
    $Manifest.finished_at = (Get-Date).ToString('o'); $Summary.finished_at = $Manifest.finished_at
    Save-Results
}
Write-Host "GPU acceptance $Mode PASS: $OutputDirectory"
