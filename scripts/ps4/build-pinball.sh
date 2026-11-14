#!/usr/bin/env bash
# Space Cadet Pinball for the PS4 (ps4-kit). Run in WSL:
#   scripts/ps4/build-pinball.sh deps              SDL2 + SDL2_mixer for the PS4 (once)
#   PINBALL_VERSION=00.01 scripts/ps4/build-pinball.sh     build + package out/SpaceCadetPinball-iHaiDeeZ-vXX.YY.pkg
#
#   ~/scp-build/src/SDL2         SDL 2.32.10 + patches/sdl2-ps4.patch
#   ~/scp-build/src/SDL2_mixer   SDL_mixer 2.8.1: WAV (the table's sounds) + MIDI on its built-in Timidity
#   ~/scp-build/timidity         freepats (Debian's GM patch set) + timidity.cfg -> /app0/timidity
#   ~/scp-build/sysroot          both, installed
#   ~/scp-build/data             optional: the game's data (PINBALL.DAT, *.WAV, ... or CADET.DAT for
#                                Full Tilt); packaged into /app0 when present. Without it the game
#                                reads /data/SpaceCadetPinball on the console.
# Then from Windows (Git Bash): ~/ps4-kit/scripts/upload.sh out/SpaceCadetPinball-iHaiDeeZ-vXX.YY.pkg <console-ip>
set -euo pipefail
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
source "$HOME/ps4-kit/toolchain/env.sh"
SCP="$HOME/scp-build"
SYSROOT="$SCP/sysroot"
B="$SCP/build"
VERSION="${PINBALL_VERSION:-00.00}"
TITLE_ID=SCPB00001
export PATH="$PSB/host-tools/bin:$PATH"
mkdir -p "$SCP/logs"
need cmake ninja

# The libraries are configured with the deps toolchain in scripts/ps4/toolchain (from the Conker port:
# configure checks link real PS4 programs); the kit's toolchain fails SDL's pthread check. Same clang 21
# + libc++ 21, static C libraries. DEPS_SYSROOT holds its libc++ and runtime (libdolphinps4_rt.a).
# (copied to a path without spaces: its linker-script flag breaks on one)
mkdir -p "$SCP/toolchain"
cp "$HERE/scripts/ps4/toolchain/ps4-deps.cmake" "$HERE/scripts/ps4/toolchain/ps4-link.x" "$SCP/toolchain/"
CONKER_TC="$SCP/toolchain/ps4-deps.cmake"
DEPS_SYSROOT="${DEPS_SYSROOT:-$HOME/cbfd-build/sysroot}"
deps_cmake() {
    env -u CFLAGS -u CXXFLAGS -u CPPFLAGS -u LDFLAGS -u LIBS PS4_SYSROOT="$DEPS_SYSROOT" \
    cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE="$CONKER_TC" -DPS4_TRY_LINK=ON -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_INSTALL_PREFIX="$SYSROOT" "$@"
}

fetch_sources() {
    mkdir -p "$SCP/src"
    if [ ! -d "$SCP/src/SDL2" ]; then
        git clone -q --depth 1 -b release-2.32.10 https://github.com/libsdl-org/SDL.git "$SCP/src/SDL2"
        git -C "$SCP/src/SDL2" apply "$HERE/patches/sdl2-ps4.patch"
    fi
    [ -d "$SCP/src/SDL2_mixer" ] || git clone -q --depth 1 -b release-2.8.1 https://github.com/libsdl-org/SDL_mixer.git "$SCP/src/SDL2_mixer"
}

