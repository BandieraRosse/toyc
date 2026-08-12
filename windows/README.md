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

The installer caches the SDL2 archive and build under `.windows-deps`; rerun
it after an interrupted download to resume instead of starting over. If you
already have the Debian packages installed, it also skips `apt update`.

The Windows layer is selected only by the Windows target and implements the
existing window, audio, timing, and socket boundaries. The first milestone
uses one render thread so Linux `clone`/futex/TLS code does not need to be
ported before the game is playable.
