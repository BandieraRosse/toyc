param([string]$BuildDir = 'build-windows', [string]$Compiler = 'C:/msys64/mingw64/bin/gcc.exe')
$ErrorActionPreference = 'Stop'
$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$oldPath = $env:PATH
Push-Location -LiteralPath $repository
try {
    if (-not (Test-Path -LiteralPath $Compiler)) { throw "Native GCC not found: $Compiler" }
    $env:PATH = (Split-Path -Parent $Compiler) + ';' + $env:PATH
    $objects = @(
        'rasterfall/src/rasterfall_model.o', 'rasterfall/src/rasterfall_calibration.o',
        'rasterfall/src/rasterfall_humanoid_basis.o', 'rasterfall/src/rasterfall_humanoid_retarget.o',
        'app/linux/glb_inspect.o', 'lib/linux/assets.o', 'windows/src/runtime.o',
        'windows/src/string.o', 'windows/src/socket_winsock.o', 'lib/portable/math.o'
    ) | ForEach-Object { Join-Path $BuildDir ('win/' + $_) }
    foreach ($object in $objects) {
        if (-not (Test-Path -LiteralPath $object)) {
            throw "Missing native dependency $object. Run windows/NativeCodex.ps1 build first."
        }
    }
    $output = Join-Path $repository 'tmp/native/mesh-weaver-gpu-cache-test.exe'
    [System.IO.Directory]::CreateDirectory((Split-Path -Parent $output)) | Out-Null
    $compileArgs = @(
        '-std=c11', '-O2', '-Wall', '-Wextra', '-Wno-unused-function', '-Werror=misleading-indentation',
        '-ffunction-sections', '-fdata-sections', '-DTOYC_WINDOWS',
        '-Iwindows/include', '-Iinclude', '-idirafter', 'include/posix',
        '-idirafter', 'include/tlibc', '-Irasterfall/include', '-Irasterfall/lib', '-Igpu/include',
        '-I.windows-deps/x86_64-w64-mingw32/include', '-I.windows-deps/x86_64-w64-mingw32/include/SDL2',
        '-include', 'windows/include/windows_stdlib.h',
        'tools/mesh_weaver_gpu_cache_test.c', 'rasterfall/src/rf_mesh_weaver_presentation.c'
    ) + $objects + @('-Wl,--gc-sections', '-static', '-lws2_32', '-o', $output)
    & $Compiler @compileArgs
    if ($LASTEXITCODE -ne 0) { throw "Cache audit compilation failed: $LASTEXITCODE" }
    & $output $repository
    if ($LASTEXITCODE -ne 0) { throw "Cache audit failed: $LASTEXITCODE" }
} finally {
    $env:PATH = $oldPath
    Pop-Location
}