build_sdl2() {
    local S="$SCP/src/SDL2" BD="$SCP/build-sdl2"
    fetch_sources
    rm -rf "$BD"
    # The dummy video driver (its window surface goes to VideoOut through SDL_PS4_PresentFramebuffer)
    # and the software renderer; sound through the sceAudioOut driver; pads are virtual joysticks.
    deps_cmake -S "$S" -B "$BD" \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DSDL2_DISABLE_SDL2MAIN=ON -DSDL_STATIC_PIC=ON \
        -DSDL_LOADSO=OFF -DSDL_RPATH=OFF \
        -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_VULKAN=OFF \
        -DSDL_DIRECTFB=OFF -DSDL_RPI=OFF -DSDL_VIVANTE=OFF -DSDL_OFFSCREEN=OFF -DSDL_METAL=OFF \
        -DSDL_RENDER=ON -DSDL_DUMMYVIDEO=ON -DSDL_DUMMYAUDIO=ON -DSDL_PS4AUDIO=ON -DSDL_DISKAUDIO=OFF \
        -DSDL_OSS=OFF -DSDL_ALSA=OFF -DSDL_JACK=OFF -DSDL_PIPEWIRE=OFF -DSDL_PULSEAUDIO=OFF -DSDL_SNDIO=OFF \
        -DSDL_ESD=OFF -DSDL_ARTS=OFF -DSDL_NAS=OFF -DSDL_FUSIONSOUND=OFF \
        -DSDL_HIDAPI=OFF -DSDL_HIDAPI_JOYSTICK=OFF -DSDL_VIRTUAL_JOYSTICK=ON -DSDL_JOYSTICK=ON -DSDL_HAPTIC=ON \
        -DSDL_SENSOR=ON -DSDL_POWER=OFF -DSDL_LIBUDEV=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_LIBSAMPLERATE=OFF \
        -DSDL_SYSTEM_ICONV=OFF -DSDL_LIBICONV=OFF \
        -DSDL_PTHREADS=ON -DSDL_PTHREADS_SEM=ON -DSDL_CLOCK_GETTIME=ON -DSDL_SSE3=ON -DSDL_3DNOW=OFF \
        > "$SCP/logs/sdl2-configure.log" 2>&1 || { tail -40 "$SCP/logs/sdl2-configure.log"; exit 1; }
    ninja -C "$BD" > "$SCP/logs/sdl2-build.log" 2>&1 || { grep -E "error|undefined" "$SCP/logs/sdl2-build.log" | head -40; exit 1; }
    ninja -C "$BD" install > /dev/null
    # The static target asks for libm and -pthread, which ld.lld (called directly) rejects: both are
    # in the system libraries already.
    sed -i 's/\\\$<LINK_ONLY:m>;\\\$<LINK_ONLY:-pthread>;//' "$SYSROOT/lib/cmake/SDL2/SDL2staticTargets.cmake"
    grep -E "define SDL_(VIDEO_DRIVER_DUMMY|AUDIO_DRIVER_PS4|JOYSTICK_VIRTUAL|VIDEO_RENDER_SW)" "$SYSROOT/include/SDL2/SDL_config.h" || true
    ls -la "$SYSROOT/lib/libSDL2.a"
}

build_mixer() {
    local S="$SCP/src/SDL2_mixer" BD="$SCP/build-mixer"
    fetch_sources
    rm -rf "$BD"
    deps_cmake -S "$S" -B "$BD" -DBUILD_SHARED_LIBS=OFF -DSDL2MIXER_SAMPLES=OFF -DSDL2MIXER_VENDORED=OFF \
        -DSDL2MIXER_DEPS_SHARED=OFF -DSDL2MIXER_WAVE=ON -DSDL2MIXER_MIDI=ON -DSDL2MIXER_MIDI_TIMIDITY=ON \
        -DSDL2MIXER_MIDI_FLUIDSYNTH=OFF -DSDL2MIXER_MIDI_NATIVE=OFF -DSDL2MIXER_MOD=OFF \
        -DSDL2MIXER_MP3=OFF -DSDL2MIXER_FLAC=OFF -DSDL2MIXER_OPUS=OFF -DSDL2MIXER_VORBIS=OFF \
        -DSDL2MIXER_WAVPACK=OFF -DSDL2MIXER_GME=OFF -DSDL2MIXER_CMD=OFF -DSDL2MIXER_INSTALL=ON \
        -DSDL2_DIR="$SYSROOT/lib/cmake/SDL2" \
        > "$SCP/logs/mixer-configure.log" 2>&1 || { tail -40 "$SCP/logs/mixer-configure.log"; exit 1; }
    ninja -C "$BD" > "$SCP/logs/mixer-build.log" 2>&1 || { grep -E "error|undefined" "$SCP/logs/mixer-build.log" | head -40; exit 1; }
    ninja -C "$BD" install > /dev/null
    ls -la "$SYSROOT/lib/libSDL2_mixer.a"
}

