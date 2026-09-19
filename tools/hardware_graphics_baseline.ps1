[CmdletBinding()]
param(
    [string] $OutputDirectory = '',
    [string] $DifferentialExe = 'build-windows/rf-gpu-raster-diff-test.exe',
    [string] $Checkpoint = 'HG-0'
)
# Run after NativeCodex.ps1 package. No build/cache mutation or asset copying here.
$ErrorActionPreference = 'Stop'
# Normalize inherited PATH casing before Windows PowerShell constructs a child
# environment (some agent hosts supply both Path and PATH).
$ToolPath = $env:Path
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $ToolPath, 'Process')
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package = Join-Path $Root 'build-windows/rasterfall-windows'
$Exe = Join-Path $Package 'rasterfall.exe'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $Root ('tmp/hg0-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new output directory to preserve previous evidence.' }
$Diff = (Resolve-Path (Join-Path $Root $DifferentialExe)).Path
if (-not (Test-Path -LiteralPath $Exe)) { throw 'Run windows/NativeCodex.ps1 package first.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$Runs = [Collections.Generic.List[object]]::new()
$Manifest = [ordered]@{
    schema = 1; checkpoint = $Checkpoint; started = (Get-Date).ToString('o')
    commit = (& git -C $Root rev-parse HEAD); worktree = @(& git -C $Root status --short)
    executable = (Get-FileHash -Algorithm SHA256 -LiteralPath $Exe).Hash
    differential = (Get-FileHash -Algorithm SHA256 -LiteralPath $Diff).Hash
    warmup_frames = 16; measured_frames = '17-46'; runs = $Runs
}
try { $Manifest.adapters = @(Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion, PNPDeviceID) }
catch {
    $Manifest.adapter_query_error = $_.Exception.Message
    $Manifest.adapters = @(Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\*' -ErrorAction SilentlyContinue | Where-Object { $_.DriverDesc } | Select-Object DriverDesc, DriverVersion, MatchingDeviceId)
    $Manifest.adapter_source = 'display-class registry (CIM unavailable)'
}
$Manifest.assets = @(Get-ChildItem -LiteralPath (Join-Path $Package 'rasterfall') -File -Recurse | Sort-Object FullName | ForEach-Object {
    @{ path = $_.FullName.Substring($Package.Length + 1); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
})
function Save-Manifest { $Manifest | ConvertTo-Json -Depth 12 | Set-Content -Encoding UTF8 (Join-Path $OutputDirectory 'manifest.json') }
function Run([string] $Name, [string] $Program, [string[]] $ProgramArgs, [int] $Frames = 0, [string] $ExpectedPath = '') {
    Write-Host "[$Checkpoint] $Name"
    $stdout = Join-Path $OutputDirectory "$Name.stdout.txt"
    $stderr = Join-Path $OutputDirectory "$Name.stderr.txt"
    $runtimeLog = Join-Path $Package 'rasterfall.log'
    $previousLines = 0
    if ($Program -eq $Exe -and (Test-Path -LiteralPath $runtimeLog)) {
        $previousLines = @(Get-Content -LiteralPath $runtimeLog).Count
    }
    # CLI arguments have no embedded quote; quote each argument for Windows paths with spaces.
    foreach ($arg in $ProgramArgs) { if ($arg.Contains('"')) { throw 'Embedded quotes are not supported.' } }
    $quoted = ($ProgramArgs | ForEach-Object { '"' + $_ + '"' }) -join ' '
    # The standalone suite writes its replay self-check under repository build/.
    $workingDirectory = if ($Program -eq $Diff) { $Root } else { $Package }
    $p = Start-Process -FilePath $Program -ArgumentList $quoted -WorkingDirectory $workingDirectory -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $record = [ordered]@{ name = $Name; argv = $ProgramArgs; exit_code = $p.ExitCode }
    $Runs.Add($record)
    if ($Program -eq $Exe -and (Test-Path (Join-Path $Package 'rasterfall.log'))) {
        Get-Content -LiteralPath $runtimeLog | Select-Object -Skip $previousLines | Set-Content -Encoding UTF8 (Join-Path $OutputDirectory "$Name.runtime.log")
    }
    Save-Manifest
    if ($p.ExitCode -ne 0) { throw "$Name exited $($p.ExitCode)" }
    if ($Frames) {
        $log = Get-Content -LiteralPath (Join-Path $OutputDirectory "$Name.runtime.log")
        $headers = @($log | Select-String 'FRAME-AUDIT frame=')
        $gpu = @($log | Select-String 'FRAME-AUDIT gpu ')
        $layers = @($log | Select-String 'FRAME-AUDIT layers ')
        if ($headers.Count -ne $Frames -or $gpu.Count -ne $Frames -or $layers.Count -ne $Frames) { throw "$Name incomplete frame audit" }
        foreach ($line in $headers) { if ($line.Line -notmatch "path=$ExpectedPath ") { throw "$Name unexpected render path" } }
        foreach ($line in $gpu) { if ($line.Line -notmatch 'readback_bytes=0 cpu_framebuffer_copy_bytes=0') { throw "$Name readback/copy detected" } }
        foreach ($line in $layers) { if ($line.Line -notmatch 'invalid_transitions=0 .*pre_post_cpu_fallback=0 fallback_reason=0x0 ') { throw "$Name fallback/order failure" } }
        $stats = [ordered]@{}
        foreach ($field in @('frontend_ms','fence_wait_ms','native_present_queue_idle_ms')) {
            $values = @($gpu | Select-Object -Skip 16 | ForEach-Object { if ($_.Line -match "$field=([0-9.]+)") { [double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture) } } | Sort-Object)
            if ($values.Count -ne ($Frames - 16)) { throw "$Name missing timing $field" }
            $n = $values.Count
            $stats[$field] = @{ median = ($values[[int][Math]::Floor(($n-1)/2)] + $values[[int][Math]::Floor($n/2)])/2; p95 = $values[[int][Math]::Ceiling($n*0.95)-1] }
        }
        $record.statistics = $stats
        $record.measured_frames = "17-$Frames"
        $record.verified_frames = $headers.Count
        Save-Manifest
    }
}
try {
    Run 'differential-suite' $Diff @('--artifact-dir',(Join-Path $OutputDirectory 'differential-failure'))
    Run 'help' $Exe @('--help')
    Run 'logic' $Exe @('--logic-test')
    Run 'offscreen-near' $Exe @('--normal-frame-audit','-13000','-12000','0','1024','0','1024','1280','720',(Join-Path $OutputDirectory 'offscreen-near.bmp'))
    foreach ($view in @('near','mid')) {
        foreach ($enemies in @('0','30')) {
            $name = "$view-$enemies"
            $stream = Join-Path $OutputDirectory "$name.bin"
            Run "stream-$name" $Exe @('--gpu-world-raster-test',$view,$enemies,$stream)
            $capture = Join-Path $OutputDirectory "reference-$name"
            Run "reference-$name" $Diff @('--replay-raster-stream',$stream,'--artifact-dir',$capture,'--capture-success')
            foreach ($file in @('cpu-color.bmp','gpu-color.bmp','cpu-depth.bin','gpu-depth.bin','report.txt')) {
                if (-not (Test-Path -LiteralPath (Join-Path $capture $file))) { throw "Missing reference artifact: $name/$file" }
            }
        }
        Run "cpu-$view" $Exe @('--renderer','cpu','--gpu-normal-scene',$view,'0','--frame-audit','--frames','46') 46 'cpu'
        foreach ($fog in @($false,$true)) {
            $argsForRun = @('--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene',$view,'0','--frame-audit','--frames','46')
            if ($fog) { $argsForRun += '--gpu-post-fog' }
            Run "native-$view-fog-$fog" $Exe $argsForRun 46 'gpu-native'
        }
    }
    Run 'wave' $Exe @('--map','rasterfall/assets/maps/rasterfall.map','--gpu-wave-repro','--frames','320','--renderer','gpu-compute','--gpu-required','--gpu-native-present','--frame-audit') 320 'gpu-native'
    $waveOutput = Get-Content -Raw (Join-Path $OutputDirectory 'wave.stdout.txt')
    if ($waveOutput -notmatch 'GPU-WAVE-REPRO world=1 ' -or $waveOutput -notmatch 'GPU-WAVE-REPRO wave=\d+ phase=\d+ alive=[1-9]') { throw 'Wave did not exercise Campaign enemies.' }
    $Manifest.result = 'PASS'
} catch {
    $Manifest.result = 'FAIL'
    $Manifest.error = $_.Exception.Message
    throw
} finally {
    $Manifest.finished = (Get-Date).ToString('o')
    Save-Manifest
}
Write-Host "[$Checkpoint] PASS: $OutputDirectory"
