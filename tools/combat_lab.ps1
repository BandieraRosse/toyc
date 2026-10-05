[CmdletBinding()]
param(
    [ValidateSet('Suite','Scale','Play')][string]$Stage='Suite',
    [ValidateRange(-1,20)][int]$Preset=-1,
    [ValidateRange(1,3)][int]$Repeats=3,
    [ValidateRange(1,980000)][int]$Seed=1337,
    [string]$OutputDirectory='tmp/combat-lab',
    [switch]$Observe,
    [int]$Width=0,
    [int]$Height=0
)
$ErrorActionPreference='Stop'
if(($Width -ne 0 -or $Height -ne 0) -and ($Width -lt 640 -or $Width -gt 7680 -or $Height -lt 480 -or $Height -gt 4320)) {
    throw 'Specify both Width (640..7680) and Height (480..4320), or leave both zero for the native display default'
}
$TaskPath=[Environment]::GetEnvironmentVariable('Path','Process')
[Environment]::SetEnvironmentVariable('PATH',$null,'Process')
[Environment]::SetEnvironmentVariable('Path',$TaskPath,'Process')
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Out=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
if(Test-Path -LiteralPath $Out){throw 'Use a new evidence directory'}
if(Get-Process rasterfall -ErrorAction SilentlyContinue){throw 'Rasterfall is already running'}
if(!(Test-Path -LiteralPath "$Package/rasterfall.exe")){throw 'Stage the current build with NativeCodex.ps1 test or run first'}
New-Item -ItemType Directory -Path $Out | Out-Null
$Utf8=[Text.UTF8Encoding]::new($false)
$Started=Get-Date
$Commit=(& git -C $Root rev-parse HEAD).Trim()
$Status=@(& git -C $Root status --short)
$SavedCommit=$env:RF_COMBAT_COMMIT
$SavedVendor=$env:RF_GPU_VULKAN_VENDOR_ID
$env:RF_COMBAT_COMMIT=$Commit
if($Stage -eq 'Scale'){$env:RF_GPU_VULKAN_VENDOR_ID='0x10de'}
$TaskProcess=$null
function Write-Json($Value,[string]$Name) {
    [IO.File]::WriteAllText((Join-Path $Out $Name),(ConvertTo-Json -InputObject $Value -Depth 8),$Utf8)
}
try {
    Write-Json @{started=$Started.ToString('o');timezone=[TimeZoneInfo]::Local.Id;commit=$Commit;status=$Status;stage=$Stage;preset=$Preset;seed=$Seed;repeats=$Repeats;observe=[bool]$Observe;window_width=$Width;window_height=$Height;exe_hash=(Get-FileHash -LiteralPath "$Package/rasterfall.exe").Hash;map_hash=(Get-FileHash -LiteralPath "$Package/rasterfall/assets/maps/outpost.map").Hash} 'manifest.json'
    $Cases=if($Stage -eq 'Scale'){@(for($Round=0;$Round -lt $Repeats;$Round++){19;20})}elseif($Preset -ge 0){@($Preset)}else{@(-1)}
    $Trial=0
    foreach($Case in $Cases) {
        $Trial++
        $Name=if($Case -lt 0){'suite'}else{"preset-$Case"}
        $RunSeed=$Seed
        if($Stage -eq 'Scale') {
            $Round=[int][math]::Ceiling($Trial/2)
            $Name="r$Round-$Name";$RunSeed+=7919*($Round-1)
        }
        $Csv=Join-Path $Out "$Name.csv"
        $Arguments=@('--skip-boot','--map','rasterfall/assets/maps/outpost.map','--combat-lab-output',('"'+$Csv+'"'),'--combat-lab-seed',[string]$RunSeed,'--combat-lab-repeat',[string]$Repeats)
        if($Width) {$Arguments+=@('--window-size',[string]$Width,[string]$Height)}
        if($Stage -eq 'Suite') {
            $Arguments+='--combat-lab-suite'
            if($Case -ge 0){$Arguments+=@('--combat-lab',[string]$Case)}
        } else {
            $Arguments+=@('--gpu-scene-play','--combat-lab',[string]$(if($Case -lt 0){18}else{$Case}))
            if($Observe -or $Stage -eq 'Scale'){$Arguments+='--combat-lab-observe'}
            if($Stage -eq 'Scale'){$Arguments+='--combat-lab-auto-exit'}
        }
        Write-Host "[COMBAT-LAB] $Name $Stage"
        if($Stage -eq 'Play') {
            # The user requested an interactive window in Play mode.
            $TaskProcess=Start-Process -FilePath "$Package/rasterfall.exe" -WorkingDirectory $Package -ArgumentList $Arguments -PassThru -RedirectStandardOutput "$Out/$Name.out" -RedirectStandardError "$Out/$Name.err"
            Write-Host "Interactive process $($TaskProcess.Id); close Rasterfall to finish."
        } else {
            $TaskProcess=Start-Process -FilePath "$Package/rasterfall.exe" -WorkingDirectory $Package -ArgumentList $Arguments -PassThru -WindowStyle Hidden -RedirectStandardOutput "$Out/$Name.out" -RedirectStandardError "$Out/$Name.err"
        }
        $TaskHandle=$TaskProcess.Handle
        $Timer=[Diagnostics.Stopwatch]::StartNew()
        while(!$TaskProcess.WaitForExit(500)) {
            if($Stage -ne 'Play' -and $Timer.Elapsed.TotalSeconds -gt 300) {
                Stop-Process -Id $TaskProcess.Id -Force
                throw "$Name exceeded 300 seconds; process stopped; inspect evidence"
            }
        }
        $TaskProcess.WaitForExit()
        if(Test-Path -LiteralPath "$Package/rasterfall.log"){Copy-Item -LiteralPath "$Package/rasterfall.log" -Destination "$Out/$Name.rasterfall.log"}
        if($TaskProcess.ExitCode -ne 0){throw "$Name failed: exit $($TaskProcess.ExitCode)"}
        if(!(Test-Path -LiteralPath $Csv)){throw "$Name did not produce a CSV"}
        $Rows=@(Import-Csv -LiteralPath $Csv -Encoding UTF8)
        $Expected=if($Stage -eq 'Suite'){$Repeats*$(if($Case -lt 0){21}else{1})}else{1}
        if(($Stage -ne 'Play' -and $Rows.Count -ne $Expected) -or ($Stage -eq 'Play' -and !$Rows.Count)) {
            throw "$Name expected results (batch count $Expected), found $($Rows.Count)"
        }
        foreach($Row in $Rows) {
            if($Row.status -eq 'timeout' -and [int]$Row.ttk_ms -ge 0){throw 'A timed-out trial incorrectly reported TTK'}
            if([int]$Row.dt_ms -ne 16 -or [int]$Row.ticks -le 0){throw 'Missing fixed-step sample'}
            if($Stage -eq 'Scale' -and [int]$Row.frames -le 0){throw 'Scale trial lacks native frame samples'}
            if($Stage -eq 'Scale' -and $Row.status -eq 'aborted'){throw 'Scale trial did not reach its complete sampling duration'}
        }
        Write-Host "[COMBAT-LAB] $Name completed, $($Rows.Count) rows"
    }
    $AllRows=@(Get-ChildItem -LiteralPath $Out -Filter '*.csv' | ForEach-Object {Import-Csv -LiteralPath $_.FullName -Encoding UTF8})
    $Summary=@($AllRows | Group-Object preset | ForEach-Object {
        $Group=@($_.Group)
        $Times=@($Group | ForEach-Object {[int]$_.elapsed_ms} | Sort-Object)
        $Ttk=@($Group | Where-Object {[int]$_.ttk_ms -ge 0} | ForEach-Object {[int]$_.ttk_ms} | Sort-Object)
        $Dps=@($Group | ForEach-Object {1000.0*[int]$_.health_damage/[math]::Max(1,[int]$_.elapsed_ms)} | Sort-Object)
        $Shots=($Group | Measure-Object -Property shots -Sum).Sum
        $Pellets=($Group | Measure-Object -Property pellets -Sum).Sum
        $Hits=($Group | Measure-Object -Property hits -Sum).Sum
        @{preset=[int]$_.Name;configuration=$Group[0].configuration;runs=$Group.Count;completed=@($Group | Where-Object status -eq 'complete').Count;timeouts=@($Group | Where-Object status -eq 'timeout').Count;aborted=@($Group | Where-Object status -eq 'aborted').Count;crossed=@($Group | Where-Object crossed -eq '1').Count;time_median_ms=$Times[[int][math]::Floor($Times.Count/2)];time_min_ms=$Times[0];time_max_ms=$Times[-1];ttk_killed_only_median_ms=$(if($Ttk.Count){$Ttk[[int][math]::Floor($Ttk.Count/2)]}else{$null});effective_health_dps_median=$Dps[[int][math]::Floor($Dps.Count/2)];effective_health_dps_min=$Dps[0];effective_health_dps_max=$Dps[-1];shots_total=$Shots;pellets_total=$Pellets;hits_total=$Hits;hits_per_shot=$(if($Shots){$Hits/$Shots}else{$null});hits_per_pellet=$(if($Pellets){$Hits/$Pellets}else{$null});reloads_total=($Group | Measure-Object -Property reloads -Sum).Sum}
    })
    Write-Json @{started=$Started.ToString('o');finished=(Get-Date).ToString('o');results=$Summary} 'summary.json'
} finally {
    if($TaskProcess -and !$TaskProcess.HasExited){Stop-Process -Id $TaskProcess.Id -Force}
    [Environment]::SetEnvironmentVariable('RF_COMBAT_COMMIT',$SavedCommit,'Process')
    [Environment]::SetEnvironmentVariable('RF_GPU_VULKAN_VENDOR_ID',$SavedVendor,'Process')
}