# The music's instruments: Debian's freepats (GPL), with every General MIDI program it lacks filled by
# the nearest instrument of its family (scripts/ps4/make_timidity_cfg.py).
build_timidity() {
    local DL="$SCP/dl"
    mkdir -p "$DL"
    if [ ! -d "$DL/freepats" ]; then
        (cd "$DL" && apt-get download freepats >/dev/null && dpkg-deb -x freepats_*.deb freepats)
    fi
    rm -rf "$SCP/timidity"
    mkdir -p "$SCP/timidity"
    cp -r "$DL/freepats/usr/share/midi/freepats/Drum_000" "$DL/freepats/usr/share/midi/freepats/Tone_000" "$SCP/timidity/"
    cp "$DL/freepats/usr/share/doc/freepats/copyright" "$SCP/timidity/COPYING.freepats"
    python3 "$HERE/scripts/ps4/make_timidity_cfg.py" "$DL/freepats/etc/timidity/freepats.cfg" "$SCP/timidity/timidity.cfg"
}

if [ "${1:-}" = deps ]; then
    shift
    for d in "${@:-sdl2 mixer timidity}"; do for x in $d; do "build_$x"; done; done
    exit 0
fi

[ -f "$SYSROOT/lib/libSDL2_mixer.a" ] || { echo "run: $0 deps" >&2; exit 1; }
[ -f "$SCP/timidity/timidity.cfg" ] || { echo "run: $0 deps timidity" >&2; exit 1; }
[ "${1:-}" = clean ] && rm -rf "$B"

echo "== configure"
ps4_cmake -S "$HERE" -B "$B" -G Ninja -DPINBALL_PS4=ON -DPINBALL_PS4_VERSION="$VERSION" \
    -DPINBALL_PS4_SYSROOT="$SYSROOT" > "$SCP/logs/configure.log" 2>&1 || { tail -40 "$SCP/logs/configure.log"; exit 1; }
echo "== build"
ninja -C "$B" -j"$JOBS" > "$SCP/logs/build.log" 2>&1 \
    || { grep -E "error:|FAILED|undefined symbol|missing module" "$SCP/logs/build.log" | head -60; exit 1; }

echo "== package"
STAGE="$B/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/sce_sys"
cp "$B/SpaceCadetPinball_eboot/eboot.bin" "$STAGE/eboot.bin"
cp "$HERE/Platform/PS4/sce_sys/icon0.png" "$STAGE/sce_sys/"
[ -f "$HERE/Platform/PS4/sce_sys/pic1.png" ] && cp "$HERE/Platform/PS4/sce_sys/pic1.png" "$STAGE/sce_sys/"
cp -r "$SCP/timidity" "$STAGE/timidity"
# the menus' fonts (SIL OFL; static cuts by scripts/ps4/make_fonts.py)
mkdir -p "$STAGE/fonts"
cp "$HERE/Platform/PS4/fonts/"{Exo2-Regular,Exo2-SemiBold,Orbitron-Bold}.ttf "$HERE/Platform/PS4/fonts/"OFL-*.txt "$STAGE/fonts/"
# PINBALL_NO_DATA=1: a package to share (the game's files are Microsoft's: never in a public package)
if [ "${PINBALL_NO_DATA:-0}" != 1 ] && [ -d "$SCP/data" ] && [ -n "$(ls -A "$SCP/data")" ]; then
    echo "   game data from $SCP/data goes into the package"
    cp -r "$SCP/data/." "$STAGE/"
fi
PKG="$(bash "$PS4_KIT/scripts/make-pkg.sh" "$STAGE" "$TITLE_ID" "Space Cadet Pinball" "$VERSION" SPACECADETPINBALL "$PSB/out" | tail -1)"
mkdir -p "$HERE/out" "$SCP/oelf"
cp "$PKG" "$HERE/out/SpaceCadetPinball-iHaiDeeZ-v$VERSION.pkg"
cp "$B/SpaceCadetPinball" "$SCP/oelf/v$VERSION.elf"
ls -la "$HERE/out/SpaceCadetPinball-iHaiDeeZ-v$VERSION.pkg"
