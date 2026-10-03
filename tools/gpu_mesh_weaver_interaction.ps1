<#
.SYNOPSIS
Exercise Mesh Weaver through the real Windows input and native rendering path.
.DESCRIPTION
Uses E, arrows, Enter and Escape. Enter also exercises the normal fire binding.
The task is never started
by a simulation driver. Each weapon runs in a fresh staged process, cuts and
restores power through the terminal, claims one product and fires one real ray.
Requires the read-only MESH-WEAVER-INTERACTION audit and the interaction camera
entry point. Does not build, stage, use fixed ticks, or call gameplay APIs.
Run this serially with other native GPU validation. Evidence is saved even when
an input, interaction range, accounting, renderer or child exit check fails.
#>
[CmdletBinding()]
param(
    [string]$OutputDirectory='tmp/mesh-weaver/interaction',
    [ValidateSet('All','ak','pistol')][string]$Blueprint='All',
    [ValidateRange(60,600)][int]$TimeoutSeconds=180,
    [ValidateRange(80,300)][int]$KeyHoldMs=110,
    [switch]$NoScreenshots,
    [switch]$CheckOnly
)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Exe=Join-Path $Package 'rasterfall.exe'
if(-not (Test-Path -LiteralPath $Exe -PathType Leaf)) {
    throw 'Stage the native package through windows/NativeCodex.ps1 first.'
}
if(-not ('MeshWeaverInteractionWindow' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class MeshWeaverInteractionWindow {
    public delegate bool EnumProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback,IntPtr parameter);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint process);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,StringBuilder name,int maximum);
    [DllImport("user32.dll")] static extern uint MapVirtualKey(uint key,uint mode);
    [DllImport("user32.dll",SetLastError=true)] public static extern bool PostMessage(IntPtr window,uint message,IntPtr a,IntPtr b);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    public static IntPtr Find(int process) {
        IntPtr found=IntPtr.Zero;
        EnumWindows(delegate(IntPtr window,IntPtr parameter) {
            uint owner; GetWindowThreadProcessId(window,out owner);
            var name=new StringBuilder(128);GetClassName(window,name,128);
            if(owner==process && name.ToString()=="SDL_app") {found=window;return false;}
            return true;
        },IntPtr.Zero);
        return found;
    }
    public static void Key(IntPtr window,int code,bool down) {
        long bits=1L|((long)MapVirtualKey((uint)code,0)<<16);
        if(code>=37 && code<=40) bits|=0x01000000L;
        if(!down) bits|=0xC0000000L;
        if(!PostMessage(window,down?0x100u:0x101u,new IntPtr(code),new IntPtr(bits)))
            throw new InvalidOperationException("Posting keyboard input failed.");
    }
}
'@
}
if($CheckOnly) {
    Write-Host '[WEAVER-INTERACTION] Script and Win32 helper loaded; GPU was not started.'
    exit 0
}
if(Get-Process rasterfall -ErrorAction SilentlyContinue) {
    throw 'Rasterfall is already running. Interaction evidence requires a serial native run.'
}
$RunId=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
$Out=Join-Path ([IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))) $RunId
New-Item -ItemType Directory -Path $Out | Out-Null
$Utf8=[Text.UTF8Encoding]::new($false)
$SavedPath=$env:PATH
$Keys=@('RF_WEAVER_VIEW','RF_WEAVER_BLUEPRINT','RF_WEAVER_TIME_MS','RF_WEAVER_PAUSE_MS',
    'RF_WEAVER_RESUME_MS','RF_WEAVER_MENU','RF_WEAVER_INTERACTION_AUDIT','RF_SCENE_PERF_FRAMES',
    'RF_WEAVER_CAPTURE_DIRECTORY','RF_WEAVER_CAPTURE_EVERY')
