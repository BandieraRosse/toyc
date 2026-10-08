param([string]$Gcc = 'C:\msys64\mingw64\bin\gcc.exe')
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $repoRoot 'tmp\audio-test'
[void][IO.Directory]::CreateDirectory($outputDirectory)
$executable = Join-Path $outputDirectory 'rf_audio_test.exe'
$savedPath = $env:PATH
$result = 1
Push-Location $repoRoot
try {
    Remove-Item Env:Path -ErrorAction SilentlyContinue
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:PATH = (Split-Path -Parent $Gcc) + ';' + $savedPath
    $arguments = @('-O2','-std=c11','-Wall','-Wextra','-Werror',
        '-ffunction-sections','-fdata-sections','-DTOYC_WINDOWS',
        '-Iwindows/include','-Iinclude','-idirafter','include/posix',
        '-idirafter','include/tlibc','-Irasterfall/include',
        '-include','windows/include/windows_stdlib.h',
        'tools/rf_audio_test.c','rasterfall/lib/sfx.c',
        'build-windows/win/windows/src/runtime.o',
        'build-windows/win/windows/src/env.o',
        'build-windows/win/windows/src/string.o',
        'build-windows/win/windows/src/socket_winsock.o',
        'build-windows/win/windows/src/pthread_win32.o',
        'build-windows/win/lib/linux/assets.o',
        'build-windows/win/lib/portable/math.o',
        '-Wl,--gc-sections','-lws2_32','-o',$executable)
    & $Gcc @arguments
    $result = $LASTEXITCODE
    if ($result -eq 0) {
        & $executable (Join-Path $outputDirectory 'settings.cfg') (Join-Path $repoRoot 'build-windows\rasterfall-windows')
        $result = $LASTEXITCODE
    }
} finally {
    Remove-Item Env:Path -ErrorAction SilentlyContinue
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:PATH = $savedPath
    Pop-Location
}
exit $result
