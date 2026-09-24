# Deferred network investigation after GPU migration; not a current stage gate.
# Guest visibility/count expectations are unresolved. Use pose/logic tests now.
[CmdletBinding()]
param([string]$OutputDirectory='tmp/gpu-scene-network',[int]$Port=29461)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
$Out=[IO.Path]::GetFullPath((Join-Path $Root $OutputDirectory))
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$SavedPath=$env:Path
$SavedVendor=$env:RF_GPU_VULKAN_VENDOR_ID
$Processes=@()
try {
    if (Get-Process rasterfall -ErrorAction SilentlyContinue | Where-Object { -not $_.HasExited }) {
        throw 'Rasterfall is already running'
    }
    [Environment]::SetEnvironmentVariable('PATH',$null,'Process')
    [Environment]::SetEnvironmentVariable('Path',$SavedPath,'Process')
    $env:RF_GPU_VULKAN_VENDOR_ID='0x10de'
    foreach ($Role in @('host','guest')) {
        $View=if ($Role -eq 'host') {'near'} else {'mid'}
        $Frames=if ($Role -eq 'host') {32} else {8}
        $Argv=@('--renderer','gpu-compute','--gpu-required','--gpu-native-present',
            '--port',$Port,'--gpu-normal-scene',$View,0,'--frame-audit','--frames',$Frames,
            '--gpu-frame-capture',"$Out/$Role.bmp",'--gpu-capture-frame',4)
        if ($Role -eq 'host') {$Argv+='--host'} else {$Argv+=@('--connect','127.0.0.1')}
        $Quoted=($Argv | ForEach-Object {'"'+$_+'"'}) -join ' '
        $p=Start-Process -FilePath "$Package/rasterfall.exe" -WorkingDirectory $Package `
            -ArgumentList $Quoted -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput "$Out/$Role.out" -RedirectStandardError "$Out/$Role.err"
        $ProcessHandle=$p.Handle
        $Processes+=@{Role=$Role;Process=$p}
        if ($Role -eq 'host' -and $p.WaitForExit(1000)) {throw "host exited early: $($p.ExitCode)"}
    }
    $Deadline=[DateTime]::UtcNow.AddSeconds(240)
    foreach ($Entry in $Processes) {
        $p=$Entry.Process
        while (-not $p.WaitForExit(10000)) {
            if ([DateTime]::UtcNow -gt $Deadline) {throw 'network fixture timeout'}
            Write-Host "[SCENE-NETWORK] waiting for $($Entry.Role)"
        }
        if ($p.ExitCode -ne 0) {throw "$($Entry.Role) exit $($p.ExitCode)"}
        $Log=Get-Content -Encoding UTF8 "$Out/$($Entry.Role).out","$Out/$($Entry.Role).err" | Out-String
        if ($Log -match 'Validation Error|SYNC-HAZARD|VUID-|SCENE-.*failed') {throw "$($Entry.Role) failed"}
        $Mode=if ($Entry.Role -eq 'host') {1} else {2}
        if ($Log -notmatch "SCENE-PROCEDURAL[^\r\n]*items=[5-9][^\r\n]*net_mode=$Mode") {
            throw "$($Entry.Role) did not freeze a remote player"
        }
        if ($Entry.Role -eq 'guest' -and $Log -notmatch 'supplemental_modular=8') {
            throw 'guest did not freeze the modular roster'
        }
        if ($Log -notmatch 'FRAME-AUDIT.*path=gpu-native' -or
            -not (Test-Path "$Out/$($Entry.Role).bmp.scene.ppm")) {throw 'missing native capture'}
        Write-Host "[SCENE-NETWORK] $($Entry.Role) PASS"
    }
} finally {
    foreach ($Entry in $Processes) {
        if (-not $Entry.Process.HasExited) {Stop-Process -Id $Entry.Process.Id -Force}
    }
    $env:Path=$SavedPath
    [Environment]::SetEnvironmentVariable('RF_GPU_VULKAN_VENDOR_ID',$SavedVendor,'Process')
}
