<#
.SYNOPSIS
Capture the real player UI at 720p, 1080p and a narrow window with larger text.
.DESCRIPTION
Uses native Win32 resizing and input, private settings/story files and explicit
GPU captures. Run serially after windows/NativeCodex.ps1 build has staged assets.
The output records requested and actual client dimensions and process exits.
#>
[CmdletBinding()]
param(
    [string]$OutputDirectory='tmp/player-ui/layout',
    [ValidateSet('All','720p','1080p','Narrow')][string]$Case='All',
    [switch]$CheckOnly
)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Exe=Join-Path $Package 'rasterfall.exe'
$Utf8=[Text.UTF8Encoding]::new($false)
if(-not ('PlayerUiLayoutWindow' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class PlayerUiLayoutWindow {
    public delegate bool EnumProc(IntPtr window,IntPtr state);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; }
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback,IntPtr state);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint process);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,StringBuilder name,int size);
    [DllImport("user32.dll")] static extern uint MapVirtualKey(uint key,uint mode);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window,uint message,IntPtr a,IntPtr b);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] static extern int GetWindowLong(IntPtr window,int index);
    [DllImport("user32.dll")] static extern int SetWindowLong(IntPtr window,int index,int value);
    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] static extern bool AdjustWindowRectEx(ref Rect r,uint style,bool menu,uint ex);
    [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr window,IntPtr after,int x,int y,int w,int h,uint flags);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr window,out Rect rect);
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
    public static void Resize(IntPtr window,int width,int height) {
        SetThreadDpiAwarenessContext(new IntPtr(-4));
        // A borderless capture window permits a full physical 1080p client on
        // a 1080p desktop; decorated windows are constrained by its work area.
        SetWindowLong(window,-16,GetWindowLong(window,-16)&~0x00CF0000);
        var r=new Rect {right=width,bottom=height};
        if(!AdjustWindowRectEx(ref r,(uint)GetWindowLong(window,-16),false,(uint)GetWindowLong(window,-20)))
            throw new InvalidOperationException("Cannot determine native window border.");
        if(!SetWindowPos(window,IntPtr.Zero,0,0,r.right-r.left,r.bottom-r.top,0x0024))
            throw new InvalidOperationException("Native resize failed.");
    }
    public static int[] Size(IntPtr window) {
        SetThreadDpiAwarenessContext(new IntPtr(-4));
        Rect r;GetClientRect(window,out r);return new int[]{r.right-r.left,r.bottom-r.top};
    }
    public static void Key(IntPtr window,int code,bool down) {
        long bits=1L|((long)MapVirtualKey((uint)code,0)<<16);
        if(!down)bits|=0xC0000000L;
        PostMessage(window,down?0x100u:0x101u,new IntPtr(code),new IntPtr(bits));
    }
    public static void Button(IntPtr window,int x,int y,bool down) {
        var point=new IntPtr((y<<16)|(x&65535));
        PostMessage(window,0x200u,IntPtr.Zero,point);
        PostMessage(window,down?0x201u:0x202u,new IntPtr(down?1:0),point);
    }
}
'@
}
if($CheckOnly){Write-Output 'player-ui-layout: script and Win32 helper valid; GPU not started';exit 0}
if(Get-Process rasterfall -ErrorAction SilentlyContinue){throw 'GPU lane is busy; close the existing Rasterfall process before starting.'}
if(-not (Test-Path -LiteralPath $Exe)){throw 'Build and stage Rasterfall first.'}
$OutBase=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
if(-not $OutBase.StartsWith($Root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Capture output must remain within this workspace.'
}
$Out=Join-Path $OutBase ([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
[void](New-Item -ItemType Directory -Path $Out)
$Cases=@(
    [pscustomobject]@{name='720p';width=1280;height=720;scale=100},
    [pscustomobject]@{name='1080p';width=1920;height=1080;scale=100},
    [pscustomobject]@{name='Narrow';width=960;height=720;scale=150}
)
$EnvKeys=@('RF_UI_AUDIT','RF_UI_STORY','RF_UI_NO_SAVE','RF_UI_CAPTURE_DIRECTORY','RF_UI_SAVE_PATH',
    'RF_STORY_SAVE_PATH','RF_WEAVER_VIEW','RF_WEAVER_BLUEPRINT','RF_WEAVER_TIME_MS','RF_WEAVER_PAUSE_MS',
    'RF_WEAVER_RESUME_MS','RF_WEAVER_MENU','RF_WEAVER_INTERACTION_AUDIT','RF_WEAVER_CAPTURE_DIRECTORY',
    'RF_WEAVER_CAPTURE_EVERY','RF_SCENE_PERF_FRAMES','SDL_WINDOWS_DPI_AWARENESS')
$Saved=@{};foreach($Name in $EnvKeys){$Saved[$Name]=[Environment]::GetEnvironmentVariable($Name,'Process')}
$SavedPath=$env:PATH;$Records=[Collections.Generic.List[object]]::new()
function Key-Layout([IntPtr]$Window,[int]$Code) {
    [PlayerUiLayoutWindow]::Key($Window,$Code,$true)
    try{Start-Sleep -Milliseconds 140}finally{[PlayerUiLayoutWindow]::Key($Window,$Code,$false)}
    Start-Sleep -Milliseconds 240
}
function Click-Layout([IntPtr]$Window,[int]$PointX,[int]$PointY) {
    [PlayerUiLayoutWindow]::Button($Window,$PointX,$PointY,$true)
    try{Start-Sleep -Milliseconds 140}finally{[PlayerUiLayoutWindow]::Button($Window,$PointX,$PointY,$false)}
    Start-Sleep -Milliseconds 240
}
function Wait-LayoutVideo($Run,[string]$Stdout) {
    $Until=[DateTime]::UtcNow.AddSeconds(25)
    while([DateTime]::UtcNow -lt $Until) {
        if($Run.process.HasExited){throw 'Native process exited before the live camera became ready.'}
        if((Get-Content -LiteralPath $Stdout -Encoding UTF8 -ErrorAction SilentlyContinue|Out-String) -match '"video":2') {return}
        Start-Sleep -Milliseconds 100
    }
    throw 'The real video did not become ready before V05 capture.'
}
function Capture-Layout($Run,[string]$Name) {
    $Run.sequence++
    $pending=Join-Path $Run.frames 'capture.pending'
    [IO.File]::WriteAllText($pending,[string]$Run.sequence,$Utf8)
    Move-Item -LiteralPath $pending -Destination (Join-Path $Run.frames 'capture.request') -Force
    $ack=Join-Path $Run.frames 'capture.complete';$until=[DateTime]::UtcNow.AddSeconds(25)
    while([DateTime]::UtcNow -lt $until) {
        if($Run.process.HasExited){throw 'Native process exited while waiting for a capture.'}
        if(Test-Path -LiteralPath $ack) {
            $value=[IO.File]::ReadAllText($ack,[Text.Encoding]::UTF8)
            if($value -match '^(\d+) (\d+)' -and [int]$Matches[1] -eq $Run.sequence) {
                $path=Join-Path $Run.frames ('frame-{0:D6}.scene.ppm' -f $Run.sequence)
                if(-not (Test-Path -LiteralPath $path)){throw 'Capture acknowledgement has no image.'}
                $reader=[IO.StreamReader]::new($path,[Text.Encoding]::ASCII)
                try {
                    $magic=$reader.ReadLine();$dimensions=$reader.ReadLine()
                    if($magic -ne 'P6' -or $dimensions -ne "$($Run.width) $($Run.height)") {
                        throw "GPU capture dimensions differ from requested case: $dimensions"
                    }
                } finally {$reader.Dispose()}
                $Run.screens.Add([ordered]@{name=$Name;path=$path;frame=[int]$Matches[2]})
                Write-Output "[UI-LAYOUT] $($Run.name) $Name captured"
                return
            }
        }
        Start-Sleep -Milliseconds 60
    }
    throw "Capture timeout: $Name"
}
try {
    Remove-Item Env:Path -ErrorAction SilentlyContinue;Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:PATH='C:\msys64\mingw64\bin;C:\msys64\usr\bin;'+$SavedPath
    foreach($Spec in $Cases) {
        if($Case -ne 'All' -and $Spec.name -ne $Case){continue}
        foreach($Scene in @('Outpost','Weaver')) {
            foreach($Name in $EnvKeys){[Environment]::SetEnvironmentVariable($Name,$null,'Process')}
            $Name=$Spec.name+'-'+$Scene;$Frames=Join-Path $Out ($Name+'-frames')
            [void](New-Item -ItemType Directory -Path $Frames)
            $env:RF_UI_AUDIT='1';$env:RF_UI_STORY='1';$env:RF_UI_CAPTURE_DIRECTORY=$Frames
            $env:SDL_WINDOWS_DPI_AWARENESS='permonitorv2'
            $env:RF_UI_SAVE_PATH=Join-Path $Out ($Name+'-ui.cfg')
            $env:RF_STORY_SAVE_PATH=Join-Path $Out ($Name+'-story.bin')
            [IO.File]::WriteAllText($env:RF_UI_SAVE_PATH,"RFUI 1 0 $($Spec.scale) 216 1 1`n",$Utf8)
            $Stdout=Join-Path $Out ($Name+'.stdout.log');$Stderr=Join-Path $Out ($Name+'.stderr.log')
            $LaunchArgs='--renderer gpu-scene --skip-boot --map rasterfall/assets/maps/outpost.map --frame-audit'
            if($Scene -eq 'Weaver'){$LaunchArgs+=' --gpu-normal-scene mesh-weaver 0';$env:RF_WEAVER_VIEW='interaction'}
            $Run=[pscustomobject]@{name=$Name;frames=$Frames;width=$Spec.width;height=$Spec.height;sequence=0;process=$null;screens=[Collections.Generic.List[object]]::new()}
            $Record=[ordered]@{case=$Name;width=$Spec.width;height=$Spec.height;scale=$Spec.scale;status='started';
                stdout=$Stdout;stderr=$Stderr;exe_sha256=(Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash}
            $Window=[IntPtr]::Zero
            try {
                $Run.process=Start-Process -FilePath $Exe -WorkingDirectory $Package -ArgumentList $LaunchArgs -WindowStyle Hidden `
                    -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr -PassThru
                $Handle=$Run.process.Handle;$Until=[DateTime]::UtcNow.AddSeconds(30)
                while($Window -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $Until) {
                    if($Run.process.WaitForExit(60)){throw 'Native startup failed.'}
                    $Window=[PlayerUiLayoutWindow]::Find($Run.process.Id)
                }
                if($Window -eq [IntPtr]::Zero){throw 'Native SDL window did not appear.'}
                [void][PlayerUiLayoutWindow]::SetForegroundWindow($Window)
                [PlayerUiLayoutWindow]::Resize($Window,$Spec.width,$Spec.height)
                Start-Sleep -Milliseconds 1600
                $Actual=[PlayerUiLayoutWindow]::Size($Window);$Record.actual_width=$Actual[0];$Record.actual_height=$Actual[1]
                if($Actual[0] -ne $Spec.width -or $Actual[1] -ne $Spec.height){throw 'Client dimensions differ from requested V05 case.'}
                if($Scene -eq 'Weaver') {
                    Key-Layout $Window 69
                    Wait-LayoutVideo $Run $Stdout
                    Capture-Layout $Run 'weaver-player-panel'
                } else {
                    Wait-LayoutVideo $Run $Stdout
                    Capture-Layout $Run 'fps-comms'
                    Key-Layout $Window 72;Capture-Layout $Run 'fps-player'
                    Key-Layout $Window 77;Capture-Layout $Run 'rts-player'
                    # UI-only fold affordance; no selection or gameplay command.
                    $Scale=[Math]::Max(1.0,$Spec.height/720.0)*$Spec.scale/100.0
                    $Margin=[Math]::Round(18*$Scale);$Gap=[Math]::Round(12*$Scale)
                    $Dock=[Math]::Min([Math]::Round(145*$Scale),[Math]::Floor($Spec.height/3))
                    $ToggleX=$Spec.width-$Margin-[Math]::Round(50*$Scale)
                    Click-Layout $Window $ToggleX ($Spec.height-$Margin-$Dock-$Gap-[Math]::Round(13*$Scale))
                    Capture-Layout $Run 'rts-collapsed'
                    Click-Layout $Window $ToggleX ($Spec.height-$Margin-[Math]::Round(13*$Scale))
                    Capture-Layout $Run 'rts-restored'
                    Key-Layout $Window 72;Capture-Layout $Run 'rts-comms'
                    Key-Layout $Window 77;Key-Layout $Window 192;Capture-Layout $Run 'terminal'
                    Key-Layout $Window 192;Key-Layout $Window 112;Capture-Layout $Run 'render-device'
                }
                [void][PlayerUiLayoutWindow]::PostMessage($Window,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
                if(-not $Run.process.WaitForExit(30000)){throw 'Native shutdown timed out.'}
                $Run.process.WaitForExit();$Run.process.Refresh();$Record.exit_code=$Run.process.ExitCode
                if($Record.exit_code -ne 0){throw "Native exit code $($Record.exit_code)"}
                $Log=Get-Content -LiteralPath $Stdout,$Stderr -Encoding UTF8|Out-String
                if($Log -notmatch 'SCENE-NATIVE.*bridges=0.*mixed_execute=0' -or
                   $Log -match 'Validation Error|SYNC-HAZARD|VUID-|preparation/submit failed') {throw 'Native rendering evidence failed.'}
                $Record.status='captured-needs-visual-review'
            } catch {
                $Record.status='failed';$Record.error=$_.Exception.Message;throw
            } finally {
                if($Run.process) {
                    $Run.process.Refresh()
                    if(-not $Run.process.HasExited) {
                        [void][PlayerUiLayoutWindow]::PostMessage($Window,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
                        if(-not $Run.process.WaitForExit(10000)){Stop-Process -Id $Run.process.Id -Force;$Run.process.WaitForExit()}
                    }
                    $Run.process.Refresh();$Record.exit_code=$Run.process.ExitCode
                }
                $Record.screenshots=$Run.screens.ToArray();$Records.Add($Record)
                [IO.File]::WriteAllText((Join-Path $Out ($Name+'.json')),($Record|ConvertTo-Json -Depth 8),$Utf8)
            }
        }
    }
} finally {
    foreach($Name in $EnvKeys){[Environment]::SetEnvironmentVariable($Name,$Saved[$Name],'Process')}
    Remove-Item Env:Path -ErrorAction SilentlyContinue;Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:PATH=$SavedPath
    [IO.File]::WriteAllText((Join-Path $Out 'summary.json'),($Records.ToArray()|ConvertTo-Json -Depth 8),$Utf8)
}
