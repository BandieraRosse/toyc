[CmdletBinding()]
param(
    [string] $OutputDirectory = '',
    [string] $ValidationLayerDirectory = '',
    [ValidateSet('All','Validation','Faults','Soak')][string] $Stage = 'All',
    [switch] $SkipSoak
)
$ErrorActionPreference = 'Stop'
if ($Stage -eq 'Soak' -and $SkipSoak) { throw 'Stage Soak cannot be combined with SkipSoak.' }
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package = Join-Path $Root 'build-windows/rasterfall-windows'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $Root ('tmp/rb0-special-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$Out = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path $Out) { throw 'Use a new output directory.' }
if (-not $ValidationLayerDirectory) { $ValidationLayerDirectory = Join-Path $Root 'tmp/hg2a-tools/mingw64/bin' }
if (-not (Test-Path (Join-Path $ValidationLayerDirectory 'VkLayer_khronos_validation.json'))) { throw 'Validation layer manifest missing.' }
$TaskPath = $env:Path
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $TaskPath, 'Process')
$Msys = if ($env:RF_WINDOWS_MSYS2_ROOT) { $env:RF_WINDOWS_MSYS2_ROOT } else { 'C:\msys64' }
$SavedEnvironment = @{}
foreach ($Key in @('VK_LAYER_PATH','VK_INSTANCE_LAYERS','VK_VALIDATION_VALIDATE_SYNC','VK_LAYER_REPORT_FLAGS','VK_LOADER_DEBUG')) {
    $SavedEnvironment[$Key] = [Environment]::GetEnvironmentVariable($Key, 'Process')
}
New-Item -ItemType Directory -Path $Out | Out-Null
$Records = [Collections.Generic.List[object]]::new()
$Manifest = [ordered]@{ result='FAIL'; stage=$Stage; package_sha256=(Get-FileHash "$Package/rasterfall.exe").Hash; skip_soak=[bool]$SkipSoak; runs=$Records }
function Assert-Idle {
    if (Get-Process | Where-Object { $_.ProcessName -match '^(rasterfall|rf-gpu)' }) { throw 'Residual GPU process.' }
    if ((powercfg /getactivescheme | Out-String) -notmatch '381b4222-f694-41f0-9685-ff5bb260df2e') { throw 'Balanced power scheme required.' }
}
function Run([string]$Name,[string]$Exe,[string[]]$Argv,[string]$Cwd,[int]$Frames=0,[string]$Fault='',[bool]$ExpectedFailure=$false) {
    Assert-Idle
    Write-Host "[RB0-SPECIAL] $Name"
    $Before = if (Test-Path "$Cwd/rasterfall.log") { @(Get-Content -Encoding UTF8 "$Cwd/rasterfall.log").Count } else { 0 }
    $Quoted = ($Argv | ForEach-Object { if ($_.Contains('"')) { throw 'Embedded quote in argument.' }; '"'+$_+'"' }) -join ' '
    $p = Start-Process -FilePath $Exe -ArgumentList $Quoted -WorkingDirectory $Cwd -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput "$Out/$Name.stdout.txt" -RedirectStandardError "$Out/$Name.stderr.txt"
    $Runtime = @()
    if (Test-Path "$Cwd/rasterfall.log") {
        $Runtime = @(Get-Content -Encoding UTF8 "$Cwd/rasterfall.log" | Select-Object -Skip $Before)
        $Runtime | Set-Content -Encoding UTF8 "$Out/$Name.runtime.log"
    }
    $Records.Add([ordered]@{name=$Name; exit_code=$p.ExitCode; argv=$Argv; expected_failure=$ExpectedFailure})
    $Manifest | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 "$Out/manifest.json"
    if (($ExpectedFailure -and $p.ExitCode -ne 3) -or (-not $ExpectedFailure -and $p.ExitCode -ne 0)) { throw "$Name unexpected exit $($p.ExitCode)" }
    $Logs = (Get-Content "$Out/$Name.stdout.txt","$Out/$Name.stderr.txt" | Out-String)
    if ($Logs -match 'Validation Error|SYNC-HAZARD|VUID-') { throw "$Name validation error" }
    if ($env:VK_INSTANCE_LAYERS -and ($Logs -notmatch 'Insert instance layer.*VK_LAYER_KHRONOS_validation' -or $Logs -notmatch 'CURRENT-VALIDATION-ENABLED' -or $Logs -notmatch ' - Synchronization')) { throw "$Name validation/sync enablement not proven" }
    if ($Fault -and $Logs -notmatch "fault-injection=$Fault frame=10") { throw "$Name fault was not injected" }
    if ($ExpectedFailure -and $Logs -notmatch 'GPU-FRAME attempted=10 rendered=9 cpu_fallback=0') { throw "$Name failed at an unexpected boundary" }
    if ($Frames) {
        $Audit = @($Runtime | Select-String '^FRAME-AUDIT frame=')
        $Present = @($Runtime | Select-String '^PRESENT-AUDIT frame=')
        if ($Audit.Count -ne $Frames -or $Present.Count -ne $Frames) { throw "$Name incomplete frame audit" }
        foreach ($Line in $Audit) { if ($Line.Line -notmatch 'path=gpu-native') { throw "$Name non-native frame" } }
        $Gpu = @($Runtime | Select-String '^FRAME-AUDIT gpu ')
        $Layers = @($Runtime | Select-String '^FRAME-AUDIT layers ')
        if ($Gpu.Count -ne $Frames -or $Layers.Count -ne $Frames) { throw "$Name incomplete GPU/layer audit" }
        foreach ($Line in $Gpu) { if ($Line.Line -notmatch 'readback_bytes=0 cpu_framebuffer_copy_bytes=0') { throw "$Name readback/copy" } }
        foreach ($Line in $Layers) { if ($Line.Line -notmatch 'invalid_transitions=0 .*pre_post_cpu_fallback=0 fallback_reason=0x0 ') { throw "$Name fallback/layer order" } }
        foreach ($Line in $Present) {
            $Poison = if ($Fault -eq 'present-out-of-date' -and $Line.Line -match '^PRESENT-AUDIT frame=10 ') { 1 } else { 0 }
            if ($Line.Line -notmatch "hot_queue_idle_count=0 .*presenter_poisoned=$Poison ") { throw "$Name presenter invariant" }
        }
        if ($Fault -eq 'present-out-of-date') {
            $BeforeRecreate = ($Present | Where-Object { $_.Line -match '^PRESENT-AUDIT frame=10 ' }).Line
            $AfterRecreate = ($Present | Where-Object { $_.Line -match '^PRESENT-AUDIT frame=11 ' }).Line
            if ($BeforeRecreate -notmatch 'swapchain_gen=(\d+) ') { throw 'Missing poisoned generation.' }
            $Generation = [int64]$Matches[1]
            if ($AfterRecreate -notmatch 'swapchain_gen=(\d+) ' -or [int64]$Matches[1] -le $Generation -or $AfterRecreate -notmatch 'recreate_queue_idle_count=[1-9]\d* ') { throw 'Poisoned generation was not retired before the next frame.' }
        }
    }
    if ($Name -eq 'timestamp-truncation-expansion' -and ($Logs -notmatch 'expanded=0 requested=\d+ recorded=16 dropped=[1-9]\d* valid=0 PASS' -or $Logs -notmatch 'expanded=1 requested=\d+ recorded=\d+ dropped=0 valid=1 PASS')) { throw 'Missing timestamp coverage proof.' }
    if ($Name -eq 'soak-10000' -and ($Logs -notmatch 'GPU-FRAME attempted=10000 rendered=10000 cpu_fallback=0' -or $Logs -notmatch 'color-readback=0 cpu-framebuffer-copy=0')) { throw 'Incomplete soak summary.' }
    Assert-Idle
}
try {
    $env:Path = "$ValidationLayerDirectory;$(Join-Path $Msys 'mingw64/bin');$TaskPath"
    $env:VK_LAYER_PATH = $ValidationLayerDirectory
    $env:VK_INSTANCE_LAYERS = 'VK_LAYER_KHRONOS_validation'
    $env:VK_VALIDATION_VALIDATE_SYNC = 'true'
    $env:VK_LAYER_REPORT_FLAGS = 'error,warn,info'
    $env:VK_LOADER_DEBUG = 'layer'
    $Common = @('--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene','near','0','--gpu-normal-fixed-tick')
    if ($Stage -in @('All','Validation')) {
        Run 'timestamp-truncation-expansion' "$Root/build-windows/rf-gpu-raster-test.exe" @('--mixed-gate') $Root
        Run 'validation-resize' "$Root/build-windows/rasterfall-gpu-mixed-test.exe" @('--native-window') $Root
        Run 'validation-300' "$Package/rasterfall.exe" ($Common+@('--frame-audit','--frames','300')) $Package 300
    }
    if ($Stage -in @('All','Faults')) {
    foreach ($Fault in @('acquire-out-of-date','record-failure','submit-failure','present-out-of-date','present-suboptimal')) {
        $Failure = $Fault -in @('record-failure','submit-failure')
        $Frames = if ($Failure) { 9 } else { 30 }
        Run "fault-$Fault" "$Package/rasterfall.exe" ($Common+@('--gpu-present-fault',$Fault,'10','--frame-audit','--frames','30')) $Package $Frames $Fault $Failure
    }
    }
    foreach ($Key in $SavedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($Key, $null, 'Process') }
    if (-not $SkipSoak -and $Stage -in @('All','Soak')) { Run 'soak-10000' "$Package/rasterfall.exe" ($Common+@('--frames','10000')) $Package }
    if ((Get-FileHash "$Package/rasterfall.exe").Hash -ne $Manifest.package_sha256) { throw 'Package changed during suite.' }
    $Manifest.result = 'PASS'
    Write-Host "RB0-SPECIAL PASS $Out"
} finally {
    $Manifest | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 "$Out/manifest.json"
    powercfg /getactivescheme | Out-File "$Out/power.txt"
    foreach ($Key in $SavedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($Key, $SavedEnvironment[$Key], 'Process') }
    $env:Path = $TaskPath
}
