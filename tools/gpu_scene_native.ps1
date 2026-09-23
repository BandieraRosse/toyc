[CmdletBinding()]
param([string]$OutputDirectory='', [string]$ValidationLayerDirectory='',
    [ValidatePattern('^$|^[0-9a-fA-F]{4}$')][string]$DeviceVendor='')
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package=Join-Path $Root 'build-windows/rasterfall-windows'
if (-not $OutputDirectory) { $OutputDirectory=Join-Path $Root ('tmp/scene-native-'+(Get-Date -Format 'yyyyMMdd-HHmmss')) }
$Out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $Out){throw 'Use a new evidence directory.'}
New-Item -ItemType Directory -Path $Out | Out-Null
$SavedPath=$env:Path
[Environment]::SetEnvironmentVariable('PATH',$null,'Process')
[Environment]::SetEnvironmentVariable('Path',$SavedPath,'Process')
$Keys=@('VK_LAYER_PATH','VK_INSTANCE_LAYERS','VK_VALIDATION_VALIDATE_SYNC','VK_LAYER_REPORT_FLAGS','VK_LOADER_DEBUG','RF_GPU_VULKAN_VENDOR_ID')
$Saved=@{};foreach($Key in $Keys){$Saved[$Key]=[Environment]::GetEnvironmentVariable($Key,'Process')}
$Runs=[Collections.Generic.List[object]]::new()
$Manifest=[ordered]@{result='FAIL'; validation_sync='NOT RUN - layer not requested'; requested_vendor=$DeviceVendor; intel_native='NOT VERIFIED'; executable_sha256=(Get-FileHash "$Package/rasterfall.exe").Hash; runs=$Runs}
function Run([string]$Name,[string[]]$Argv,[int]$Expected=0,[int]$Frames=0,[string]$Fault='') {
    if(Get-Process rasterfall -ErrorAction SilentlyContinue){throw 'Another Rasterfall process is running.'}
    $Quoted=($Argv | ForEach-Object {'"'+$_+'"'}) -join ' '
    $p=Start-Process -FilePath "$Package/rasterfall.exe" -WorkingDirectory $Package -ArgumentList $Quoted -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput "$Out/$Name.out" -RedirectStandardError "$Out/$Name.err"
    $Runs.Add([ordered]@{name=$Name; exit_code=$p.ExitCode; argv=$Argv})
    $Manifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 "$Out/manifest.json"
    if($p.ExitCode -ne $Expected){throw "$Name exit $($p.ExitCode), expected $Expected"}
    $Log=(Get-Content "$Out/$Name.out","$Out/$Name.err" | Out-String)
    if($Log -match 'Validation Error|SYNC-HAZARD|VUID-'){throw "$Name validation error"}
    if($Name -like 'scene-*') {
        if($Log -notmatch 'SCENE occlusion=PASS' -or $Log -notmatch 'SCENE cleanup live=0 retired=0 pinned=0'){throw "$Name incomplete diagnostics/cleanup"}
        if($Log -notmatch "SCENE result=$Expected rendered=$Frames bridge=0 "){throw "$Name incomplete native frames"}
        if($DeviceVendor -and $Log -notmatch "SCENE adapter=.+ vendor=$DeviceVendor device="){throw "$Name wrong device"}
        if($Expected -and $Log -notmatch 'pins_held_until_drain=3'){throw "$Name early pin release"}
        if($Fault -and $Log -notmatch "fault-injection=$Fault frame=2"){throw "$Name missing fault"}
        $SyncEnabled=($Log -match 'CURRENT-VALIDATION-ENABLED' -and $Log -match ' - Synchronization') -or
            ($Log -match 'Khronos Validation Layer Active:' -and $Log -match 'Current Enables:.*VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT')
        if($ValidationLayerDirectory -and ($Log -notmatch 'Insert instance layer.*VK_LAYER_KHRONOS_validation' -or -not $SyncEnabled)){throw "$Name layer/sync activation not proven"}
    }
    Write-Host "[SCENE] $Name PASS"
}
try {
    $env:RF_GPU_VULKAN_VENDOR_ID=$DeviceVendor
    if($ValidationLayerDirectory) {
        if(-not(Test-Path "$ValidationLayerDirectory/VkLayer_khronos_validation.json")){throw 'Validation layer manifest missing.'}
        $env:Path="$ValidationLayerDirectory;C:\msys64\mingw64\bin;$SavedPath"
        $env:VK_LAYER_PATH=$ValidationLayerDirectory;$env:VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation'
        $env:VK_VALIDATION_VALIDATE_SYNC='true';$env:VK_LAYER_REPORT_FLAGS='error,warn,info';$env:VK_LOADER_DEBUG='layer'
        $Manifest.validation_layer=@(Get-FileHash "$ValidationLayerDirectory/VkLayer_khronos_validation.json","$ValidationLayerDirectory/VkLayer_khronos_validation.dll" | Select-Object Path,Hash)
    }
    Run 'scene-lifecycle' @('--gpu-scene-native-fixture','--frames','120') 0 120
    $Log=Get-Content "$Out/scene-lifecycle.out" | Out-String
    if($Log -notmatch 'resource-growth object=1 vertices=10200' -or $Log -notmatch 'world-retirement PASS'){throw 'Missing growth/world retirement'}
    $Extents=@([regex]::Matches($Log,'extent=(\d+x\d+)') | ForEach-Object {$_.Groups[1].Value} | Sort-Object -Unique)
    if($Extents.Count -lt 2){throw 'Missing resize evidence'}
    foreach($Name in @('scene-native','scene-object-0','scene-object-1','scene-object-2')){Copy-Item -LiteralPath "$Package/$Name.ppm" -Destination "$Out/$Name.ppm"}
    foreach($Fault in @('acquire-out-of-date','record-failure','submit-failure','present-out-of-date','present-suboptimal')) {
        $Failure=$Fault -in @('record-failure','submit-failure')
        $Code=if($Failure){3}else{0};$Count=if($Failure){1}else{4}
        Run "scene-$Fault" @('--gpu-scene-native-fixture','--frames','4','--gpu-present-fault',$Fault,'2') $Code $Count $Fault
    }
    if($ValidationLayerDirectory){$Manifest.validation_sync='PASS'}
    if($DeviceVendor -eq '8086'){$Manifest.intel_native='PASS'}
    Run 'pose' @('--gpu-scene-pose-test')
    Run 'logic' @('--logic-test')
    Run 'normal-native' @('--renderer','gpu-compute','--gpu-required','--gpu-native-present','--gpu-normal-scene','near','0','--gpu-normal-fixed-tick','--frame-audit','--frames','3')
    if((Get-FileHash "$Package/rasterfall.exe").Hash -ne $Manifest.executable_sha256){throw 'Executable changed during suite.'}
    $Manifest.result='PASS - available gates'
} finally {
    $Manifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 "$Out/manifest.json"
    foreach($Key in $Keys){[Environment]::SetEnvironmentVariable($Key,$Saved[$Key],'Process')}
    $env:Path=$SavedPath
}
