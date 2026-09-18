# Windows Rasterfall build

## Windows Native Codex 配置

本项目支持完全 Windows 原生的 Rasterfall 开发，不需要 WSL。MSYS2 可使用
[nightly x86_64 installer](https://github.com/msys2/msys2-installer/releases/tag/nightly-x86_64)
安装，建议安装到 `C:\msys64`。

### 1. 安装 MSYS2 工具

打开 **MSYS2 MSYS** 终端，执行：

```bash
pacman -Syu
# 如果提示关闭终端，重新打开 MSYS2 MSYS 后继续执行：
pacman -Su
pacman -S --needed make python git
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-binutils
pacman -S --needed mingw-w64-x86_64-SDL2
```

固定使用 `C:\msys64\usr\bin` 和 `C:\msys64\mingw64\bin`，不要混入其他
MinGW/MSYS2 ABI。

### 2. 用 PowerShell 让 Codex 检查环境

```powershell
cd C:\Users\15259\Desktop\tcc
Set-ExecutionPolicy -Scope Process Bypass
.\windows\NativeCodex.ps1 doctor
```

如果 MSYS2 安装在其他目录，先设置：

```powershell
$env:RF_WINDOWS_MSYS2_ROOT = "D:\msys64"
```

`doctor` 必须能找到 Git、Python、Make、MinGW GCC、SDL2、Vulkan runtime 和物理
GPU。wrapper 会自动设置 Codex 所需 PATH 和 MSYS2 shell，不需要手工修改系统 PATH。

### 3. 构建、测试和 GPU 验收

```powershell
.\windows\NativeCodex.ps1 package
.\windows\NativeCodex.ps1 test
.\windows\NativeCodex.ps1 gpu-test
.\windows\NativeCodex.ps1 acceptance
```

Windows 对象和 package 位于 `build-windows\`；真实运行 root 是
`build-windows\rasterfall-windows\`。日常开发只使用上述 wrapper 命令；不要在
PowerShell 中直接执行根目录 `make`，那会进入 Linux/Toyc 的 `build\` 构建链。

The supported native development entry point is `windows/NativeCodex.ps1`.
It keeps the Linux `build/` tree untouched, builds into `build-windows/`, and
always runs the game from the packaged root `build-windows/rasterfall-windows`.
Run it from PowerShell in the repository root:

```powershell
.\windows\NativeCodex.ps1 doctor
.\windows\NativeCodex.ps1 build
.\windows\NativeCodex.ps1 test
.\windows\NativeCodex.ps1 gpu-test
.\windows\NativeCodex.ps1 acceptance
```

The wrapper uses one fixed MSYS2 MinGW-w64 lane by default: `C:\msys64\mingw64`
and `C:\msys64\usr\bin`. Set `RF_WINDOWS_MSYS2_ROOT` once if it is installed
elsewhere; do not mix another MinGW `bin` directory into PATH. SDL2 can come
from the repository staging directory or the native MSYS2 package
`mingw-w64-x86_64-SDL2`.

The wrapper also forces the Windows Makefile recipes through the matching
MSYS2 `sh.exe` and exports `/mingw64/bin:/usr/bin`; this is required when GNU
Make is launched from PowerShell rather than an MSYS2 terminal.

This target is intentionally separate from the Linux Toyc/self-hosting build.
It uses MinGW-w64 and static SDL2 to produce a Windows executable; Toyc does
not need to emit PE/COFF for this target.

```sh
# 国内网络可把 SDL2_SOURCE_URL 换成可访问的镜像或公司代理地址
SDL2_SOURCE_URL="https://your-mirror.example/SDL2-2.30.11.tar.gz" \
  ./scripts/setup-windows-build.sh

make win-rasterfall WINDOWS_DEPS="$PWD/.windows-deps"
make win-rasterfall-package WINDOWS_DEPS="$PWD/.windows-deps"
```

The first target incrementally produces `build-windows/rasterfall.exe` without linking
assets. The package target produces `build-windows/rasterfall-windows.zip`, containing
one EXE plus complete project-shaped public and private local-development asset
directories, including art sources, models, textures, and animations. The
program resolves those paths relative to its executable directory.

The installer caches the SDL2 archive and build under `.windows-deps`; rerun
it after an interrupted download to resume instead of starting over. If you
already have the Debian packages installed, it also skips `apt update`.

The Windows layer is selected only by the Windows target and implements the
window, audio, timing, socket, directory enumeration, thread, and synchronization
boundaries. Rasterfall uses an audio thread and a software-rendering worker pool;
Linux `clone`/futex/TLS code is not required by this target.
