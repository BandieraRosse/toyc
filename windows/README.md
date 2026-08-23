# Windows Rasterfall build

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

The first target incrementally produces `build/rasterfall.exe` without linking
assets. The package target produces `build/rasterfall-windows.zip`, containing
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
