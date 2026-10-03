param(
    [string]$Gcc = 'C:\msys64\mingw64\bin\gcc.exe',
    [string]$BuildDir = 'build-windows'
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$repoRoot = Split-Path -Parent $PSScriptRoot
$gccPath = [IO.Path]::GetFullPath($Gcc)
if (-not (Test-Path -LiteralPath $gccPath -PathType Leaf)) { throw "Native GCC not found: $gccPath" }
$buildRoot = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }
$mathObject = Join-Path $buildRoot 'win\lib\portable\math.o'
if (-not (Test-Path -LiteralPath $mathObject -PathType Leaf)) {
    throw "Missing runtime math object: $mathObject. Run windows/NativeCodex.ps1 build first."
}
$outputDirectory = Join-Path $repoRoot 'tmp\weaver-audio-test'
[void][IO.Directory]::CreateDirectory($outputDirectory)
$executable = Join-Path $outputDirectory 'rf_weaver_audio_test.exe'
$savedPath = $env:PATH
$result = 1
Push-Location $repoRoot
try {
    Remove-Item Env:Path -ErrorAction SilentlyContinue
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:PATH = (Split-Path -Parent $gccPath) + ';' + $savedPath
    $compilerArguments = @('-O2', '-std=c11', '-Wall', '-Wextra', '-Werror',
        '-Irasterfall/include', 'tools/rf_weaver_audio_test.c', $mathObject, '-o', $executable)
    Write-Host "Weaver PCM contract test links runtime math: $mathObject"
    & $gccPath @compilerArguments
    $result = $LASTEXITCODE
    if ($result -eq 0) { & $executable; $result = $LASTEXITCODE }
}
finally {
    Remove-Item Env:Path -ErrorAction SilentlyContinue
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    $env:PATH = $savedPath
    Pop-Location
}
exit $result
