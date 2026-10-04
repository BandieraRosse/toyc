<#
.SYNOPSIS
Exercise player UI and NULL communications through native Windows input.
.DESCRIPTION
Runs the staged executable serially. Uses normal realtime simulation and real
Win32 -> SDL input. Audits are observations, never gameplay command injection.
Outpost checks mode/view switches, terminal focus, stale answers and video hide.
Resume restarts with the isolated Outpost files and verifies persisted state.
Weaver checks GUI start, terminal power/status and GUI claim on the same task.
Output includes all samples/actions, explicit native captures and actual exit.
#>
[CmdletBinding()]
param(
    [ValidateSet('All','Outpost','Resume','Weaver','Boundaries','RenderBoundaries')][string]$Stage='All',
    [string]$OutputDirectory='tmp/player-ui/interaction',
    [string]$ResumeDirectory='',
    [ValidateRange(90,600)][int]$TimeoutSeconds=240,
    [ValidateRange(80,300)][int]$KeyHoldMs=110,
    [switch]$NoScreenshots,
    [switch]$CheckOnly
)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Exe=Join-Path $Package 'rasterfall.exe'
$Utf8=[Text.UTF8Encoding]::new($false)
if(-not ('PlayerUiWindow' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class PlayerUiWindow {
    public delegate bool EnumProc(IntPtr window,IntPtr state);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback,IntPtr state);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint process);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,StringBuilder name,int size);
    [DllImport("user32.dll")] static extern uint MapVirtualKey(uint key,uint mode);
    [DllImport("user32.dll",SetLastError=true)] public static extern bool PostMessage(IntPtr window,uint message,IntPtr a,IntPtr b);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr window,int command);
    [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr window);
    [DllImport("user32.dll")] static extern bool AttachThreadInput(uint from,uint to,bool attach);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [StructLayout(LayoutKind.Sequential)] struct Point { public int x,y; }
    [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr window,ref Point point);
    [DllImport("user32.dll")] static extern bool SetCursorPos(int x,int y);
    [DllImport("user32.dll")] static extern void mouse_event(uint flags,uint x,uint y,uint data,UIntPtr extra);
    public static IntPtr Find(int process) {
        IntPtr found=IntPtr.Zero;
        EnumWindows(delegate(IntPtr window,IntPtr state) {
            uint owner;GetWindowThreadProcessId(window,out owner);
            var name=new StringBuilder(128);GetClassName(window,name,128);
            if(owner==process && name.ToString()=="SDL_app") { found=window;return false; }
            return true;
        },IntPtr.Zero);
        return found;
    }
    public static void Key(IntPtr window,int code,bool down) {
        long bits=1L|((long)MapVirtualKey((uint)code,0)<<16);
        if(code>=37 && code<=40)bits|=0x01000000L;
        if(!down)bits|=0xC0000000L;
        if(!PostMessage(window,down?0x100u:0x101u,new IntPtr(code),new IntPtr(bits)))
            throw new InvalidOperationException("Keyboard input post failed.");
    }
    public static void LeftMouse(IntPtr window,bool down,int x,int y) {
        ShowWindow(window,9);
        var foreground=GetForegroundWindow();uint owner;
        uint other=GetWindowThreadProcessId(foreground,out owner),current=GetCurrentThreadId();
        bool attached=other!=0&&other!=current&&AttachThreadInput(current,other,true);
        try { BringWindowToTop(window);SetForegroundWindow(window); }
        finally { if(attached)AttachThreadInput(current,other,false); }
        if(GetForegroundWindow()!=window) {
            // A release is cleanup, never a new click in another window.
            if(!down)mouse_event(4u,0,0,0,UIntPtr.Zero);
            throw new InvalidOperationException("Target SDL window is not foreground; mouse input stopped. target="+window+" foreground="+GetForegroundWindow());
        }
        if(down) {
            var point=new Point{x=x,y=y};
            if(!ClientToScreen(window,ref point)||!SetCursorPos(point.x,point.y))
                throw new InvalidOperationException("Mouse positioning failed.");
        }
        mouse_event(down?2u:4u,0,0,0,UIntPtr.Zero);
    }
}
'@
}
if($CheckOnly) {
    Write-Host '[PLAYER-UI] Script and Win32 helper loaded; GPU was not started.'
    exit 0
}
if(-not (Test-Path -LiteralPath $Exe -PathType Leaf)){throw 'Stage windows/NativeCodex.ps1 build first.'}
if(Get-Process rasterfall -ErrorAction SilentlyContinue){throw 'A Rasterfall process is already running; native acceptance must be serial.'}
$RunId=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
$Out=Join-Path ([IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))) $RunId
New-Item -ItemType Directory -Path $Out | Out-Null
$EnvKeys=@('RF_UI_AUDIT','RF_UI_STORY','RF_UI_NO_SAVE','RF_UI_MODE','RF_UI_CAPTURE_DIRECTORY',
    'RF_UI_SAVE_PATH','RF_STORY_SAVE_PATH','RF_WEAVER_VIEW','RF_WEAVER_BLUEPRINT',
    'RF_WEAVER_TIME_MS','RF_WEAVER_PAUSE_MS','RF_WEAVER_RESUME_MS','RF_WEAVER_MENU',
    'RF_WEAVER_INTERACTION_AUDIT','RF_WEAVER_CAPTURE_DIRECTORY','RF_WEAVER_CAPTURE_EVERY','RF_SCENE_PERF_FRAMES')
