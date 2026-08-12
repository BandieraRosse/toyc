#!/usr/bin/env bash
set -eu

# Install Linux-side tools for the isolated Windows Rasterfall build.
# The repository itself is not modified; dependencies go to .windows-deps.

# SDL2 source downloads can be slow or intermittently unreachable in
# some networks. Re-run this script to resume; alternatively set
# SDL2_SOURCE_URL to a mirror or use a VPN/proxy (aria2c is supported).

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WINDOWS_DEPS=${WINDOWS_DEPS:-"$ROOT/.windows-deps"}
SDL2_VERSION=${SDL2_VERSION:-2.30.11}
MINGW_TRIPLET=${MINGW_TRIPLET:-x86_64-w64-mingw32}
SDL2_SOURCE_URL=${SDL2_SOURCE_URL:-"https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/SDL2-$SDL2_VERSION.tar.gz"}
APT_PROXY=${APT_PROXY:-}

if ! command -v apt-get >/dev/null 2>&1; then
    echo "error: this installer currently supports Debian/Ubuntu only" >&2
    exit 1
fi

APT=${APT:-apt-get}
SUDO=${SUDO:-sudo}

packages="ca-certificates curl cmake ninja-build pkg-config \
gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64 mingw-w64-x86-64-dev"
packages="$packages g++-mingw-w64-x86-64"
missing=0
for package in $packages; do
    if ! dpkg-query -W -f='${Status}' "$package" 2>/dev/null |
       grep -q 'install ok installed'; then
        missing=1
        break
    fi
done

if [ "$missing" -eq 1 ]; then
    apt_options=()
    if [ -n "$APT_PROXY" ]; then
        apt_options+=("-o" "Acquire::http::Proxy=$APT_PROXY")
        apt_options+=("-o" "Acquire::https::Proxy=$APT_PROXY")
    fi
    $SUDO "$APT" "${apt_options[@]}" update
    $SUDO "$APT" "${apt_options[@]}" install -y $packages
else
    echo "APT dependencies already installed; skipping apt update."
fi

mkdir -p "$WINDOWS_DEPS/src" "$WINDOWS_DEPS/$MINGW_TRIPLET"
archive="$WINDOWS_DEPS/src/SDL2-$SDL2_VERSION.tar.gz"
source_dir="$WINDOWS_DEPS/src/SDL2-$SDL2_VERSION"
prefix="$WINDOWS_DEPS/$MINGW_TRIPLET"

if [ ! -d "$source_dir" ]; then
    if [ ! -f "$archive" ]; then
        partial="$archive.part"
        echo "Downloading SDL2 from: $SDL2_SOURCE_URL"
        if command -v aria2c >/dev/null 2>&1; then
            aria2c --continue=true --max-connection-per-server=16 \
                --split=16 --min-split-size=1M --file-allocation=none \
                --out="$(basename "$partial")" --dir="$(dirname "$partial")" \
                "$SDL2_SOURCE_URL"
        else
            curl -fL --retry 3 --retry-all-errors --continue-at - \
                "$SDL2_SOURCE_URL" -o "$partial"
        fi
        mv "$partial" "$archive"
    fi
    tar -xzf "$archive" -C "$WINDOWS_DEPS/src"
fi

if [ ! -f "$prefix/lib/libSDL2.a" ]; then
    # A previous configure may have cached the host C++ compiler. Remove only
    # this isolated SDL build directory so the toolchain file is re-evaluated.
    rm -rf "$WINDOWS_DEPS/build-sdl2"
    MINGW_TRIPLET="$MINGW_TRIPLET" cmake -S "$source_dir" \
        -B "$WINDOWS_DEPS/build-sdl2" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$ROOT/windows/cmake/mingw64.cmake" \
        -DCMAKE_INSTALL_PREFIX="$prefix" -DSDL_SHARED=OFF \
        -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_INSTALL_TESTS=OFF
    cmake --build "$WINDOWS_DEPS/build-sdl2"
    cmake --install "$WINDOWS_DEPS/build-sdl2"
fi

echo "Windows build dependencies are ready: $WINDOWS_DEPS"
