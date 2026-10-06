[CmdletBinding()]
param(
    [Parameter(Position=0)]
    [ValidateSet('build','range','match','batch','inspect','self-test','train','report','help')]
    [string]$Command='help',
    [Parameter(Position=1,ValueFromRemainingArguments=$true)]
    [string[]]$ExtraArgs=@()
)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Exe=Join-Path $Root 'build-windows/rf-tactical.exe'
function Convert-ToLegacyNativeArgument([string]$Value) {
    $Escaped=[regex]::Replace($Value,'(\\*)"','$1$1\"')
    $Escaped=[regex]::Replace($Escaped,'(\\+)$','$1$1')
    return '"'+$Escaped+'"'
}
function Invoke-Native([string]$Program,[string[]]$Arguments) {
    $Preference=$ErrorActionPreference
    $ErrorActionPreference='Continue'
    $NativeArguments=$Arguments
    $ArgumentMode=Get-Variable PSNativeCommandArgumentPassing -ErrorAction SilentlyContinue
    if(!$ArgumentMode -or $ArgumentMode.Value -eq 'Legacy') {
        $NativeArguments=@($Arguments | ForEach-Object { Convert-ToLegacyNativeArgument $_ })
    }
    try { & $Program @NativeArguments; $TaskExit=$LASTEXITCODE }
    finally { $ErrorActionPreference=$Preference }
    if($TaskExit -ne 0){exit $TaskExit}
}
Push-Location $Root
try {
    if($Command -eq 'build') {
        Invoke-Native 'powershell' @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $Root 'windows/NativeCodex.ps1'),'tactical-build')
    } elseif($Command -eq 'train' -or $Command -eq 'report') {
        $Python=Get-Command python -ErrorAction Stop
        $Script=if($Command -eq 'train'){'tools/tactical_train.py'}else{'tools/tactical_report.py'}
        Invoke-Native $Python.Source (@((Join-Path $Root $Script))+$ExtraArgs)
    } else {
        if(!(Test-Path -LiteralPath $Exe)){throw 'Build first: powershell -NoProfile -ExecutionPolicy Bypass -File tools/tactical_lab.ps1 build'}
        $NativeCommand=if($Command -eq 'help'){'--help'}else{$Command}
        Invoke-Native $Exe (@($NativeCommand)+$ExtraArgs)
    }
} finally { Pop-Location }