$Saved=@{}
foreach($Key in $Keys){$Saved[$Key]=[Environment]::GetEnvironmentVariable($Key,'Process')}
$Results=[Collections.Generic.List[object]]::new()
$script:WeaverInteraction=$null

function Quote-NativeArgument([string]$Value) {
    $Value=[regex]::Replace($Value,'(\\*)"','$1$1\"')
    $Value=[regex]::Replace($Value,'(\\+)$','$1$1')
    return '"'+$Value+'"'
}
function Assert-Interaction([bool]$Condition,[string]$Message) {
    if(-not $Condition){throw $Message}
}
function Read-Interaction {
    $c=$script:WeaverInteraction
    if(-not $c.reader -and (Test-Path -LiteralPath $c.stdout)) {
        $stream=[IO.FileStream]::new($c.stdout,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
        $c.reader=[IO.StreamReader]::new($stream,[Text.Encoding]::UTF8)
    }
    if($c.reader) {
        $c.pending+=$c.reader.ReadToEnd()
        $lastNewline=$c.pending.LastIndexOf("`n")
        if($lastNewline -ge 0) {
            $complete=$c.pending.Substring(0,$lastNewline)
            $c.pending=$c.pending.Substring($lastNewline+1)
            foreach($line in ($complete -split "`n")) {
                if($line.TrimEnd("`r") -match '^MESH-WEAVER-INTERACTION (\{.*\})$') {
                    $sample=$Matches[1]|ConvertFrom-Json
                    foreach($field in @('frame','normal_sim','driver','menu','near','row','selected',
                        'phase','pause_reason','progress','elapsed_ms','energy_kj','used_form_kj','used_base_kj',
                        'power_on','serial','produced','collected','output_slot','highlight','weapon',
                        'standard_mag','mag','reserve','fire_seq','shots','ray_count','switch_ms')) {
                        if(-not $sample.PSObject.Properties[$field]){throw "Interaction audit lacks $field; refresh the staged executable."}
                    }
                    Assert-Interaction ($sample.normal_sim -eq 1 -and $sample.driver -eq 0) 'A fixed-tick or automatic task driver contaminated this input run.'
                    $c.latest=$sample
                    $c.samples.Add($sample)
                }
            }
        }
    }
    if([DateTime]::UtcNow -gt $c.deadline){throw 'Native interaction exceeded its overall deadline.'}
    $c.process.Refresh()
    if($c.process.HasExited){throw "Native process exited before the input sequence completed: $($c.process.ExitCode)"}
}
function Wait-Interaction([string]$Name,[scriptblock]$Condition,[int]$WaitMs=15000,[long]$AfterFrame=-1) {
    $until=[DateTime]::UtcNow.AddMilliseconds($WaitMs)
    while([DateTime]::UtcNow -lt $until) {
        Read-Interaction
        $sample=$script:WeaverInteraction.latest
        if($sample -and $sample.frame -gt $AfterFrame -and (& $Condition $sample)) {
            Write-Host "[WEAVER-INTERACTION] $($script:WeaverInteraction.name) $Name frame=$($sample.frame)"
            return $sample
        }
        Start-Sleep -Milliseconds 40
    }
    $last=$script:WeaverInteraction.latest|ConvertTo-Json -Compress
    throw "$Name timed out. Last audit: $last"
}
function Send-InteractionKey([int]$Code,[string]$Name) {
    $c=$script:WeaverInteraction
    $c.actions.Add([ordered]@{input='key';name=$Name;code=$Code;hold_ms=$KeyHoldMs;
        frame=$c.latest.frame;utc=[DateTime]::UtcNow.ToString('o')})
    [MeshWeaverInteractionWindow]::Key($c.window,$Code,$true)
    try{Start-Sleep -Milliseconds $KeyHoldMs}
    finally{[MeshWeaverInteractionWindow]::Key($c.window,$Code,$false)}
}
function Save-InteractionScreen([string]$Name) {
    if($NoScreenshots){return}
    $c=$script:WeaverInteraction
    $c.captureId++
    $pending=Join-Path $c.captureDirectory 'capture.pending'
    [IO.File]::WriteAllText($pending,[string]$c.captureId,$Utf8)
    Move-Item -LiteralPath $pending -Destination (Join-Path $c.captureDirectory 'capture.request') -Force
    $ack=Join-Path $c.captureDirectory 'capture.complete'
    $until=[DateTime]::UtcNow.AddSeconds(15);$capturedFrame=0
    while([DateTime]::UtcNow -lt $until) {
        Read-Interaction
        if(Test-Path -LiteralPath $ack) {
            $value=Get-Content -LiteralPath $ack -Encoding UTF8 -Raw
            if($value -match '^(\d+) (\d+)' -and [int]$Matches[1] -eq $c.captureId) {
                $capturedFrame=[int]$Matches[2];break
            }
        }
        Start-Sleep -Milliseconds 40
    }
    Assert-Interaction ($capturedFrame -gt 0) 'Explicit GPU capture did not complete; refresh the staged executable.'
    $path=Join-Path $c.captureDirectory ('frame-{0:D6}.scene.ppm' -f $c.captureId)
    Assert-Interaction ((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).Length -gt 1000) 'Native capture is missing or empty.'
    $c.screens.Add([ordered]@{name=$Name;path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash;
        observed_frame=$capturedFrame;capture='Explicit GPU readback of the normal realtime native frame'})
}
function Run-Interaction([string]$Name) {
    Assert-Interaction (-not (Get-Process rasterfall -ErrorAction SilentlyContinue)) 'Another Rasterfall process appeared before this run.'
    $stdout=Join-Path $Out "$Name.out";$stderr=Join-Path $Out "$Name.err"
    $argv=@('--renderer','gpu-scene','--skip-boot','--map','rasterfall/assets/maps/outpost.map',
        '--gpu-normal-scene','mesh-weaver','0','--frame-audit')
    $record=[ordered]@{status='started';blueprint=$Name;arguments=$argv;stdout=$stdout;stderr=$stderr;
        started_utc=[DateTime]::UtcNow.ToString('o');exe_sha256=(Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash;
        input_path='Win32 window messages -> SDL events -> normal runtime command routing';
        simulation='Normal realtime fixed-step scheduling; no diagnostic task start or time override.'}
    $c=[pscustomobject]@{name=$Name;stdout=$stdout;stderr=$stderr;process=$null;reader=$null;pending='';
        latest=$null;window=[IntPtr]::Zero;deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds);
        captureId=0;captureDirectory=(Join-Path $Out "$Name-frames");
        samples=[Collections.Generic.List[object]]::new();actions=[Collections.Generic.List[object]]::new();
        screens=[Collections.Generic.List[object]]::new()}
    $script:WeaverInteraction=$c
    try {
        if(-not $NoScreenshots) {
            New-Item -ItemType Directory -Path $c.captureDirectory | Out-Null
            $env:RF_WEAVER_CAPTURE_DIRECTORY=$c.captureDirectory
        } else {$env:RF_WEAVER_CAPTURE_DIRECTORY=$null}
        $quoted=($argv|ForEach-Object {Quote-NativeArgument $_}) -join ' '
        $c.process=Start-Process -FilePath $Exe -WorkingDirectory $Package -ArgumentList $quoted `
            -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
        $handle=$c.process.Handle
        $until=[DateTime]::UtcNow.AddSeconds(30)
        while($c.window -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $until) {
            $c.window=[MeshWeaverInteractionWindow]::Find($c.process.Id)
            if($c.process.WaitForExit(40)){throw "Native startup exited: $($c.process.ExitCode)"}
        }
        Assert-Interaction ($c.window -ne [IntPtr]::Zero) 'No SDL native window appeared.'
        [void][MeshWeaverInteractionWindow]::SetForegroundWindow($c.window)
        $idle=Wait-Interaction 'idle in terminal range' {param($s) $s.phase -eq 0 -and $s.menu -eq 0 -and $s.near -eq 1}
        Assert-Interaction ($idle.serial -eq 0 -and $idle.produced -eq 0 -and $idle.collected -eq 0) 'The native run did not start with an empty manufacturing machine.'
        Save-InteractionScreen 'idle'
        Send-InteractionKey 69 'E open terminal'
        $opened=Wait-Interaction 'terminal opened by E' {param($s) $s.menu -eq 1 -and $s.row -eq 0} 15000 $idle.frame
        if($Name -eq 'pistol') {
            Send-InteractionKey 39 'Right select Pistol'
            $opened=Wait-Interaction 'Pistol selected' {param($s) $s.menu -eq 1 -and $s.selected -eq 1} 15000 $opened.frame
        } else {Assert-Interaction ($opened.selected -eq 0) 'Initial terminal selection is not AK.'}
        Send-InteractionKey 40 'Down select start'
        $row=Wait-Interaction 'start row selected' {param($s) $s.menu -eq 1 -and $s.row -eq 1} 15000 $opened.frame
        Send-InteractionKey 13 'Enter start task'
        $started=Wait-Interaction 'task started by Enter' {param($s) $s.serial -eq 1 -and $s.phase -ge 1 -and $s.phase -le 2} 15000 $row.frame
        Save-InteractionScreen 'started'
        Send-InteractionKey 27 'Escape close terminal'
        $closed=Wait-Interaction 'task advances with terminal closed' {param($s) $s.menu -eq 0 -and $s.elapsed_ms -gt ($started.elapsed_ms+160) -and $s.phase -eq 2} 15000 $started.frame
        Send-InteractionKey 69 'E reopen terminal'
        $opened=Wait-Interaction 'terminal reopened during task' {param($s) $s.menu -eq 1 -and $s.row -eq 1} 15000 $closed.frame
        Send-InteractionKey 40 'Down select power'
        $row=Wait-Interaction 'power row selected' {param($s) $s.menu -eq 1 -and $s.row -eq 2} 15000 $opened.frame
        Send-InteractionKey 13 'Enter cut power'
        $off=Wait-Interaction 'power cut pauses real task' {param($s) $s.power_on -eq 0 -and $s.pause_reason -eq 2 -and $s.progress -gt 0 -and $s.progress -lt 1} 15000 $row.frame
        Save-InteractionScreen 'power-off'
        Send-InteractionKey 27 'Escape observe paused machine'
        $closed=Wait-Interaction 'paused terminal closed' {param($s) $s.menu -eq 0 -and $s.power_on -eq 0} 15000 $off.frame
        $pauseUntil=[DateTime]::UtcNow.AddMilliseconds(1400)
        while([DateTime]::UtcNow -lt $pauseUntil){Read-Interaction;Start-Sleep -Milliseconds 40}
        $still=Wait-Interaction 'pause remains observable' {param($s) $s.power_on -eq 0 -and $s.pause_reason -eq 2} 15000 $closed.frame
        foreach($field in @('elapsed_ms','progress','energy_kj','used_form_kj','used_base_kj')) {
            Assert-Interaction ([Math]::Abs([double]$still.$field-[double]$off.$field) -le 0.000001) "Power-off changed $field."
        }
        Save-InteractionScreen 'paused'
        Send-InteractionKey 69 'E reopen paused terminal'
        $opened=Wait-Interaction 'paused terminal reopened' {param($s) $s.menu -eq 1 -and $s.row -eq 2} 15000 $still.frame
        Send-InteractionKey 13 'Enter restore power'
        $resumed=Wait-Interaction 'power resumes existing task' {param($s) $s.power_on -eq 1 -and $s.pause_reason -eq 0 -and $s.elapsed_ms -gt $off.elapsed_ms} 15000 $opened.frame
        Assert-Interaction ($resumed.serial -eq $off.serial -and $resumed.energy_kj -lt $off.energy_kj -and $resumed.progress -ge $off.progress) 'Resume reset the task or refilled its source.'
        Send-InteractionKey 27 'Escape close resumed terminal'
        $ready=Wait-Interaction 'one finished product ready' {param($s) $s.phase -eq 4 -and $s.menu -eq 0 -and $s.output_slot -ge 0 -and $s.highlight -eq $s.output_slot} 60000 $resumed.frame
        Assert-Interaction ($ready.produced -eq 1 -and $ready.collected -eq 0 -and $ready.highlight -eq $ready.output_slot) 'Finished product is not the normally highlighted pickup at the interaction entry point.'
        Assert-Interaction ([Math]::Abs(($idle.energy_kj-$ready.energy_kj)-($ready.used_form_kj+$ready.used_base_kj)) -le 0.00001) 'Native task accounting does not conserve the source reserve.'
        Save-InteractionScreen 'ready'
        Send-InteractionKey 69 'E claim finished weapon'
        $claimed=Wait-Interaction 'real E claims exactly one weapon' {param($s) $s.phase -eq 0 -and $s.collected -eq 1 -and $s.output_slot -lt 0 -and $s.menu -eq 0} 15000 $ready.frame
        $expectedWeapon=if($Name -eq 'ak'){3}else{0}
        Assert-Interaction ($claimed.weapon -eq $expectedWeapon -and $claimed.standard_mag -gt 0 -and $claimed.mag -eq $claimed.standard_mag -and $claimed.reserve -eq 0) 'Claim did not equip the requested weapon with exactly its standard magazine and zero reserve.'
        $armed=Wait-Interaction 'weapon switch completes' {param($s) $s.switch_ms -eq 0 -and $s.menu -eq 0} 15000 $claimed.frame
        Save-InteractionScreen 'equipped'
        Send-InteractionKey 13 'Enter real fire binding'
        $fired=Wait-Interaction 'real weapon fires' {param($s) $s.fire_seq -gt $armed.fire_seq -and $s.shots -gt $armed.shots -and $s.ray_count -gt 0} 15000 $armed.frame
        Assert-Interaction ($fired.weapon -eq $expectedWeapon -and $fired.mag -eq ($armed.mag-1) -and $fired.reserve -eq 0 -and $fired.produced -eq 1 -and $fired.collected -eq 1) 'Firing did not consume exactly one paid round or changed manufacturing ownership.'
        Save-InteractionScreen 'fired'
        Send-InteractionKey 69 'E after collection opens terminal'
        $reopened=Wait-Interaction 'collected product cannot be reclaimed' {param($s) $s.menu -eq 1 -and $s.collected -eq 1 -and $s.output_slot -lt 0} 15000 $fired.frame
        Assert-Interaction ($reopened.mag -eq $fired.mag -and $reopened.reserve -eq 0 -and $reopened.produced -eq 1) 'A second E interaction refilled or duplicated the manufactured weapon.'
        $record.final_state=$reopened
        $c.actions.Add([ordered]@{input='window_close';frame=$reopened.frame;utc=[DateTime]::UtcNow.ToString('o')})
        [void][MeshWeaverInteractionWindow]::PostMessage($c.window,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
        if(-not $c.process.WaitForExit(30000)){throw 'Native window did not finish shutdown after WM_CLOSE.'}
        $c.process.WaitForExit();$c.process.Refresh()
        $record.exit_code=$c.process.ExitCode
        Assert-Interaction ($record.exit_code -eq 0) "Native exit $($record.exit_code)."
        $log=Get-Content -LiteralPath $stdout,$stderr -Encoding UTF8|Out-String
        Assert-Interaction ($log -match 'SCENE-NATIVE.*bridges=0.*mixed_execute=0' -and $log -match 'SCENE-SOURCE.*independent=1.*legacy_producer=0') 'Missing independent native rendering evidence.'
        Assert-Interaction ($log -notmatch 'SCENE-NATIVE[^\r\n]*(bridges|mixed_execute)=[1-9]' -and $log -notmatch 'SCENE-SOURCE[^\r\n]*(legacy_producer|raster_commands|mixed_draws)=[1-9]') 'Native interaction used a legacy or mixed rendering path.'
        $readbackFrames=@([regex]::Matches($log,'SCENE-NATIVE frame=(\d+)[^\r\n]*readback=1')|ForEach-Object {[int]$_.Groups[1].Value})
        Assert-Interaction ($readbackFrames.Count -eq $c.screens.Count) 'Unexpected readback outside explicitly requested screenshots.'
        foreach($screen in $c.screens) {
            Assert-Interaction ($readbackFrames -contains $screen.observed_frame) 'Screenshot is not tied to its native readback frame.'
        }
        Assert-Interaction ($log -notmatch 'Validation Error|SYNC-HAZARD|VUID-|preparation/submit failed|MESH-WEAVER-ERROR') 'Native log contains a rendering or manufacturing error.'
        Assert-Interaction ($log -notmatch '(?m)^MESH-WEAVER-DIAGNOSTIC' -and $log -notmatch 'RF_WEAVER_TIME_MS') 'Unexpected deterministic manufacturing driver evidence.'
        $record.status='passed'
        Write-Host "[WEAVER-INTERACTION] $Name PASS real UI/pause/resume/claim/fire; evidence $Out"
    } catch {
        $record.status='failed';$record.error=$_.Exception.Message
        if($c.window -ne [IntPtr]::Zero){try{Save-InteractionScreen 'failure'}catch{}}
        throw
    } finally {
        if($c.reader){$c.reader.Dispose()}
        if($c.process) {
            $c.process.Refresh()
            if(-not $c.process.HasExited) {
                if($c.window -ne [IntPtr]::Zero){[void][MeshWeaverInteractionWindow]::PostMessage($c.window,0x10,[IntPtr]::Zero,[IntPtr]::Zero)}
                if(-not $c.process.WaitForExit(10000)){Stop-Process -Id $c.process.Id -Force;$c.process.WaitForExit()}
            }
            $c.process.Refresh();$record.exit_code=$c.process.ExitCode
        }
        $record.finished_utc=[DateTime]::UtcNow.ToString('o')
        $record.actions=$c.actions.ToArray();$record.samples=$c.samples.ToArray();$record.screenshots=$c.screens.ToArray()
        [IO.File]::WriteAllText((Join-Path $Out "$Name.json"),($record|ConvertTo-Json -Depth 9),$Utf8)
        $Results.Add([ordered]@{blueprint=$Name;status=$record.status;exit_code=$record.exit_code;evidence="$Name.json"})
    }
}
try {
    Remove-Item Env:Path -ErrorAction SilentlyContinue
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:PATH=$SavedPath
    foreach($Key in $Keys){[Environment]::SetEnvironmentVariable($Key,$null,'Process')}
    $env:RF_WEAVER_VIEW='interaction'
    $env:RF_WEAVER_INTERACTION_AUDIT='1'
    if($Blueprint -eq 'All'){Run-Interaction 'ak';Run-Interaction 'pistol'}
    else {Run-Interaction $Blueprint}
} finally {
    try{[IO.File]::WriteAllText((Join-Path $Out 'runs.json'),($Results.ToArray()|ConvertTo-Json -Depth 4),$Utf8)}
    finally {
        Remove-Item Env:Path -ErrorAction SilentlyContinue
        Remove-Item Env:PATH -ErrorAction SilentlyContinue
        $env:PATH=$SavedPath
        foreach($Key in $Keys){[Environment]::SetEnvironmentVariable($Key,$Saved[$Key],'Process')}
    }
}
