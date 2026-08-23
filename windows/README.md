# Windows Rasterfall build

This target is intentionally separate from the Linux Toyc/self-hosting build.
It uses MinGW-w64 and static SDL2 to produce a Windows executable; Toyc does
not need to emit PE/COFF for this target.

```sh
# 国内网络可把 SDL2_SOURCE_URL 换成可访问的镜像或公司代理地址
SDL2_SOURCE_URL="https://your-mirror.example/SDL2-2.30.11.tar.gz" \
  ./scripts/setup-windows-build.sh

make win-rasterfall WINDOWS_DEPS="$PWD/.windows-deps"
```

The result will be `build/rasterfall.exe`. Existing `make`, `make
self-*`, and test targets continue to use `build/` and are unaffected.

The Windows target is the single-file package build. It embeds
`rasterfall/assets/` and, when present, runtime files from
`rasterfall/private-assets/` into `build/rasterfall.exe`; no external resource
directory or particular working directory is required.

The installer caches the SDL2 archive and build under `.windows-deps`; rerun
it after an interrupted download to resume instead of starting over. If you
already have the Debian packages installed, it also skips `apt update`.

The Windows layer is selected only by the Windows target and implements the
window, audio, timing, socket, directory enumeration, thread, and synchronization
boundaries. Rasterfall uses an audio thread and a software-rendering worker pool;
Linux `clone`/futex/TLS code is not required by this target.
