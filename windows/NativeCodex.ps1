[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('doctor', 'build', 'asset-tools', 'package', 'test', 'gpu-test', 'run', 'acceptance', 'help')]
    [string] $Command = 'help',
    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]] $ExtraArgs = @()
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Build = Join-Path $Root 'build-windows'
$PackageRoot = Join-Path $Build 'rasterfall-windows'
$Exe = Join-Path $PackageRoot 'rasterfall.exe'
$Deps = Join-Path $Root '.windows-deps'
$MsysRoot = if ($env:RF_WINDOWS_MSYS2_ROOT) { $env:RF_WINDOWS_MSYS2_ROOT } else { 'C:\msys64' }
$MingwRoot = Join-Path $MsysRoot 'mingw64'
$Jobs = [Environment]::ProcessorCount

function Say([string] $Text) { Write-Host "[windows-native] $Text" }
function Fail([string] $Text) { throw "[windows-native] $Text" }
function Find-Tool([string] $Name) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}
function Invoke-Make([string[]] $MakeArguments) {
    $make = Find-Tool 'make'
    if (-not $make) { Fail 'make not found. Install MSYS2 make and use the MSYS2 usr/bin lane.' }
    Push-Location $Root
    try {
        & $make "-j$Jobs" @MakeArguments
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    } finally { Pop-Location }
}
function Get-SdlPrefix {
    $staged = Join-Path $Deps 'x86_64-w64-mingw32'
    if (Test-Path (Join-Path $staged 'lib\libSDL2.a')) { return $staged }
    $native = Join-Path $MingwRoot ''
    if (Test-Path (Join-Path $native 'lib\libSDL2.a')) { return $native }
    return $null
}
function Convert-ToMsysPath([string] $Path) {
    $cygpath = Join-Path $MsysRoot 'usr\bin\cygpath.exe'
    if (Test-Path -LiteralPath $cygpath) {
        return (& $cygpath -u $Path).Trim()
    }
    return ($Path -replace '\\', '/')
}
function Get-PythonForMake {
    $python = Find-Tool 'python3'
    if (-not $python) { $python = Find-Tool 'python' }
    if (-not $python) { Fail 'python3/python not found.' }
    return (Convert-ToMsysPath $python)
}
function Convert-ToLegacyNativeArgument([string] $Value) {
    # Windows PowerShell joins native arguments into a command line. Quote each
    # argument for the CRT parser, including empty values and literal quotes.
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}
function Invoke-Staged([string[]] $ProgramArguments) {
    if (-not (Test-Path -LiteralPath $Exe)) { Fail "staged executable missing: $Exe" }
    $nativeArguments = $ProgramArguments
    $argumentMode = Get-Variable PSNativeCommandArgumentPassing -ErrorAction SilentlyContinue
    if (-not $argumentMode -or $argumentMode.Value -eq 'Legacy') {
        $nativeArguments = @($ProgramArguments | ForEach-Object { Convert-ToLegacyNativeArgument $_ })
    }
    # Newer PowerShell may otherwise turn a nonzero native exit into an exception
    # before the original code can be propagated. This is local to the function.
    $PSNativeCommandUseErrorActionPreference = $false
    Push-Location $PackageRoot
    try {
        # A pipeline makes PowerShell wait for a Windows-subsystem executable.
        # Out-Host streams its output and leaves the SDL window interactive.
        & $Exe @nativeArguments | Out-Host
        $code = $LASTEXITCODE
    } finally { Pop-Location }
    if ($code -ne 0) { exit $code }
}
function Ensure-Staged([string] $Target = 'stage') {
    $sdl = Get-SdlPrefix
    if (-not $sdl) { Fail "SDL2 static library missing in $Deps or $MingwRoot. Install mingw-w64-x86_64-SDL2." }
    $makeArgs = @('-f', 'windows/Makefile', $Target, "WINDOWS_DEPS=$(Convert-ToMsysPath $Deps)", "SDL_PREFIX=$(Convert-ToMsysPath $sdl)", "MSYS2_ROOT=$MsysRootForMake")
    if ($Target -eq 'package') { $makeArgs += "PYTHON=$(Get-PythonForMake)" }
    Invoke-Make $makeArgs
}

