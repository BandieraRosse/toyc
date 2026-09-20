[CmdletBinding()]
param([string] $OutputDirectory = '', [switch] $NoRedirect)
# NoRedirect avoids an intermittent PowerShell GUI-process pipe wait. The
# per-frame runtime log remains the gate's complete audit source.
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package = Join-Path $Root 'build-windows/rasterfall-windows'
$Exe = Join-Path $Package 'rasterfall.exe'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $Root ('tmp/hg-resize-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new evidence directory.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$ToolPath = $env:Path
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $ToolPath, 'Process')
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class HgResizeWindow {
    public delegate bool Visitor(IntPtr window, IntPtr argument);
    [DllImport("user32.dll")] public static extern bool EnumWindows(Visitor visit, IntPtr argument);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("kernel32.dll")] public static extern bool GetExitCodeProcess(IntPtr process, out uint code);
    public static IntPtr Find(int process) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr window, IntPtr argument) {
            uint owner; GetWindowThreadProcessId(window, out owner);
            if (owner == process) { found = window; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@
$RuntimeLog = Join-Path $Package 'rasterfall.log'
$PreviousLines = if (Test-Path -LiteralPath $RuntimeLog) { @(Get-Content -LiteralPath $RuntimeLog).Count } else { 0 }
$ProgramArgs = @('--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-post-fog','--gpu-normal-scene','near','0','--frame-audit','--frames','140')
$Record = [ordered]@{ executable = (Get-FileHash -LiteralPath $Exe).Hash; argv = $ProgramArgs; result = 'FAIL' }
$Process = $null
try {
    if ($NoRedirect) {
        $Process = Start-Process -FilePath $Exe -ArgumentList $ProgramArgs -WorkingDirectory $Package -WindowStyle Hidden -PassThru
    } else {
        $Process = Start-Process -FilePath $Exe -ArgumentList $ProgramArgs -WorkingDirectory $Package -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $OutputDirectory 'stdout.txt') -RedirectStandardError (Join-Path $OutputDirectory 'stderr.txt')
    }
    # Retain the process handle before polling HasExited. PowerShell 5 can
    # otherwise return a null Process.ExitCode for asynchronously started apps.
    $ProcessHandle = $Process.Handle
    $Window = [IntPtr]::Zero
    for ($Attempt = 0; $Attempt -lt 50 -and $Window -eq [IntPtr]::Zero; ++$Attempt) {
        if ($Process.HasExited) { throw 'Process exited before window discovery.' }
        $Window = [HgResizeWindow]::Find($Process.Id)
        if ($Window -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 100 }
    }
    if ($Window -eq [IntPtr]::Zero) { throw 'No owned window found.' }
    Start-Sleep -Milliseconds 2000
    foreach ($Size in @(@(980,620), @(1300,780), @(820,640))) {
        if ($Process.HasExited) { throw 'Process exited before resize.' }
        # SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE; only this process's window.
        if (-not [HgResizeWindow]::SetWindowPos($Window, [IntPtr]::Zero, 0, 0, $Size[0], $Size[1], 0x16)) { throw 'SetWindowPos failed.' }
        Start-Sleep -Milliseconds 1500
    }
    # The current Intel correctness path waits for present completion every
    # frame; four extent rebuilds can exceed the former three-minute budget.
    if (-not $Process.WaitForExit(300000)) { throw 'Resize smoke timed out.' }
    $Process.WaitForExit()
    [uint32] $ExitCode = 0
    if (-not [HgResizeWindow]::GetExitCodeProcess($ProcessHandle, [ref] $ExitCode)) { throw 'Cannot read process exit code.' }
    $Record.exit_code = $ExitCode
    if ($ExitCode -ne 0) { throw "Resize smoke exited $ExitCode." }
    $Log = @(Get-Content -LiteralPath $RuntimeLog | Select-Object -Skip $PreviousLines)
    $Log | Set-Content -Encoding UTF8 (Join-Path $OutputDirectory 'runtime.log')
    $Frames = @($Log | Select-String 'FRAME-AUDIT frame=')
    $Gpu = @($Log | Select-String 'FRAME-AUDIT gpu ')
    $Layers = @($Log | Select-String 'FRAME-AUDIT layers ')
    $Resources = @($Log | Select-String 'FRAME-AUDIT draw-resources ')
    if ($Frames.Count -ne 140 -or $Gpu.Count -ne 140 -or $Layers.Count -ne 140 -or $Resources.Count -ne 140) { throw 'Incomplete frame audit.' }
    foreach ($Line in $Frames) { if ($Line.Line -notmatch 'path=gpu-native ') { throw 'Unexpected render path.' } }
    foreach ($Line in $Gpu) { if ($Line.Line -notmatch 'readback_bytes=0 cpu_framebuffer_copy_bytes=0') { throw 'Readback/copy detected.' } }
    foreach ($Line in $Layers) { if ($Line.Line -notmatch 'invalid_transitions=0 .*pre_post_cpu_fallback=0 fallback_reason=0x0 ') { throw 'Fallback/order failure.' } }
    $Pinned = @()
    foreach ($Line in $Resources) {
        if ($Line.Line -notmatch 'live=(\d+) retired=0 pinned=(\d+) failed=0') { throw 'Resource lifetime failure.' }
        $LiveCount = [int]$Matches[1]
        $PinnedCount = [int]$Matches[2]
        if ($PinnedCount -gt $LiveCount) { throw 'Pinned resource count exceeds live resources.' }
        $Pinned += $PinnedCount
    }
    $Extents = @($Frames | ForEach-Object { if ($_.Line -match 'extent=(\d+x\d+)') { $Matches[1] } } | Sort-Object -Unique)
    $Loads = @($Resources | ForEach-Object { if ($_.Line -match 'loads=(\d+) ') { $Matches[1] } } | Sort-Object -Unique)
    if ($Extents.Count -lt 4 -or $Loads.Count -ne 1) { throw 'Resize did not produce four extents or reloaded mesh resources.' }
    $Record.extents = $Extents; $Record.loads = $Loads
    $Record.pinned = @($Pinned | Sort-Object -Unique); $Record.frames = $Frames.Count
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
Write-Host "HG resize PASS: $OutputDirectory"
