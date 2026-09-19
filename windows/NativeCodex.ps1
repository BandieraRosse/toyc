[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('doctor', 'build', 'package', 'test', 'gpu-test', 'run', 'acceptance', 'help')]
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
function Invoke-Packaged([string[]] $ProgramArguments) {
    if (-not (Test-Path -LiteralPath $Exe)) { Fail "package executable missing: $Exe (run package first)" }
    Push-Location $PackageRoot
    try {
        & '.\rasterfall.exe' @ProgramArguments
        $code = $LASTEXITCODE
    } finally { Pop-Location }
    if ($code -ne 0) { exit $code }
}
function Ensure-Package {
    $sdl = Get-SdlPrefix
    if (-not $sdl) { Fail "SDL2 static library missing in $Deps or $MingwRoot. Install mingw-w64-x86_64-SDL2." }
    Invoke-Make @('-f', 'windows/Makefile', 'package', "WINDOWS_DEPS=$(Convert-ToMsysPath $Deps)", "SDL_PREFIX=$(Convert-ToMsysPath $sdl)", "MSYS2_ROOT=$MsysRootForMake", "PYTHON=$(Get-PythonForMake)")
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
        $gpus = Get-CimInstance Win32_VideoController | ForEach-Object { "$($_.Name) [$($_.DriverVersion)]" }
        if ($gpus) { $gpus | ForEach-Object { Say "GPU: $_" } } else { Say 'GPU: no Win32_VideoController result' }
    } catch {
        try {
            $gpus = Get-PnpDevice -Class Display | ForEach-Object { $_.FriendlyName }
            if ($gpus) { $gpus | ForEach-Object { Say "GPU: $_ (PnP)" } } else { Say 'GPU: no display device result' }
        } catch { Say "GPU: query failed ($($_.Exception.Message))" }
    }
    if (Test-Path $PackageRoot) { Say "package root: OK ($PackageRoot)" } else { Say "package root: MISSING ($PackageRoot)" }
    if (Test-Path $Exe) { Say "package exe: OK ($Exe)" } else { Say 'package exe: MISSING' }
    if ($requiredFailed) { exit 1 }
}

if ($Command -eq 'help') {
    @'
Windows Native Codex
  doctor      Check the fixed MSYS2/MinGW lane, SDL2, Vulkan, package and GPU.
  build       Build the Windows executable into build-windows/.
  package     Build the executable and complete package.
  test        Run package/rasterfall.exe --logic-test.
  gpu-test    Run required native-present GPU smoke with frame audit.
  run         Run from the package root; remaining arguments go to rasterfall.
  acceptance  GPU smoke, normal-frame audit, and a deterministic visual capture.
'@ | Write-Host
    exit 0
}

# The wrapper owns PATH order for this lane. Existing unrelated MinGW entries
# are intentionally not searched before this fixed MSYS2 installation.
$env:Path = "$MingwRoot\bin;$MsysRoot\usr\bin;$env:Path"
$env:SHELL = Join-Path $MsysRoot 'usr\bin\sh.exe'
$MsysRootForMake = $MsysRoot -replace '\\', '/'

switch ($Command) {
    'doctor' { Doctor }
    'build' {
        $sdl = Get-SdlPrefix
        if (-not $sdl) { Fail "SDL2 static library missing in $Deps or $MingwRoot. Install mingw-w64-x86_64-SDL2." }
        Invoke-Make @('-f', 'windows/Makefile', 'all', "WINDOWS_DEPS=$(Convert-ToMsysPath $Deps)", "SDL_PREFIX=$(Convert-ToMsysPath $sdl)", "MSYS2_ROOT=$MsysRootForMake")
    }
    'package' { Ensure-Package }
    'test' { Ensure-Package; Invoke-Packaged @('--logic-test') }
    'gpu-test' {
        Ensure-Package
        $gpuArgs = @('--renderer', 'gpu-compute', '--gpu-required', '--gpu-native-present', '--gpu-normal-scene', 'near', '0', '--frame-audit', '--frames', '120') + $ExtraArgs
        Invoke-Packaged $gpuArgs
    }
    'run' { Ensure-Package; Invoke-Packaged $ExtraArgs }
    'acceptance' {
        Ensure-Package
        Invoke-Packaged @('--renderer', 'gpu-compute', '--gpu-required', '--gpu-native-present', '--gpu-normal-scene', 'near', '0', '--frame-audit', '--frames', '120')
        Invoke-Packaged @('--normal-frame-audit', '0', '0', '0', '1', '0', '1', '1280', '720', 'windows-native-normal.bmp')
        Invoke-Packaged @('--visual-capture', 'procedural-humanoid', '--visual-output', 'windows-native-procedural-humanoid.bmp')
    }
}