$Saved=@{}
foreach($Name in $EnvKeys){$Saved[$Name]=[Environment]::GetEnvironmentVariable($Name,'Process')}
$SavedPath=$env:PATH
$Results=[Collections.Generic.List[object]]::new()
$script:UiRun=$null

function Assert-Ui([bool]$Condition,[string]$Message) {
    if(-not $Condition){throw $Message}
}
function Quote-Native([string]$Value) {
    $Value=[regex]::Replace($Value,'(\\*)"','$1$1\"')
    $Value=[regex]::Replace($Value,'(\\+)$','$1$1')
    return '"'+$Value+'"'
}
function Read-Ui {
    $c=$script:UiRun
    if(-not $c.reader -and (Test-Path -LiteralPath $c.stdout)) {
        $stream=[IO.FileStream]::new($c.stdout,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
        $c.reader=[IO.StreamReader]::new($stream,[Text.Encoding]::UTF8)
    }
    if($c.reader) {
        $c.pending+=$c.reader.ReadToEnd()
        $end=$c.pending.LastIndexOf("`n")
        if($end -ge 0) {
            $complete=$c.pending.Substring(0,$end);$c.pending=$c.pending.Substring($end+1)
            foreach($line in ($complete -split "`n")) {
                if($line.TrimEnd("`r") -match '^PLAYER-UI-AUDIT (\{.*\})$') {
                    $s=$Matches[1]|ConvertFrom-Json
                    foreach($field in @('frame','ui_mode','terminal','focus','rts','x','z','fire_seq','slot',
                        'story','node','sr','nr','link','collapsed','queue','progress_a','progress_b','hold',
                        'camera_generation','camera_valid','weaver_panel','weaver_focus','selected','phase',
                        'progress','serial','collected','power','command_code','video_frames','normal_sim','driver')) {
                        if(-not $s.PSObject.Properties[$field]){throw "Audit lacks $field; refresh the staged executable."}
                    }
                    Assert-Ui ($s.normal_sim -eq 1 -and $s.driver -eq 0) 'A diagnostic task driver contaminated native input acceptance.'
                    $c.latest=$s;$c.samples.Add($s)
                } elseif($line.TrimEnd("`r") -match '^PLAYER-UI-DEVICE (\{.*\})$') {
                    $d=$Matches[1]|ConvertFrom-Json
                    if($c.latest -and $c.latest.frame -eq $d.frame) {
                        foreach($property in $d.PSObject.Properties) {
                            if($property.Name -ne 'frame'){$c.latest|Add-Member -NotePropertyName $property.Name -NotePropertyValue $property.Value -Force}
                        }
                    }
                } elseif($line.TrimEnd("`r") -match '^PLAYER-UI-COMMAND status=(-?\d+) lines=(\d+) command=(.*)$') {
                    $c.commands.Add([pscustomobject]@{status=[int]$Matches[1];lines=[int]$Matches[2];command=$Matches[3];frame=$c.latest.frame})
                }
            }
        }
    }
    if([DateTime]::UtcNow -gt $c.deadline){throw 'Native player UI acceptance exceeded its overall deadline.'}
    $c.process.Refresh()
    if($c.process.HasExited){throw "Process exited before input acceptance completed: $($c.process.ExitCode)"}
}
function Wait-Ui([string]$Name,[scriptblock]$Condition,[int]$WaitMs=20000,[long]$AfterFrame=-1) {
    $until=[DateTime]::UtcNow.AddMilliseconds($WaitMs)
    while([DateTime]::UtcNow -lt $until) {
        Read-Ui
        $sample=$script:UiRun.latest
        if($sample -and $sample.frame -gt $AfterFrame -and (& $Condition $sample)) {
            Write-Host "[PLAYER-UI] $($script:UiRun.name) $Name frame=$($sample.frame)"
            return $sample
        }
        Start-Sleep -Milliseconds 40
    }
    throw "$Name timed out. Last audit: $($script:UiRun.latest|ConvertTo-Json -Compress)"
}
function Key-Ui([int]$Code,[string]$Name,[int]$Hold=$KeyHoldMs) {
    $c=$script:UiRun
    $c.actions.Add([ordered]@{kind='key';name=$Name;code=$Code;hold_ms=$Hold;
        frame=$c.latest.frame;utc=[DateTime]::UtcNow.ToString('o')})
    [PlayerUiWindow]::Key($c.window,$Code,$true)
    try { Start-Sleep -Milliseconds $Hold } finally { [PlayerUiWindow]::Key($c.window,$Code,$false) }
    Start-Sleep -Milliseconds 80
}
function Mouse-Ui([bool]$Down,[int]$X=2,[int]$Y=2) {
    $c=$script:UiRun
    $c.actions.Add([ordered]@{kind='mouse';button='left';down=$Down;x=$X;y=$Y;
        frame=$c.latest.frame;utc=[DateTime]::UtcNow.ToString('o')})
    [PlayerUiWindow]::LeftMouse($c.window,$Down,$X,$Y)
}
function Command-Ui([string]$Text) {
    $c=$script:UiRun
    Assert-Ui ($c.latest.terminal -eq 1) 'Text commands require an open terminal.'
    $c.actions.Add([ordered]@{kind='text';text=$Text;frame=$c.latest.frame;utc=[DateTime]::UtcNow.ToString('o')})
    # The current terminal consumes legacy key edges, one character per
    # frame. WM_CHAR/SDL_TEXTINPUT does not drive that code path.
    foreach($character in $Text.ToCharArray()) {
        $code=[int][char]::ToUpperInvariant($character)
        if(-not (($code -ge 65 -and $code -le 90) -or ($code -ge 48 -and $code -le 57) -or $code -eq 32)) {
            throw "Unsupported native command character: $character"
        }
        [PlayerUiWindow]::Key($c.window,$code,$true)
        try { Start-Sleep -Milliseconds 65 } finally { [PlayerUiWindow]::Key($c.window,$code,$false) }
        Start-Sleep -Milliseconds 45
    }
    Key-Ui 13 "Enter execute: $Text"
}
function Command-CheckedUi([string]$Text,[int]$Status=0) {
    $priorCommandCount=$script:UiRun.commands.Count
    Command-Ui $Text
    [void](Wait-Ui "command result $Text" {param($s) $script:UiRun.commands.Count -gt $priorCommandCount})
    $result=$script:UiRun.commands[$script:UiRun.commands.Count-1]
    Assert-Ui ($result.command -eq $Text -and $result.status -eq $Status -and $result.lines -gt 0) "Unexpected command result: $($result|ConvertTo-Json -Compress)"
}
function Screen-Ui([string]$Name) {
    if($NoScreenshots){return}
    $c=$script:UiRun;$c.captureId++
    $pending=Join-Path $c.captureDirectory 'capture.pending'
    [IO.File]::WriteAllText($pending,[string]$c.captureId,$Utf8)
    Move-Item -LiteralPath $pending -Destination (Join-Path $c.captureDirectory 'capture.request') -Force
    $ack=Join-Path $c.captureDirectory 'capture.complete';$until=[DateTime]::UtcNow.AddSeconds(20);$frame=0
    while([DateTime]::UtcNow -lt $until) {
        Read-Ui
        if(Test-Path -LiteralPath $ack) {
            $value=[IO.File]::ReadAllText($ack,[Text.Encoding]::UTF8)
            if($value -match '^(\d+) (\d+)' -and [int]$Matches[1] -eq $c.captureId){$frame=[int]$Matches[2];break}
        }
        Start-Sleep -Milliseconds 40
    }
    Assert-Ui ($frame -gt 0) 'Explicit native capture did not complete.'
    $path=Join-Path $c.captureDirectory ('frame-{0:D6}.scene.ppm' -f $c.captureId)
    Assert-Ui ((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).Length -gt 1000) 'Native capture is missing or empty.'
    $c.screens.Add([ordered]@{name=$Name;path=$path;observed_frame=$frame})
}
function Terminal-FocusCheck {
    $before=$script:UiRun.latest
    Key-Ui 192 'Backquote open terminal'
    $opened=Wait-Ui 'terminal opened' {param($s) $s.terminal -eq 1} 20000 $before.frame
    Key-Ui 87 'W held while terminal owns text input' 500
    Key-Ui 13 'Enter submit input without firing'
    $after=Wait-Ui 'terminal input observed' {param($s) $s.terminal -eq 1} 20000 ($opened.frame+10)
    Assert-Ui ($after.x -eq $opened.x -and $after.z -eq $opened.z -and $after.fire_seq -eq $opened.fire_seq) 'Terminal text leaked into player movement or weapon fire.'
    Command-Ui 'weaver status'
    $queried=Wait-Ui 'terminal read-only query' {param($s) $s.terminal -eq 1} 20000 $after.frame
    Assert-Ui ($queried.x -eq $opened.x -and $queried.z -eq $opened.z -and $queried.fire_seq -eq $opened.fire_seq) 'Terminal Enter/query changed gameplay input.'
    Screen-Ui 'terminal-focus'
    Key-Ui 27 'Escape return from terminal'
    $closed=Wait-Ui 'terminal closed' {param($s) $s.terminal -eq 0} 20000 $queried.frame
    Assert-Ui ($closed.x -eq $opened.x -and $closed.z -eq $opened.z -and $closed.fire_seq -eq $opened.fire_seq) 'A held terminal key leaked after close.'
}
function Outpost-Route {
    $first=Wait-Ui 'live outpost introduction' {param($s) $s.story -eq 101 -and $s.link -eq 2 -and $s.video_frames -gt 0}
    Assert-Ui ($first.ui_mode -eq 0 -and $first.camera_valid -eq 1 -and $first.hold -ne 0) 'Default player UI or NULL entity control is unavailable.'
    Screen-Ui 'fps-comms'
    foreach($mode in @(1,2,0)) {
        $previous=$script:UiRun.latest
        Key-Ui 114 "F3 switch to mode $mode"
        $sample=Wait-Ui "mode $mode" {param($s) $s.ui_mode -eq $mode} 20000 $previous.frame
        Assert-Ui ($sample.story -eq $first.story -and $sample.node -eq $first.node -and $sample.sr -eq $first.sr) 'Mode switch changed the current conversation.'
    }
    Terminal-FocusCheck
    $previous=$script:UiRun.latest
    Key-Ui 77 'M enter RTS'
    $rts=Wait-Ui 'RTS retains conversation' {param($s) $s.rts -eq 1} 20000 $previous.frame
    Assert-Ui ($rts.node -eq $first.node -and $rts.sr -eq $first.sr) 'RTS reset a dialogue node.'
    Screen-Ui 'rts-comms'
    Key-Ui 77 'M return FPS'
    $fps=Wait-Ui 'FPS restored' {param($s) $s.rts -eq 0} 20000 $rts.frame
    Key-Ui 72 'H collapse communications'
    $hidden=Wait-Ui 'communications collapsed' {param($s) $s.collapsed -eq 1} 20000 $fps.frame
    $settled=Wait-Ui 'hidden video settles' {param($s) $s.collapsed -eq 1} 20000 ($hidden.frame+30)
    $quiet=Wait-Ui 'hidden video remains stopped' {param($s) $s.collapsed -eq 1} 20000 ($settled.frame+30)
    Assert-Ui ($quiet.video_frames -eq $settled.video_frames -and $quiet.node -eq $first.node) 'Hidden communications continued video rendering or advanced the node.'
    Key-Ui 72 'H restore communications'
    $restored=Wait-Ui 'restored real camera refresh' {param($s) $s.collapsed -eq 0 -and $s.video_frames -gt $quiet.video_frames} 20000 $quiet.frame
    Key-Ui 90 'Z answer first branch'
    $answered=Wait-Ui 'answer advances one node' {param($s) $s.node -eq 1011} 20000 $restored.frame
    Assert-Ui ($answered.slot -eq $first.slot -and $answered.fire_seq -eq $first.fire_seq) 'Dialogue action changed weapon slot or fired.'
    Key-Ui 192 'Backquote open terminal for stale answer'
    [void](Wait-Ui 'terminal ready for stale request' {param($s) $s.terminal -eq 1} 20000 $answered.frame)
    Command-Ui "comms answer 1 $($first.sr) $($first.nr)"
    $stale=Wait-Ui 'stale answer rejected' {param($s) $s.command_code -eq 4} 20000 $answered.frame
    Assert-Ui ($stale.node -eq $answered.node -and $stale.nr -eq $answered.nr) 'Stale answer advanced the dialogue.'
    Key-Ui 27 'Escape close terminal'
    [void](Wait-Ui 'terminal closed before confirm' {param($s) $s.terminal -eq 0} 20000 $stale.frame)
    Key-Ui 90 'Z confirm final NULL line'
    $done=Wait-Ui 'outpost call completes and releases control' {param($s) $s.story -eq 0 -and $s.progress_a -eq 4 -and $s.hold -eq 0} 20000 $stale.frame
    Screen-Ui 'fps-after-comms'
    Key-Ui 114 'F3 persist terminal presentation for process restart'
    [void](Wait-Ui 'terminal presentation selected before shutdown' {param($s) $s.ui_mode -eq 1 -and $s.terminal -eq 1} 20000 $done.frame)
}
function Resume-Route {
    $loaded=Wait-Ui 'saved presentation and completed call restored' {param($s) $s.ui_mode -eq 1 -and $s.terminal -eq 1 -and $s.progress_a -eq 4 -and $s.story -eq 0 -and $s.hold -eq 0}
    Assert-Ui ($loaded.task_id -eq 201 -and $loaded.task_state -eq 1) 'The persisted exploration task was not restored.'
    Key-Ui 27 'Escape inspect restored world'
    $closed=Wait-Ui 'restored terminal closes without replay' {param($s) $s.terminal -eq 0 -and $s.progress_a -eq 4 -and $s.story -eq 0} 20000 $loaded.frame
    $settled=Wait-Ui 'completed outpost trigger stays consumed' {param($s) $s.story -eq 0 -and $s.progress_a -eq 4 -and $s.hold -eq 0} 20000 ($closed.frame+90)
    Assert-Ui ($settled.queue -eq 0 -and $settled.link -eq 0 -and $settled.video_frames -eq 0) 'Reload replayed a completed communication or refreshed its hidden video.'
    Screen-Ui 'persisted-outpost-restored'
}
function Weaver-FocusStart {
    for($step=0;$step -lt 32;$step++) {
        $priorFocus=$script:UiRun.latest.weaver_focus
        $priorFrame=$script:UiRun.latest.frame
        if($priorFocus -eq 1){return}
        Key-Ui 40 'Down navigate real device actions'
        [void](Wait-Ui 'device focus advanced' {param($s) $s.weaver_panel -eq 1 -and $s.weaver_focus -ne $priorFocus} 20000 $priorFrame)
    }
    throw 'The device start/collect action was not keyboard reachable.'
}
function Weaver-Route {
    $idle=Wait-Ui 'weaver scene ready' {param($s) $s.phase -eq 0 -and $s.ui_mode -eq 0}
    Terminal-FocusCheck
    Key-Ui 192 'Backquote inspect and select real blueprint'
    [void](Wait-Ui 'blueprint terminal opened' {param($s) $s.terminal -eq 1} 20000 $script:UiRun.latest.frame)
    Command-Ui 'weaver select pistol'
    $selected=Wait-Ui 'real Pistol blueprint selected' {param($s) $s.selected -eq 1 -and $s.command_code -eq 0}
    Key-Ui 27 'Escape close blueprint terminal'
    [void](Wait-Ui 'blueprint terminal closed' {param($s) $s.terminal -eq 0} 20000 $selected.frame)
    Key-Ui 69 'E open player device panel'
    $opened=Wait-Ui 'new device panel opened' {param($s) $s.weaver_panel -eq 1}
    Screen-Ui 'weaver-player-panel'
    Weaver-FocusStart
    $before=$script:UiRun.latest
    Key-Ui 13 'Enter GUI start manufacturing'
    $started=Wait-Ui 'one real job started' {param($s) $s.serial -eq 1 -and $s.phase -ge 1 -and $s.phase -le 2} 20000 $before.frame
    Assert-Ui ($started.fire_seq -eq $idle.fire_seq) 'Device confirm leaked into weapon fire.'
    Key-Ui 27 'Escape close device while work continues'
    [void](Wait-Ui 'device closed' {param($s) $s.weaver_panel -eq 0} 20000 $started.frame)
    Key-Ui 192 'Backquote manage the GUI job through terminal'
    [void](Wait-Ui 'job terminal opened' {param($s) $s.terminal -eq 1} 20000 $script:UiRun.latest.frame)
    Command-Ui 'weaver power 0'
    $off=Wait-Ui 'terminal cuts real power' {param($s) $s.power -eq 0 -and $s.serial -eq 1}
    Command-Ui 'weaver status'
    Screen-Ui 'weaver-terminal-paused'
    Key-Ui 27 'Escape observe paused device'
    [void](Wait-Ui 'paused terminal closed' {param($s) $s.terminal -eq 0} 20000 $off.frame)
    $paused=Wait-Ui 'paused task remains same' {param($s) $s.power -eq 0} 20000 ($off.frame+30)
    Assert-Ui ([Math]::Abs($paused.progress-$off.progress) -le 0.000001 -and $paused.serial -eq 1) 'Power-off progressed or restarted the task.'
    Key-Ui 192 'Backquote restore power'
    [void](Wait-Ui 'restore terminal opened' {param($s) $s.terminal -eq 1} 20000 $paused.frame)
    Command-Ui 'weaver power 1'
    $on=Wait-Ui 'existing job resumes' {param($s) $s.power -eq 1 -and $s.serial -eq 1}
    Key-Ui 27 'Escape wait for manufactured product'
    $ready=Wait-Ui 'one finished product ready' {param($s) $s.phase -eq 4 -and $s.terminal -eq 0} 90000 $on.frame
    Key-Ui 69 'E open device for GUI collection'
    [void](Wait-Ui 'ready panel opened' {param($s) $s.weaver_panel -eq 1} 20000 $ready.frame)
    Weaver-FocusStart
    $before=$script:UiRun.latest
    Key-Ui 13 'Enter request product collection'
    $claim=Wait-Ui 'collection or explicit replacement confirmation' {param($s) $s.collected -eq 1 -or $s.command_code -eq 8} 20000 $before.frame
    if($claim.collected -eq 0) {
        Screen-Ui 'weaver-replace-confirmation'
        Key-Ui 13 'Enter confirm named weapon replacement'
        $claim=Wait-Ui 'one product collected' {param($s) $s.collected -eq 1 -and $s.phase -eq 0} 20000 $claim.frame
    }
    Assert-Ui ($claim.serial -eq 1 -and $claim.collected -eq 1 -and $claim.fire_seq -eq $idle.fire_seq) 'GUI collection duplicated a job or leaked fire input.'
    Screen-Ui 'weaver-collected'
}
function Held-CloseCheck([string]$Context) {
    $before=$script:UiRun.latest
    $c=$script:UiRun
    $c.actions.Add([ordered]@{kind='key-hold';name="W held across $Context close";code=87;frame=$before.frame})
    [PlayerUiWindow]::Key($c.window,87,$true)
    try {
        Mouse-Ui $true
        $held=Wait-Ui "$Context consumes held movement and mouse" {param($s) $true} 20000 ($before.frame+30)
        Assert-Ui ($held.x -eq $before.x -and $held.z -eq $before.z -and $held.fire_seq -eq $before.fire_seq) "$Context input reached the player while open."
        if($Context -eq 'terminal'){Key-Ui 8 'Backspace clear held W text before closing'}
        Key-Ui 27 "Escape close $Context with W and mouse still held"
        $closed=Wait-Ui "$Context closes" {param($s) $s.terminal -eq 0 -and $s.focus -eq 0 -and $s.weaver_panel -eq 0} 20000 $held.frame
        $blocked=Wait-Ui "held input remains consumed after $Context close" {param($s) $true} 20000 ($closed.frame+30)
        Assert-Ui ($blocked.x -eq $before.x -and $blocked.z -eq $before.z -and $blocked.fire_seq -eq $before.fire_seq) "Held input leaked after $Context closed."
    } finally {
        [PlayerUiWindow]::Key($c.window,87,$false)
        Mouse-Ui $false
    }
    $released=Wait-Ui "$Context input release settles" {param($s) $true} 20000 ($script:UiRun.latest.frame+10)
    Assert-Ui ($released.x -eq $before.x -and $released.z -eq $before.z -and $released.fire_seq -eq $before.fire_seq) "$Context release produced movement or a shot."
}
function Video-CycleCheck {
    $c=$script:UiRun;$baseline=0L;$peak=0L
    for($cycle=0;$cycle -lt 12;$cycle++) {
        $prior=$c.latest
        Key-Ui 72 'H collapse video for repeated lifecycle check'
        $hidden=Wait-Ui 'cycle video collapsed' {param($s) $s.collapsed -eq 1} 20000 $prior.frame
        $quiet=Wait-Ui 'cycle hidden video settles' {param($s) $s.collapsed -eq 1} 20000 ($hidden.frame+10)
        $still=Wait-Ui 'cycle hidden video stays stopped' {param($s) $s.collapsed -eq 1} 20000 ($quiet.frame+10)
        Assert-Ui ($still.video_frames -eq $quiet.video_frames) 'A hidden video kept refreshing during repeated lifecycle checks.'
        Key-Ui 72 'H restore video for repeated lifecycle check'
        [void](Wait-Ui 'cycle visible video refreshes' {param($s) $s.collapsed -eq 0 -and $s.video_frames -gt $still.video_frames} 20000 $still.frame)
        $c.process.Refresh();$bytes=$c.process.PrivateMemorySize64
        if($cycle -eq 1){$baseline=$bytes;$peak=$bytes}
        if($cycle -ge 2 -and $bytes -gt $peak){$peak=$bytes}
    }
    $c.process.Refresh();$end=$c.process.PrivateMemorySize64
    $c.metrics.video_cycles=[ordered]@{warmup_cycles=2;measured_cycles=10;private_bytes_before=$baseline;
        private_bytes_after=$end;private_bytes_peak=$peak;private_bytes_delta=($end-$baseline);
        memory_scope='Native process private memory only; not a VRAM allocation measurement.'}
    Write-Host "[PLAYER-UI] video cycles private bytes before=$baseline after=$end delta=$($end-$baseline)"
}
function Boundary-OutpostRoute {
    $first=Wait-Ui 'boundary introduction ready' {param($s) $s.story -eq 101 -and $s.link -eq 2}
    Key-Ui 192 'Backquote open terminal for held-input boundary'
    [void](Wait-Ui 'held-input terminal opened' {param($s) $s.terminal -eq 1} 20000 $first.frame)
    Held-CloseCheck 'terminal'
    Key-Ui 67 'C enter deliberate communications pointer focus'
    [void](Wait-Ui 'communications focus opened' {param($s) $s.focus -eq 1} 20000 $script:UiRun.latest.frame)
    Held-CloseCheck 'communications'
    Video-CycleCheck
    $before=$script:UiRun.latest
    Key-Ui 83 'S move normally with unanswered communication' 300
    $moved=Wait-Ui 'communication permits normal movement' {param($s) $s.x -ne $before.x -or $s.z -ne $before.z} 20000 $before.frame
    Assert-Ui ($moved.story -eq $first.story -and $moved.node -eq $first.node -and $moved.sr -eq $first.sr) 'Ordinary movement answered or reset communication.'
    try {
        Mouse-Ui $true 640 360
        $fired=Wait-Ui 'communication permits real weapon fire' {param($s) $s.fire_seq -gt $moved.fire_seq -and $s.combat -eq 1} 20000 $moved.frame
        Assert-Ui ($fired.story -eq $first.story -and $fired.node -eq $first.node -and $fired.focus -eq 0) 'Ordinary combat focus changed the conversation.'
        Screen-Ui 'comms-combat-compact'
    } finally { Mouse-Ui $false 640 360 }
    Key-Ui 192 'Backquote query device services'
    $terminal=Wait-Ui 'device query terminal opened' {param($s) $s.terminal -eq 1} 20000 $script:UiRun.latest.frame
    foreach($command in @('render status','table status','table maps','devices status')) {
        Command-CheckedUi $command
        $query=Wait-Ui "read-only $command observed" {param($s) $s.terminal -eq 1} 20000 ($script:UiRun.latest.frame+10)
        Assert-Ui ($query.x -eq $terminal.x -and $query.z -eq $terminal.z -and $query.story -eq $terminal.story -and $query.nr -eq $terminal.nr) "$command changed gameplay or conversation state."
        Assert-Ui ($query.render_mask -eq $terminal.render_mask -and $query.table_selected -eq $terminal.table_selected -and $query.table_pending -eq $terminal.table_pending) "$command mutated a read-only device query."
        Screen-Ui ($command -replace ' ','-')
    }
    Command-CheckedUi 'table deploy campaign' -1
    $range=Wait-Ui 'table deployment rejects out-of-range player' {param($s) $s.device_code -eq 3} 20000 $terminal.frame
    Assert-Ui ($range.table_pending -eq -1 -and $range.x -eq $terminal.x -and $range.z -eq $terminal.z) 'Rejected table deployment changed the world.'
    Command-CheckedUi 'comms reset 0' -1
    $denied=Wait-Ui 'player terminal rejects developer reset' {param($s) $s.command_code -eq 2} 20000 $terminal.frame
    Assert-Ui ($denied.story -eq $first.story -and $denied.node -eq $first.node -and $denied.nr -eq $first.nr) 'A denied developer reset changed the conversation.'
    Screen-Ui 'terminal-permission-denied'
}
function Boundary-WeaverRoute {
    $first=Wait-Ui 'boundary weaver scene ready' {param($s) $s.phase -eq 0 -and $s.ui_mode -eq 0}
    Key-Ui 69 'E open device for held-input boundary'
    [void](Wait-Ui 'boundary device panel opened' {param($s) $s.weaver_panel -eq 1} 20000 $first.frame)
    Held-CloseCheck 'device'
    Assert-Ui ($script:UiRun.latest.serial -eq 0) 'An outside-panel pointer press started a manufacturing job.'
    Screen-Ui 'device-held-input-released'
}
function Boundary-RenderRoute {
    $first=Wait-Ui 'render boundary world ready' {param($s) $s.ui_mode -eq 0 -and $s.story -eq 101}
    Key-Ui 112 'F1 open player rendering device'
    [void](Wait-Ui 'render panel input settles' {param($s) $true} 20000 ($first.frame+10))
    foreach($step in @(1,2,3)){Key-Ui 40 'Down select flashlight feature'}
    Key-Ui 13 'Enter toggle real rendering feature'
    $changed=Wait-Ui 'GUI toggles shared flashlight state' {param($s) $s.render_mask -eq ($first.render_mask -bxor 8)} 20000 $first.frame
    Assert-Ui ($changed.fire_seq -eq $first.fire_seq -and $changed.x -eq $first.x -and $changed.z -eq $first.z) 'Render panel input leaked to gameplay.'
    Screen-Ui 'render-device-feature-toggled'
    Held-CloseCheck 'render'
    Screen-Ui 'render-held-input-closed'
    Key-Ui 192 'Backquote inspect GUI rendering state'
    [void](Wait-Ui 'render query terminal opens' {param($s) $s.terminal -eq 1} 20000 $script:UiRun.latest.frame)
    Command-CheckedUi 'render status'
    $query=Wait-Ui 'terminal sees GUI rendering value' {param($s) $s.render_mask -eq $changed.render_mask} 20000 $changed.frame
    Assert-Ui ($query.render_mask -eq $changed.render_mask -and $query.fire_seq -eq $first.fire_seq) 'Render status changed the GUI setting or fired.'
    Screen-Ui 'render-terminal-shared-state'
}
function Run-Ui([string]$Name,[string]$SaveDirectory=$Out) {
    $stdout=Join-Path $Out "$Name.stdout.log";$stderr=Join-Path $Out "$Name.stderr.log"
    $saveName=if($Name -eq 'Resume'){'Outpost'}else{$Name}
    $uiPath=Join-Path $SaveDirectory "$saveName-ui.cfg"
    $storyPath=Join-Path $SaveDirectory "$saveName-story.bin"
    if($Name -eq 'Resume') {
        Assert-Ui ((Test-Path -LiteralPath $uiPath) -and (Test-Path -LiteralPath $storyPath)) 'Resume requires completed Outpost-ui.cfg and Outpost-story.bin from an Outpost route.'
    }
    $argv=@('--renderer','gpu-scene','--skip-boot','--map','rasterfall/assets/maps/outpost.map','--frame-audit')
    if($Name -eq 'Weaver' -or $Name -eq 'BoundaryWeaver'){$argv+=@('--gpu-normal-scene','mesh-weaver','0')}
    $c=[pscustomobject]@{name=$Name;stdout=$stdout;stderr=$stderr;process=$null;reader=$null;pending='';latest=$null;
        window=[IntPtr]::Zero;deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds);captureId=0;
        captureDirectory=(Join-Path $Out "$Name-frames");samples=[Collections.Generic.List[object]]::new();
        actions=[Collections.Generic.List[object]]::new();screens=[Collections.Generic.List[object]]::new();
        commands=[Collections.Generic.List[object]]::new();metrics=[ordered]@{}}
    $script:UiRun=$c
    $record=[ordered]@{status='started';stage=$Name;arguments=$argv;stdout=$stdout;stderr=$stderr;
        started_utc=[DateTime]::UtcNow.ToString('o');exe_sha256=(Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash;
        ui_save_path=$uiPath;story_save_path=$storyPath;
        input_path='Win32 key messages and guarded native mouse events -> SDL input -> normal UI command routing';
        simulation='Realtime fixed-step scheduling; observation audits and explicit screenshot readback only.'}
    try {
        $env:RF_UI_AUDIT='1';$env:RF_UI_STORY='1';$env:RF_UI_NO_SAVE=$null
        $env:RF_UI_SAVE_PATH=$uiPath
        $env:RF_STORY_SAVE_PATH=$storyPath
        $env:RF_WEAVER_VIEW=if($Name -eq 'Weaver' -or $Name -eq 'BoundaryWeaver'){'interaction'}else{$null}
        if(-not $NoScreenshots){New-Item -ItemType Directory -Path $c.captureDirectory | Out-Null;$env:RF_UI_CAPTURE_DIRECTORY=$c.captureDirectory}
        else{$env:RF_UI_CAPTURE_DIRECTORY=$null}
        $quoted=($argv|ForEach-Object {Quote-Native $_}) -join ' '
        $c.process=Start-Process -FilePath $Exe -WorkingDirectory $Package -ArgumentList $quoted -WindowStyle Hidden `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
        $handle=$c.process.Handle;$until=[DateTime]::UtcNow.AddSeconds(30)
        while($c.window -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $until) {
            $c.window=[PlayerUiWindow]::Find($c.process.Id)
            if($c.process.WaitForExit(40)){throw "Native startup exited: $($c.process.ExitCode)"}
        }
        Assert-Ui ($c.window -ne [IntPtr]::Zero) 'No native SDL window appeared.'
        [void][PlayerUiWindow]::SetForegroundWindow($c.window)
        if($Name -eq 'Outpost'){Outpost-Route}
        elseif($Name -eq 'Resume'){Resume-Route}
        elseif($Name -eq 'BoundaryOutpost'){Boundary-OutpostRoute}
        elseif($Name -eq 'BoundaryWeaver'){Boundary-WeaverRoute}
        elseif($Name -eq 'BoundaryRender'){Boundary-RenderRoute}
        else{Weaver-Route}
        $record.final_state=$c.latest
        [void][PlayerUiWindow]::PostMessage($c.window,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
        if(-not $c.process.WaitForExit(30000)){throw 'Native shutdown did not finish after WM_CLOSE.'}
        $c.process.WaitForExit();$c.process.Refresh();$record.exit_code=$c.process.ExitCode
        Assert-Ui ($record.exit_code -eq 0) "Native exit $($record.exit_code)."
        $log=Get-Content -LiteralPath $stdout,$stderr -Encoding UTF8|Out-String
        Assert-Ui ($log -match 'SCENE-NATIVE.*bridges=0.*mixed_execute=0') 'Missing native rendering evidence.'
        Assert-Ui ($log -notmatch 'Validation Error|SYNC-HAZARD|VUID-|preparation/submit failed') 'Native log contains a render or validation failure.'
        Assert-Ui ($log -notmatch 'SCENE-NATIVE[^\r\n]*(bridges|mixed_execute)=[1-9]') 'Native UI used a mixed rendering fallback.'
        $record.status='passed'
        Write-Host "[PLAYER-UI] $Name PASS; evidence $Out"
    } catch {
        $record.status='failed';$record.error=$_.Exception.Message
        if($c.window -ne [IntPtr]::Zero){try{Screen-Ui 'failure'}catch{}}
        throw
    } finally {
        if($c.reader){$c.reader.Dispose()}
        if($c.process) {
            $c.process.Refresh()
            if(-not $c.process.HasExited) {
                if($c.window -ne [IntPtr]::Zero){[void][PlayerUiWindow]::PostMessage($c.window,0x10,[IntPtr]::Zero,[IntPtr]::Zero)}
                if(-not $c.process.WaitForExit(10000)){Stop-Process -Id $c.process.Id -Force;$c.process.WaitForExit()}
            }
            $c.process.Refresh();$record.exit_code=$c.process.ExitCode
        }
        $record.finished_utc=[DateTime]::UtcNow.ToString('o');$record.actions=$c.actions.ToArray()
        $record.samples=$c.samples.ToArray();$record.screenshots=$c.screens.ToArray()
        $record.commands=$c.commands.ToArray()
        $record.metrics=$c.metrics
        [IO.File]::WriteAllText((Join-Path $Out "$Name.json"),($record|ConvertTo-Json -Depth 9),$Utf8)
        $Results.Add([ordered]@{stage=$Name;status=$record.status;exit_code=$record.exit_code;evidence="$Name.json"})
    }
}
try {
    Remove-Item Env:Path -ErrorAction SilentlyContinue
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:PATH='C:\msys64\mingw64\bin;C:\msys64\usr\bin;'+$SavedPath
    foreach($Name in $EnvKeys){[Environment]::SetEnvironmentVariable($Name,$null,'Process')}
    if($Stage -eq 'All' -or $Stage -eq 'Outpost'){Run-Ui 'Outpost'}
    if($Stage -eq 'All'){Run-Ui 'Resume'}
    if($Stage -eq 'Resume') {
        Assert-Ui (-not [string]::IsNullOrWhiteSpace($ResumeDirectory)) 'Stage Resume requires -ResumeDirectory pointing to a completed Outpost evidence directory.'
        Run-Ui 'Resume' ([IO.Path]::GetFullPath((Join-Path $Root $ResumeDirectory)))
    }
    if($Stage -eq 'All' -or $Stage -eq 'Weaver'){Run-Ui 'Weaver'}
    if($Stage -eq 'All' -or $Stage -eq 'Boundaries'){
        Run-Ui 'BoundaryOutpost'
        Run-Ui 'BoundaryWeaver'
    }
    if($Stage -eq 'All' -or $Stage -eq 'Boundaries' -or $Stage -eq 'RenderBoundaries'){Run-Ui 'BoundaryRender'}
} finally {
    foreach($Name in $EnvKeys){[Environment]::SetEnvironmentVariable($Name,$Saved[$Name],'Process')}
    Remove-Item Env:Path -ErrorAction SilentlyContinue
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:PATH=$SavedPath
    [IO.File]::WriteAllText((Join-Path $Out 'summary.json'),($Results.ToArray()|ConvertTo-Json -Depth 6),$Utf8)
}