function Doctor {
    $requiredFailed = $false
    Say "repo: $Root"
    foreach ($tool in @('git', 'python3', 'make', 'x86_64-w64-mingw32-gcc')) {
        $path = Find-Tool $tool
        if ($path) {
            Say "${tool}: $path"
            if ($tool -eq 'make' -and -not $path.StartsWith((Join-Path $MsysRoot 'usr\bin'), [System.StringComparison]::OrdinalIgnoreCase)) {
                Say "make lane: INVALID (expected $MsysRoot\usr\bin)"; $requiredFailed = $true
            }
            if ($tool -eq 'x86_64-w64-mingw32-gcc' -and -not $path.StartsWith((Join-Path $MingwRoot 'bin'), [System.StringComparison]::OrdinalIgnoreCase)) {
                Say "compiler lane: INVALID (expected $MingwRoot\bin)"; $requiredFailed = $true
            }
        } else { Say "${tool}: MISSING"; $requiredFailed = $true }
    }
    $sdl = Get-SdlPrefix
    if (-not $sdl) {
        Say "SDL2 deps: MISSING ($Deps or $MingwRoot)"; $requiredFailed = $true
    } else { Say "SDL2 deps: OK ($sdl)" }
    $vulkan = Join-Path $env:WINDIR 'System32\vulkan-1.dll'
    if (Test-Path $vulkan) { Say "Vulkan runtime: OK ($vulkan)" } else { Say 'Vulkan runtime: MISSING'; $requiredFailed = $true }
    try {
        $gpus = Get-CimInstance Win32_VideoController -ErrorAction Stop | ForEach-Object { "$($_.Name) [$($_.DriverVersion)]" }
        if ($gpus) { $gpus | ForEach-Object { Say "GPU: $_" } } else { Say 'GPU: no Win32_VideoController result' }
    } catch {
        try {
            $gpus = Get-PnpDevice -Class Display -ErrorAction Stop | ForEach-Object { $_.FriendlyName }
            if ($gpus) { $gpus | ForEach-Object { Say "GPU: $_ (PnP)" } } else { Say 'GPU: no display device result' }
        } catch { Say "GPU: system enumeration unavailable ($($_.Exception.Message)); Vulkan runtime tests remain authoritative" }
    }
    if (Test-Path $PackageRoot) { Say "package root: OK ($PackageRoot)" } else { Say "package root: MISSING ($PackageRoot)" }
    if (Test-Path $Exe) { Say "package exe: OK ($Exe)" } else { Say 'package exe: MISSING' }
    if ($requiredFailed) { exit 1 }
}

if ($Command -eq 'help') {
    @'
Windows Native Codex
  doctor      Check the fixed MSYS2/MinGW lane, SDL2, Vulkan, package and GPU.
  build       Build into build-windows/ and refresh an existing staged executable.
  asset-tools Build native GLB, RFCHAR, RFANIM and map diagnostics.
  package     Build the executable, stage assets, and create a ZIP archive.
  test        Run staged rasterfall.exe --logic-test.
  gpu-test    Run required native-present GPU smoke with frame audit.
  run         Build and stage without ZIP; remaining arguments go to rasterfall.
  acceptance  GPU smoke, normal-frame audit, and a deterministic visual capture.
'@ | Write-Host
    exit 0
}

# The wrapper owns PATH order for this lane. Existing unrelated MinGW entries
# are intentionally not searched before this fixed MSYS2 installation.
$inheritedSearchPath = $env:PATH
# Some launchers supply both spellings. Normalize them before native child
# creation; PowerShell's case-insensitive environment dictionary rejects twins.
Remove-Item Env:Path -ErrorAction SilentlyContinue
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:PATH = "$MingwRoot\bin;$MsysRoot\usr\bin;$inheritedSearchPath"
$env:SHELL = Join-Path $MsysRoot 'usr\bin\sh.exe'
$MsysRootForMake = $MsysRoot -replace '\\', '/'

switch ($Command) {
    'doctor' { Doctor }
    'build' {
        $sdl = Get-SdlPrefix
        if (-not $sdl) { Fail "SDL2 static library missing in $Deps or $MingwRoot. Install mingw-w64-x86_64-SDL2." }
        Invoke-Make @('-f', 'windows/Makefile', 'all', "WINDOWS_DEPS=$(Convert-ToMsysPath $Deps)", "SDL_PREFIX=$(Convert-ToMsysPath $sdl)", "MSYS2_ROOT=$MsysRootForMake")
        if (Test-Path -LiteralPath $Exe) {
            Copy-Item -LiteralPath (Join-Path $Build 'rasterfall.exe') -Destination $Exe -Force
            Say "staged executable refreshed: $Exe"
        }
    }
    'asset-tools' {
        $sdl = Get-SdlPrefix
        if (-not $sdl) { Fail "SDL2 static library missing in $Deps or $MingwRoot. Install mingw-w64-x86_64-SDL2." }
        Invoke-Make @('-f', 'windows/Makefile', 'asset-tools', "WINDOWS_DEPS=$(Convert-ToMsysPath $Deps)", "SDL_PREFIX=$(Convert-ToMsysPath $sdl)", "MSYS2_ROOT=$MsysRootForMake")
    }
    'package' { Ensure-Staged 'package' }
    'test' { Ensure-Staged; Invoke-Staged @('--logic-test') }
    'gpu-test' {
        Ensure-Staged
        $gpuArgs = @('--renderer', 'gpu-scene', '--gpu-normal-scene', 'near', '0', '--frame-audit', '--frames', '120') + $ExtraArgs
        Invoke-Staged $gpuArgs
    }
    'run' { Ensure-Staged; Invoke-Staged $ExtraArgs }
    'acceptance' {
        Ensure-Staged
        Invoke-Staged @('--renderer', 'gpu-scene', '--gpu-normal-scene', 'near', '0', '--frame-audit', '--frames', '120')
        Invoke-Staged @('--normal-frame-audit', '0', '0', '0', '1', '0', '1', '1280', '720', 'windows-native-normal.bmp')
        Invoke-Staged @('--visual-capture', 'procedural-humanoid', '--visual-output', 'windows-native-procedural-humanoid.bmp')
    }
}
