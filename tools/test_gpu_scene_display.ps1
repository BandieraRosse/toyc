param(
    [string]$Gcc = 'C:\msys64\mingw64\bin\gcc.exe'
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$repoRoot = Split-Path -Parent $PSScriptRoot
$gccPath = [IO.Path]::GetFullPath($Gcc)
if (-not (Test-Path -LiteralPath $gccPath -PathType Leaf)) {
    throw "Native GCC was not found: $gccPath (use -Gcc to select it)."
}
$outputDirectory = Join-Path $repoRoot 'tmp\scene-display-test'
[void][IO.Directory]::CreateDirectory($outputDirectory)
$executable = Join-Path $outputDirectory 'rf_gpu_scene_display_test.exe'
$savedPath = $env:PATH
$result = 1
Push-Location $repoRoot
try {
    # MinGW's cc1/as helpers must be available; keep a single environment key.
    Remove-Item Env:Path -ErrorAction SilentlyContinue
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:PATH = (Split-Path -Parent $gccPath) + ';' + $savedPath
    $compilerArguments = @(
        '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
        '-flto', '-fwhole-program', '-DTOYC_WINDOWS', '-I.', '-Iwindows/include',
        '-Iinclude', '-idirafter', 'include/posix', '-idirafter', 'include/tlibc',
        '-Irasterfall/include', '-Irasterfall/lib', '-Igpu/include', '-include',
        'windows/include/windows_stdlib.h', 'tools/rf_gpu_scene_display_test.c',
        '-o', $executable
    )
    Write-Host "Compiling retained-display CPU contract test with $gccPath"
    & $gccPath @compilerArguments
    $result = $LASTEXITCODE
    if ($result -eq 0) {
        & $executable
        $result = $LASTEXITCODE
    }
}
finally {
    Remove-Item Env:Path -ErrorAction SilentlyContinue
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:PATH = $savedPath
    Pop-Location
}
if ($result -ne 0) { Write-Host "Retained-display test failed (exit $result)." }
exit $result
