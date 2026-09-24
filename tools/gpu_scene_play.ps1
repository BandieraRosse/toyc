[CmdletBinding()]
param([string]$OutputDirectory='tmp/gpu-scene-play',
      [string]$ValidationLayerDirectory='', [int]$Frames=160,
      [ValidateSet('All','Interactive','World','Combat','Faults')][string]$Stage='All')
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Out=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
if (Test-Path -LiteralPath $Out) { throw 'Use a new evidence directory' }
New-Item -ItemType Directory -Path $Out | Out-Null
$Saved=@{}
foreach ($Key in @('Path','RF_GPU_VULKAN_VENDOR_ID','VK_LAYER_PATH','VK_INSTANCE_LAYERS',
    'VK_VALIDATION_VALIDATE_SYNC','VK_LAYER_REPORT_FLAGS','VK_LOADER_DEBUG')) {
    $Saved[$Key]=[Environment]::GetEnvironmentVariable($Key,'Process')
}
$Runs=[Collections.Generic.List[object]]::new()
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ScenePlayWindow {
    public delegate bool EnumProc(IntPtr w,IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback,IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr w,out uint process);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr w,System.Text.StringBuilder name,int max);
    public static IntPtr Find(int process) {
        IntPtr result=IntPtr.Zero;
        EnumWindows(delegate(IntPtr w,IntPtr p) {
            uint id; GetWindowThreadProcessId(w,out id);
            var name=new System.Text.StringBuilder(128);GetClassName(w,name,128);
            if (id==process && name.ToString()=="SDL_app") { result=w;return false; }
            return true;
        },IntPtr.Zero);
        return result;
    }
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr w,uint m,IntPtr a,IntPtr b);
    [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint key,uint mode);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr w,IntPtr after,int x,int y,int width,int height,uint flags);
}
'@
function Key([IntPtr]$Window,[int]$Code,[bool]$Down) {
    $Bits=1 -bor ([int][ScenePlayWindow]::MapVirtualKey($Code,0) -shl 16)
    if ($Code -ge 37 -and $Code -le 40) { $Bits=$Bits -bor 0x01000000 }
    if (-not $Down) { $Bits=$Bits -bor 0xC0000000 }
    [void][ScenePlayWindow]::PostMessage($Window, $(if ($Down) {0x100} else {0x101}),[IntPtr]$Code,[IntPtr]$Bits)
}
function Run([string]$Name,[string[]]$Argv,[int]$Expected=0,[int]$Count=4) {
    if (Get-Process rasterfall -ErrorAction SilentlyContinue) { throw 'Rasterfall is already running' }
    $p=Start-Process "$Package/rasterfall.exe" -WorkingDirectory $Package -WindowStyle Hidden -PassThru `
        -ArgumentList (($Argv | ForEach-Object {'"'+$_+'"'}) -join ' ') `
        -RedirectStandardOutput "$Out/$Name.out" -RedirectStandardError "$Out/$Name.err"
    $Handle=$p.Handle
    $Phase=0;$KeyUp=0;$Window=[IntPtr]::Zero
    $Deadline=[DateTime]::UtcNow.AddMinutes(8)
    while (-not $p.WaitForExit(1000)) {
        if ([DateTime]::UtcNow -gt $Deadline) { Stop-Process -Id $p.Id -Force;throw "$Name timeout" }
        if ($Name -eq 'interaction-resize') {
            $Window=[ScenePlayWindow]::Find($p.Id)
            if ($Window -ne [IntPtr]::Zero) {
                if ($KeyUp) { Key $Window $KeyUp $false;$KeyUp=0 }
                $Current=Get-Content -Encoding UTF8 "$Out/$Name.out" | Out-String
                if ($Phase -eq 0 -and $Current -match 'SCENE-UI frame=') {
                    Key $Window 27 $true;$KeyUp=27;$Phase=1
                } elseif ($Phase -eq 1 -and $Current -match 'paused=1 selected=0') {
                    Key $Window 40 $true;$KeyUp=40;$Phase=2
                } elseif ($Phase -eq 2 -and $Current -match 'paused=1 selected=1') {
                    Key $Window 39 $true;$KeyUp=39
                    [void][ScenePlayWindow]::SetWindowPos($Window,[IntPtr]::Zero,0,0,1000,650,6)
                    $Phase=3
                } elseif ($Phase -eq 3 -and $Current -match 'paused=1 selected=1 mouse=4') {
                    Key $Window 27 $true;$KeyUp=27;$Phase=4
                } elseif ($Phase -eq 4 -and $Current -match 'paused=0 selected=1 mouse=4') {
                    Key $Window 9 $true;$Phase=5
                } elseif ($Phase -eq 5 -and $Current -match 'tab=1') {
                    Key $Window 9 $false;$Phase=6
                } elseif ($Phase -eq 6) {
                    Key $Window 87 $true;$Phase=7
                } elseif ($Phase -eq 7) {
                    Key $Window 87 $false;$Phase=8
                }
            }
        }
    }
    $Runs.Add(@{name=$Name;exit_code=$p.ExitCode;argv=$Argv})
    $Runs | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 "$Out/runs.json"
    if ($p.ExitCode -ne $Expected) { throw "$Name exit $($p.ExitCode), expected $Expected" }
    $Log=Get-Content -Encoding UTF8 "$Out/$Name.out","$Out/$Name.err" | Out-String
    if ($Log -match 'Validation Error|SYNC-HAZARD|VUID-') { throw "$Name validation error" }
    if ($ValidationLayerDirectory -and ($Log -notmatch 'Insert instance layer.*VK_LAYER_KHRONOS_validation' -or
        $Log -notmatch 'Synchronization|VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT')) {
        throw "$Name validation/sync not activated"
    }
    $Native=[regex]::Matches($Log,'SCENE-NATIVE frame=(\d+) world_only=0 draws=\d+ bridges=0 readback=0 mixed_execute=0')
    $Sources=[regex]::Matches($Log,'SCENE-SOURCE frame=(\d+) independent=1 legacy_producer=0 raster_commands=0 mixed_draws=0')
    if ($Native.Count -ne $Count -or $Sources.Count -ne $Count) { throw "$Name missing independent frames" }
    for ($i=0;$i -lt $Count;$i++) {
        if ([int]$Native[$i].Groups[1].Value -ne $i+1 -or [int]$Sources[$i].Groups[1].Value -ne $i+1) {
            throw "$Name noncontiguous frame IDs"
        }
    }
    if ($Name -eq 'world-cycle') {
        foreach ($Frame in @(30,60,90)) {
            if ($Log -notmatch "GPU-WORLD-CYCLE frame=$Frame ") { throw "Missing world cycle $Frame" }
        }
    }
    if ($Name -eq 'continuous' -and $Log -notmatch 'GPU-WAVE-REPRO wave=\d+ phase=\d+ alive=[1-9]') {
        throw 'Continuous run did not reach a live enemy wave; increase -Frames'
    }
    if ($Name -eq 'interaction-resize') {
        if ($Phase -ne 8) { throw 'Pause/settings/resume/Tab/movement input sequence incomplete' }
        if ($Log -match 'rasterfall: \d+ frames, \d+ scene pixels, position=\(0,-3000\)') {
            throw 'Movement did not change the player position'
        }
        $Extents=@([regex]::Matches($Log,'SCENE-UI frame=\d+ extent=(\d+x\d+)') | ForEach-Object {$_.Groups[1].Value} | Sort-Object -Unique)
        if ($Extents.Count -lt 2) { throw 'Missing real window resize evidence' }
    }
    if ($Name -like 'fault-*' -and $Log -notmatch ('fault-injection='+$Name.Substring(6)+' frame=2')) {
        throw "$Name missing injected fault"
    }
    Write-Host "[SCENE-PLAY] $Name PASS frames=$Count"
}
try {
    [Environment]::SetEnvironmentVariable('PATH',$null,'Process')
    [Environment]::SetEnvironmentVariable('Path',$Saved['Path'],'Process')
    $env:RF_GPU_VULKAN_VENDOR_ID='0x10de'
    if ($ValidationLayerDirectory) {
        $Layer=(Resolve-Path -LiteralPath $ValidationLayerDirectory).Path
        $env:Path="$Layer;C:\msys64\mingw64\bin;$($Saved['Path'])"
        $env:VK_LAYER_PATH=$Layer;$env:VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation'
        $env:VK_VALIDATION_VALIDATE_SYNC='true';$env:VK_LAYER_REPORT_FLAGS='error,warn,info';$env:VK_LOADER_DEBUG='layer'
    }
    if ($Stage -in @('All','Interactive')) {
        Run 'outpost' @('--gpu-scene-play','--frames','4')
        Run 'interaction-resize' @('--gpu-scene-play','--frame-audit','--frames','80') 0 80
    }
    if ($Stage -in @('All','World')) {
        Run 'world-cycle' @('--gpu-scene-play','--gpu-world-cycle-test','--frames','120') 0 120
    }
    if ($Stage -in @('All','Combat')) {
        Run 'continuous' @('--gpu-scene-play','--map','rasterfall/assets/maps/rasterfall.map','--gpu-wave-repro','--gpu-normal-fixed-tick','--frames',"$Frames") 0 $Frames
    }
    if ($Stage -in @('All','Faults')) {
    foreach ($Fault in @('acquire-out-of-date','record-failure','submit-failure','present-out-of-date','present-suboptimal')) {
        $Failure=$Fault -in @('record-failure','submit-failure')
        $Code=if ($Failure) {1} else {0};$Count=if ($Failure) {1} else {4}
        Run "fault-$Fault" @('--gpu-scene-play','--frames','4','--gpu-present-fault',$Fault,'2') $Code $Count
    }
    }
    Get-FileHash "$Package/rasterfall.exe" | ConvertTo-Json | Set-Content -Encoding UTF8 "$Out/executable.json"
} finally {
    foreach ($Key in $Saved.Keys) { [Environment]::SetEnvironmentVariable($Key,$Saved[$Key],'Process') }
}
