# MMModern dependencies

MMModern is an independent SDL application. It does not use ScummVM's SDL
backend or instantiate its engine. The compatibility target reuses only the
Xeen archive/sprite code, the plain Nuked OPL3 and mt32emu cores, and the
Common, Graphics, and Image libraries from a separately configured ScummVM
build. Audio does not instantiate ScummVM's mixer or OSystem; `g_system`
remains null.

## bgfx rendering

The experimental fork uses bgfx for window presentation and indoor 3D rendering.
SDL2 still owns windowing, input and audio. The selected graphics backends are
Metal on macOS, Direct3D 11 on Windows and Vulkan on Linux; backend fallback is
disabled because each build embeds shaders for its selected backend.

[MMModernBgfx.cmake](../cmake/MMModernBgfx.cmake) fetches
`https://github.com/bkaradzic/bgfx.cmake.git` at
`655364897f1cde0ada1f9486e771c1de8b5bf4cd`. That build wrapper pins:

- bgfx: `a4f43eb7f671dc8f1f0443050c28f5eb14661983`
- bx: `ff68113202c71fa99a67831036500b2270ce313d`
- bimg: `388a5e637c63e472158c940317c68c735bca4795`

CMake 3.21 or newer and Git are required. The first configuration fetches these
sources into the build directory's `_deps`; subsequent builds reuse them.
Do not commit those downloaded sources or build products. Dependency targets
use their required C++20 standard; MMModern remains C++17.
bgfx, bx and bimg are BSD-2-Clause dependencies; distribution packages must
retain their copyright/license notices. The CMake wrapper is CC0.

Only bgfx and the `shaderc` tool are needed: dependency examples, tests, install
targets and unrelated tools are disabled. The texture shaders under
[shaders](../src/platform/sdl/shaders/) are compiled during the application
build and embedded as generated headers, using `metal`, `s_5_0` or `spirv`
profiles. A native host build is required for the shader compiler; cross-host
shader-tool builds are not configured.

On macOS, Apple's Metal/Cocoa frameworks are linked by bgfx's CMake target.
On Linux, the upstream build also requires X11/OpenGL development libraries
and `wayland-egl`, even though MMModern selects Vulkan at runtime. A working
Vulkan driver/loader is required to run that build. Windows uses the upstream
shader compiler's Direct3D dependencies. Only the native macOS application
build has been compiled for this migration; Windows and Linux builds and
visual verification remain unverified.

bgfx is built single-threaded so its frame submission/presentation completes
inline before the existing input-origin acquisition callbacks. All three
passes (world, UI/cursor composition, window) stay on the GPU. The original
indexed framebuffer and decoded original sprite textures are CPU-to-GPU
uploads; no GPU-to-CPU viewport transfer is used.

## Pinned ScummVM revision

The validated known-good ScummVM dependency is:

- upstream: `https://github.com/scummvm/scummvm.git`
- commit: `6814ee9ba54582f5b5adcffab49efbbd8f589edd`
- exact tag: none
- validation state: detached HEAD with an empty `git status --porcelain`
- validation date: 2026-09-07
- validation platform: Windows x86-64, MSYS2 UCRT64; since M113 also Linux
  x86-64 (Ubuntu 26.04, GCC 15.2)

Always check out the full commit SHA. A moving branch name such as `master` is
not a dependency version.

Use separate source and build directories. Their names and locations are not
fixed; pass them to MMModern through `SCUMMVM_SOURCE_DIR` and
`SCUMMVM_BUILD_DIR`. Do not build inside the ScummVM source checkout.

For example:

```sh
git clone https://github.com/scummvm/scummvm.git <scummvm-source>
git -C <scummvm-source> checkout --detach 6814ee9ba54582f5b5adcffab49efbbd8f589edd
git -C <scummvm-source> status --porcelain
mkdir <scummvm-build>
cd <scummvm-build>
```

The final status command must produce no output before validation begins.

## Toolchain and library versions

The pinned revision was validated on Windows with the toolchain below, and on
Linux with the [Linux toolchain](#linux-toolchain):

- MSYS2 UCRT64 on Windows x86-64
- GCC/G++ 16.2.0, MSYS2 Rev3, with binutils 2.47 (its `objcopy` does the
  [sprite filtering](#revision-sensitive-sprite-filtering))
- GNU Make 4.4.1, the MSYS `make` package, which runs ScummVM's `make -j4`
- CMake 4.4.2 with the `MSYS Makefiles` generator, and Ninja 1.13.2 for
  `build-rel`
- SDL2 2.32.10, linked through the shared `SDL2::SDL2` CMake target and
  `libSDL2.dll.a`; `SDL2.dll` is required at runtime
- zlib 1.3.2, linked through `ZLIB::ZLIB` and `libz.dll.a`
- libvorbis 1.3.7 and libogg 1.3.6 from MSYS2 UCRT64. CMake links
  `libvorbisfile.a`, `libvorbis.a`, then `libogg.a` explicitly for CD cue
  decoding. No ScummVM audio backend is linked and `g_system` stays null.
  `MMODERN_VORBIS_INCLUDE_DIR` and the three `MMODERN_*_LIBRARY` cache
  paths select these static artifacts through `find_library`; normal toolchain
  search paths apply, with no hard-coded MSYS2 installation directory.
- Git for Windows 2.56.0, which generates the
  [item catalog](#build-generated-english-item-catalog) and the dialog
  drawing include during configuration and every build through
  `cmake -P tools/GenerateXeenItemCatalog.cmake` (`find_package(Git)` is
  required; the build needs no PowerShell). The generator passes Windows
  paths to Git, so when another `git` (for example MSYS2's) would be found
  first, set `-DGIT_EXECUTABLE` to Git for Windows' `git.exe`.

CD audio tests require `MMODERN_XEEN_AUDIO_DIR` (defaulting to
`MMODERN_XEEN_DATA_DIR`) with `GAME1.INS`, `GAME2.INS` and `MUSIC/`.
Configuration rejects an absent source. Validation decodes every audio track;
missing or malformed tracks fail rather than skip. Runtime still accepts a
plain CD `GAME` copy, with timed speech and visible text when tracks are absent.

### Pinned MSYS2 package set

The validated toolchain is exactly this package set, as `pacman -Q` prints it:
every UCRT64 package the builds use, with their dependencies, plus the MSYS
`make`. [Continuous integration](#continuous-integration) installs these files
from `repo.msys2.org` by name and SHA-256
(`.github/ci/msys2-packages.txt`), so a toolchain update changes this list,
that file and the cached builds together. The maintainer's machine has
`msys2-runtime 3.6.10-3`. The run's MSYS2 base comes from the installer that
its `msys2/setup-msys2` version pins (v2.33.0: the 2026-09-27 base, checked
by SHA-256); the run logs that base's `msys2-runtime`.

```text
mingw-w64-ucrt-x86_64-SDL2 2.32.10-1
mingw-w64-ucrt-x86_64-binutils 2.47-3
mingw-w64-ucrt-x86_64-brotli 1.2.0-1
mingw-w64-ucrt-x86_64-bzip2 1.0.8-4
mingw-w64-ucrt-x86_64-c-ares 1.34.8-1
mingw-w64-ucrt-x86_64-ca-certificates 20260816-1
mingw-w64-ucrt-x86_64-cmake 4.4.2-2
mingw-w64-ucrt-x86_64-cppdap 1.65.r11.g6464cd7-1
mingw-w64-ucrt-x86_64-crt 14.0.0.r353.g6df76fa52-4
mingw-w64-ucrt-x86_64-curl 8.22.0-1
mingw-w64-ucrt-x86_64-expat 2.8.4-2
mingw-w64-ucrt-x86_64-gcc 16.2.0-3
mingw-w64-ucrt-x86_64-gcc-libs 16.2.0-3
mingw-w64-ucrt-x86_64-gettext-runtime 1.0-1
mingw-w64-ucrt-x86_64-gmp 6.3.0-2
mingw-w64-ucrt-x86_64-gnutls 3.8.13-3
mingw-w64-ucrt-x86_64-headers 14.0.0.r353.g6df76fa52-2
mingw-w64-ucrt-x86_64-isl 0.28-1
mingw-w64-ucrt-x86_64-jsoncpp 1.9.8-1
mingw-w64-ucrt-x86_64-libarchive 3.8.9-5
mingw-w64-ucrt-x86_64-libb2 0.98.1-3
mingw-w64-ucrt-x86_64-libffi 3.8.0-1
mingw-w64-ucrt-x86_64-libiconv 1.19-1
mingw-w64-ucrt-x86_64-libidn2 2.3.8-4
mingw-w64-ucrt-x86_64-libogg 1.3.6-1
mingw-w64-ucrt-x86_64-libpsl 0.21.5-3
mingw-w64-ucrt-x86_64-libssh2 1.11.1-2
mingw-w64-ucrt-x86_64-libtasn1 4.21.0-1
mingw-w64-ucrt-x86_64-libunistring 1.4.2-1
mingw-w64-ucrt-x86_64-libuv 1.52.1-1
mingw-w64-ucrt-x86_64-libvorbis 1.3.7-3
mingw-w64-ucrt-x86_64-libwinpthread 14.0.0.r353.g6df76fa52-2
mingw-w64-ucrt-x86_64-lz4 1.10.0-1
mingw-w64-ucrt-x86_64-mpc 1.4.1-1
mingw-w64-ucrt-x86_64-mpfr 4.2.2-3
mingw-w64-ucrt-x86_64-nettle 4.0-1
mingw-w64-ucrt-x86_64-nghttp2 1.70.0-1
mingw-w64-ucrt-x86_64-nghttp3 1.18.0-1
mingw-w64-ucrt-x86_64-ngtcp2 1.25.0-1
mingw-w64-ucrt-x86_64-ninja 1.13.2-1
mingw-w64-ucrt-x86_64-openssl 3.6.4-1
mingw-w64-ucrt-x86_64-p11-kit 0.26.5-1
mingw-w64-ucrt-x86_64-pcre2 10.48-3
mingw-w64-ucrt-x86_64-pkgconf 1~3.0.5-1
mingw-w64-ucrt-x86_64-rhash 1.4.6-1
mingw-w64-ucrt-x86_64-tzdata 2026c-1
mingw-w64-ucrt-x86_64-vulkan-loader 1~1.4.357.0-1
mingw-w64-ucrt-x86_64-windows-default-manifest 20260815-1
mingw-w64-ucrt-x86_64-wineditline 2.208-1
mingw-w64-ucrt-x86_64-winpthreads 14.0.0.r353.g6df76fa52-2
mingw-w64-ucrt-x86_64-xz 5.8.3-1
mingw-w64-ucrt-x86_64-zlib 1.3.2-2
mingw-w64-ucrt-x86_64-zstd 1.5.7-2
make 4.4.1-3
```

ScummVM's `configure` enables whatever optional libraries it finds. The
pinned build was configured before `libvorbis` and `libogg` were installed,
so its configuration has no Ogg or Vorbis support; install those two packages
only after configuring ScummVM, or reconfigure without them, to reproduce it.
Other UCRT64 packages on a development machine (for example the PNG, JPEG,
FreeType or FriBidi libraries) change that configuration the same way.

The ScummVM build uses GNU++11. MMModern uses GNU++17 and must use an
ABI-compatible compiler/toolchain when compiling ScummVM source directly and
linking the prebuilt ScummVM archives.

### Linux toolchain

Since M113 MMModern also builds natively on Linux x86-64 with GCC, GNU ld and
SDL2, without PowerShell or any Windows tool. It was validated on Ubuntu 26.04
(WSL and the [CI container](#continuous-integration)) with GCC/G++ 15.2.0,
binutils 2.46, CMake 4.2.3, Ninja 1.13.2, GNU Make 4.4.1, pkgconf 2.5.1,
SDL2 2.32.10, zlib 1.3.1, libvorbis 1.3.7, libogg 1.3.6 and Git 2.53.0. The
differences from Windows:

- Vorbis and Ogg come from the system's shared `vorbisfile` through
  `pkg-config`; no static `.a` is required.
- The platform libraries (`mmodern_platform_libs`) are empty, and the five
  `WIN32 UNICODE _UNICODE` sites take the ScummVM build's own define set
  instead (see [below](#scummvm-configuration-and-required-artifacts)).
- The sprite and Nuked OPL3 objects are not filtered by PE section names;
  every target that links the ScummVM bridge gets `--gc-sections`, so a
  reference to an engine-only symbol fails to link.
- `mmodern` is linked against the shared GCC runtime, as Linux programs
  normally are; the [AppImage](#continuous-integration) links it
  statically.

### Pinned Ubuntu package set

On Ubuntu 26.04 these packages, as `dpkg-query` prints them, are the ones a
clean machine installs (with `--no-install-recommends`) to build and test
MMModern. [Continuous integration](#continuous-integration) asks for exactly
these versions from `snapshot.ubuntu.com` at a fixed timestamp and requires
the complete resolved set of `.github/ci/linux-packages.txt`, so this list,
that file and the cached builds change together. `curl` is there only
because ScummVM's configure sets `USE_CURL` when the `curl` command exists,
as on the maintainer's machine; libcurl's development package is
deliberately absent: with it, ScummVM's configure would enable networking and
change the recorded configuration.

```text
binutils 2.46-3ubuntu2
ca-certificates 20260816ubuntu0.26.04.1
cmake 4.2.3-2ubuntu2
curl 8.18.0-1ubuntu2.8
g++ 4:15.2.0-5ubuntu1
git 1:2.53.0-1ubuntu1.1
libogg-dev:amd64 1.3.6-2
libsdl2-dev:amd64 2.32.10+dfsg-6
libvorbis-dev:amd64 1.3.7-3build2
make 4.4.1-3
ninja-build 1.13.2-1
pkgconf:amd64 2.5.1-4
zlib1g-dev:amd64 1:1.3.dfsg+really1.3.1-1ubuntu3.2
```

## ScummVM configuration and required artifacts

From an MSYS2 UCRT64 shell in the separate ScummVM build directory, configure
with:

```sh
SDL_CONFIG=/ucrt64/bin/sdl2-config \
  <scummvm-source>/configure \
  --backend=sdl \
  --disable-all-engines \
  --disable-detection-full
```

On Linux, from any shell in the build directory, configure with:

```sh
<scummvm-source>/configure \
  --backend=sdl \
  --disable-all-engines \
  --disable-detection-full \
  --disable-ogg --disable-vorbis
```

On macOS, configure with:

```sh
SDL_CONFIG="$(command -v sdl2-config)" \
  <scummvm-source>/configure \
  --backend=sdl \
  --disable-all-engines \
  --disable-detection-full \
  --disable-ogg --disable-vorbis \
  --disable-jpeg --disable-png
```

Disabling Ogg and Vorbis keeps them out of ScummVM's configuration
whatever the machine has installed, as the Windows configure order does;
MMModern still links the system's `vorbisfile` for CD speech. The macOS
configuration also disables JPEG and PNG: ScummVM's Image archive otherwise
introduces link dependencies MMModern does not use. The `USE_` set for Linux
is recorded in `.github/ci/scummvm-use-linux.txt`. Compared with Windows it
adds ALSA, the ALSA sequencer MIDI, sndio and Linux CD and drops libcurl, its
networking options, the taskbar, system dialogs and text-to-speech. MMModern's
configure reads the `-D` entries of `config.mk`'s
`DEFINES` (`SDL_BACKEND`, `USE_SDL2`, `POSIX`, `HAS_POSIX_SPAWN`,
`HAS_FSEEKO_OFFT_64`, `DATA_PATH` and `PLUGIN_DIRECTORY`), adds
`HAVE_CONFIG_H`, compiles the bridge objects with them and refuses any set
other than `.github/ci/scummvm-defines-linux.txt`. macOS has its own exact
allowlist in `.github/ci/scummvm-defines-macos.txt`.
`.github/ci/scummvm.sh` holds both configure commands and chooses by
`uname -s`.

Only four ScummVM build artifacts are required. Build them directly instead of
building ScummVM or its engines:

```sh
make -j4 \
  image/libimage.a \
  graphics/libgraphics.a \
  common/formats/po_parser.o \
  common/libcommon.a
```

MMModern checks for and links exactly these files from `SCUMMVM_BUILD_DIR`:

- `image/libimage.a`
- `graphics/libgraphics.a`
- `common/formats/po_parser.o`
- `common/libcommon.a`

`SCUMMVM_BUILD_DIR/config.h` is also required as evidence that the external
tree was configured.

MMModern compiles these ScummVM source files directly from
`SCUMMVM_SOURCE_DIR`:

- `engines/mm/shared/xeen/cc_archive.cpp`
- `engines/mm/shared/xeen/sprites.cpp`
- `audio/softsynth/opl/nuked.cpp`
- the mt32emu 2.7.0 core under `audio/softsynth/mt32`, listed below

The sprite source is compiled into a generated private object and filtered as
described below. The compatibility code includes the corresponding Xeen
archive, sprite, surface, and file headers as well as their transitive Common
and Graphics headers.

### Nuked OPL3 core

`cmake/ScummVmXeen.cmake` compiles the pinned `nuked.cpp` with GNU++11,
`-O2 -ffunction-sections -fdata-sections`, then objcopy removes sections
matching `*3OPL5NUKED3OPL*` (the class adapter and thunks), `*5Audio*`,
`*_ZT*` (RTTI/vtables) and `*refptr*`. The generated `nuked.core.o` retains
the plain core functions and tables. No source patch, ScummVM mixer, backend
or additional audio translation unit is required. The unused undefined symbol
names left in the object have no relocations from retained sections.

`XeenOpl3` owns only the plain chip struct. It calls `OPL3_Reset` at native
49716 Hz, `OPL3_WriteRegBuffered` (the pin's two-sample spacing between
writes, as its `OPL::writeReg` uses; see
[original quirks](original-quirks.md#promus-write-spacing)), and
`OPL3_Generate`. No stereo-extension
define is supplied; the header's default is zero. MMModern's promus interpreter
places interrupts using the exact ratio `49716 * 16390 / 1193182`. The SDL
device opens at the default output's mix rate (`SDL_GetDefaultAudioInfo`,
48000 when unreported) with no allowed changes. `XeenAudio` mixes FM through
an `SDL_AudioStream` from 49716 Hz and unsigned mono VOC through an independent
exact rational resampler from `1000000/68` Hz; letting SDL accept 49716 made its
WASAPI path resample internally and drop ~9 ms blocks. During play the main
thread probes SDL's current default name/rate every 250 ms and reopens when
either changes, even when WASAPI followed the default internally. Removal
events for the opened output also reopen the default. Close joins the old
callback before resampler recreation; source state and VOC phase survive rate
changes. Recreating FM/CD conversion discards its small queued tail; one small
discontinuity is accepted (maintainer, 2026-10-22). Physical removal leaves
digital/CD silent until their next start while continuing the current song.
Callback/conversion errors, a bare backend disconnection and failed recovery
silence output with one notice; gameplay continues.

FM/CD conversion uses M82's 512-native-frame refills, with fixed refill
buffers and SDL conversion storage prepared before callbacks. It uses only
the public stream API; no private padding constant, history replay or whole
rate-ratio buffering is required. Fixed FM/CD gain precedes resampling and
its S16 clamp. Dependency changes must revalidate callback allocation,
effect latency, rate transitions and callback-size independence with
`sdl_audio_device`, `xeen_digital_audio` and `xeen_cd_audio`.
The maintainer's M83 Part C Windows play-test passed on 2026-10-22.

A timeline interpreter advances by an exact integer
native-sample accumulator on the presentation clock and alone answers the game;
a render interpreter in the FM source ticks from the device's own rendered
samples and receives the timeline's operations through one queue (M82 C3), so
output never waits for the game loop and never delays the game or its fade
waits.
Title/SDL and gameplay/injected clocks may have different epochs. Explicit
session handoffs consume the outgoing clock's final interval, then rebase the
source counter while retaining elapsed driver time and fractional phase;
ordinary backwards movement within a source is rejected.

The retained core is Nuked-OPL3-fast 1.8-fast.2 (upstream 1.8, cfedb09),
copyright Nuke.YKT (2013-2020) and Tony Gies (2026), licensed LGPL-2.1-or-later.
Preserve these notices and the pinned corresponding source; releases include
the pin's `LICENSES/COPYING.LGPL` alongside the GPL and runtime-library notices.
MMModern's existing ScummVM adaptations remain GPL-3.0-or-later.

### MT-32 core and player ROMs

Music device 4 uses the pinned `audio/softsynth/mt32` core directly. CMake
compiles `Analog`, `BReverbModel`, `Display`, `File`, `LA32FloatWaveGenerator`,
`LA32Ramp`, `LA32WaveGenerator`, `Part`, `Partial`, `PartialManager`, `Poly`,
`ROMInfo`, `Synth`, `Tables`, `TVA`, `TVF`, `TVP`, and `sha1/sha1.cpp` from
that directory. The sole ScummVM utility dependency is `common/util.h` for
`ARRAYSIZE`; no ScummVM MIDI backend, OSystem, configuration manager or host
MIDI device is used. The mt32emu 2.7.0 core is copyright Dean Beeler, Jerome
Fisher and Sergey V. Mikayev, LGPL-2.1-or-later. Its SHA-1 implementation is
copyright Micael Hildenborg, BSD-3-Clause; releases retain its full notice and
the LGPL license alongside the pinned corresponding source reference.
The adapter observes the pinned core's MIDI queue read cursor to account for
events actually consumed, including notes delayed by partial aborts. It uses
explicit-instantiation member access limited to the synth's queue pointer and
the queue's read cursor and mask; the public API exposes neither the cursor
nor the pending count.
The renderer uses 48 kHz accurate analogue output, the 16-bit renderer,
immediate MIDI mode and the reference capture's nice DAC, ramp, panning and
partial-mixing settings.

Players supply the MT-32 control ROM 1.07 and MT-32 PCM ROM in
`%LOCALAPPDATA%\MMModern\roms`. mt32emu identifies their contents, so file
names are unrestricted; matching split ROM halves are also accepted. MMModern
does not bundle or copy ROM files. A missing, incomplete or different set
produces one not-supported notice and silent MT-32 output without changing
logical timing. For original-data tests, set `MMODERN_MT32_ROM_DIR` to an
external ROM directory and `MMODERN_M94_CAPTURES_DIR` to the external M94
capture root; neither belongs in the repository or a release package.

The external DRO v2 validation data is configured with
`-DMMODERN_PROMUS_CAPTURES_DIR=<capture-directory>`, alongside
`MMODERN_XEEN_DATA_DIR`. Configuration does not check it, but `xeen_promus`, and
so the complete suite, needs six captures there: `promus-startup.dro`,
`promus-inn.dro` (the Tavern song), `promus-outdoors.dro`,
`promus-fx20-alone.dro`, `promus-wall-bump.dro` (effect 46) and
`promus-fx20-over-music.dro` (effect 20 over town music). Captures and derived
register streams are never bundled.
The M94 tests also require `-DMMODERN_M94_CAPTURES_DIR=<directory>`, containing
`admus`, `blastmus` and `specmus` subdirectories, each with `rawopl0001.dro`,
and `rolmus/midi0001.mid`. These captures remain external to the repository.
On the development machine the directory is
`D:/Projetos/MModern/MModern-re/m94/captures-0c`.
The comparison removes DOSBox's initial register snapshot and unchanged writes,
aligns the sustained song/effect suffix and PIT phase, and compares both banks
through capture end. The startup's EXE intro is outside M82.

## `xeen_probe` provenance and replacement

The official pinned revision does not contain `devtools/xeen_probe`.

The earlier unversioned local ScummVM snapshot contained exactly two additions
under that directory:

- `module.mk` (SHA-256
  `b7e15826f0b64018ba67271b840e4498b4871a660e4505bdb3afa77fff3f9f4c`)
- `xeen_probe.cpp` (SHA-256
  `4bac641530fd016b768408d8227ea7236257726f1a01c27fbbeeaa68da00b5c4`)

Their upstream provenance could not be established, so they are not part of
the pinned dependency and must not be copied silently into a clean ScummVM
checkout. No source patch is required to prepare MMModern's dependency: the
direct four-target `make` command above is the MMModern-specific build helper
procedure and produces everything the integration consumes while leaving the
pinned checkout clean.

## Revision-sensitive sprite filtering

`cmake/ScummVmXeen.cmake` compiles `sprites.cpp` with
`-ffunction-sections -fdata-sections`, then uses GNU `objcopy` to remove only
the unused engine-specific `Common::Path` constructor/loader and associated
`MM::Shared::Xeen::File` destructor, thunk, VTT, and vtable-reference sections.
The stream decoder and sprite drawing implementation remain intact.

This filtering depends on ScummVM class/signature layout, GNU C++ mangled
names, the MinGW ABI, and PE/COFF section naming (`.text$`, `.xdata$`,
`.pdata$`, `.debug_frame$`, and `.rdata$.refptr`). It is therefore explicitly
revision-sensitive. A ScummVM revision change must verify that every named
section still has the intended meaning and that no engine-only unresolved
references remain; successful compilation alone is not sufficient.

No file in the pinned ScummVM source checkout is modified by this process.

## Read-only CD data and DOS dialogs

The English World of Xeen two-CD edition supplies the runtime data. Set
`MMODERN_XEEN_DATA_DIR` to its GOG installation root, a CD `GAME` directory,
or a parent containing `GAME`. Original-data tests are required when
`BUILD_TESTING` is enabled: a missing configured source fails configuration,
and an incomplete or malformed source fails validation rather than removing tests.
`MMODERN_XEEN_UI_DATA` optionally supplies the installed uncompressed English
`XEEN.DAT` for tests using a CD copy. The application receives that path through
`--ui-data <path>`; build settings do not supply runtime paths.

At a GOG root, cue metadata in `.INS` files locates the MODE1/2352 data track.
MMModern reads ISO9660 `/GAME/XEEN.CC`, `DARK.CC` and `INTRO.CC` through bounded
logical streams, with the unchanged pinned CC index/name decoding. One complete
disc suffices; byte-identical complete candidates are selected deterministically,
and conflicting or ambiguous candidates refuse startup. Co-located root floppy
archives are never used. A pre-existing plain `GAME` copy must contain all three
archives; MMModern never creates a copy, mounts an image or executes a game module.

The ISO does not contain the UI module. GOG discovery uses `WORLD/XEEN.DAT`;
a plain CD copy needs an unambiguous `WORLD` sibling or explicit `--ui-data`.
The installed `XEEN.CFG` must sit beside the UI module, or startup fails;
MMModern reads it and never creates or writes it. `XEEN.EXE` in the same folder
is read, never executed, for the Clouds and Darkside intros and the Clouds
ending; when it is missing or different, those do not play and the game
continues.
The bounded DOS reader validates the MZ load image, relocations, named field
bounds, far-pointer tables, terminators, format arguments and control streams.
Packed root DATs and unsupported layouts fail clearly. Dialogs, item/service
screens and Rest retain the DOS bytes, including the single alignment `r` in
Charges. The title, credits, control panel and save/load chooser also read
their fields from the module.

`generated/XeenDialogDrawing.inc` retains only the pinned numeric window
symbols and four-shade text palettes. `-DialogDrawing` generates this private
build include from the verified constants blob; text is read at runtime from DAT.
The separate item catalog keeps its existing pinned provenance below. No DOS
strings or original-data fixtures are distributed.

The current save format is named by `kJourneyVersion`, `kJourneySchema` and
`kJourneyContent` in `src/formats/xeen/XeenSaveFormat.h`. Archive fingerprints hash
size and CRC32 over logical encrypted CC bytes, so both discs and equal plain
copies have identical signatures. Floppy-bound saves fail the data-edition check
before restoration; signatures and resource-relative overlays are never translated.
Save paths must stay outside every resolved commercial source directory, including
separately supplied image and UI-module directories.

## Build-generated English item catalog

Milestone 24A derives the embedded English catalog directly from one immutable
object in the pinned ScummVM repository:

- path `devtools/create_mm/files/xeen/CONSTANTS_7`;
- Git blob `455b2eb3900a60be910e4d045d103a73586e73b0`;
- 35,065 bytes; SHA-256
  `a3022d378e7570a56332f30128942afe02ae2bfdef70c307b2de997eb07a9e77`.

ScummVM's `LangConstants::writeConstants` produces this tracked artifact from
the language getters. At the pinned revision it writes `ITEM_BROKEN`,
`ITEM_CURSED`, `ITEM_OF`, then the six required arrays with counts
7/41/14/11/22/74. MMModern parses only that bounded block, byte range
`[20680,22438)`, rather than the complete unversioned constants stream.

`tools/GenerateXeenItemCatalog.cmake`, run with `cmake -P` on every
platform, requires exact checkout HEAD, exact tree
entry mode/path/blob ID, size and independent SHA-256. It then reads the blob
through `git cat-file`, never through the ScummVM worktree, into a file
whose SHA-256 is checked before its bytes are decoded. Every inherited
`GIT_*` variable (matched without regard to case), replacement objects and
global/system Git configuration are removed from the plumbing process.
Only the verified blob supplies catalog bytes; mutable worktree files and
compiler inputs are outside the generation boundary.

No ScummVM header is compiled and no compiler preprocessing participates in
catalog generation.

The generator validates the three nonempty scalar strings, exact array tags and
counts, reserved/required entries, the fixed block end, a 63-byte token limit and
the 16,384-byte aggregate output limit. It emits deterministic fixed-width octal
escapes with schema 1, English language ID 7 and the exact revision marker.
Both outputs, the catalog and the dialog drawing include, are pinned by
SHA-256. A changed output is published from a sibling temporary file with
`cmake -E rename`; a failed rename preserves the previous destination and
removes the temporary file, and unchanged output is not rewritten, so builds
do not recompile. The private include remains
ignored, build-tree-only and unavailable as runtime or installed companion data.

Generation runs during configuration and on every build, so a wrong repository
revision or object identity fails immediately and stale generated output cannot
bypass the gate. The build-only generator uses CMake and Git only and adds no
application runtime dependency. Cross-compilation remains unsupported.

The embedded source-derived names are distinct from commercial material text.
At runtime MMModern reads only `mae.xen` through the selected installation's
`DARK.CC`; it does not load ScummVM's `mm.dat`, the generated include, the ScummVM
source tree, a working-directory catalog or an environment-selected companion.
The parser accepts exactly 131 bounded NUL-terminated entries and publishes no
partial table on missing, malformed or failed reads. Such failures retain bounded
base-name/numeric fallback behavior and do not prevent startup.

All archive roles use the same bounded read-only source and pinned CC decoder.
The bridge validates the complete index and member extents before upstream
short-read paths, rechecks the retained index on reopening, then reads and XOR
decodes the requested outer payload exactly once. A missing optional member is
`Missing`, structurally invalid readable bytes are `Malformed`, and invalid
extents or read failures are `ReadError`. Sprite reconstruction compares admitted
bytes under physical source, role and resource ID, including DARK and INTRO.

The normal configure/build commands below reproduce the adapter. Its focused
validation is:

```sh
cmake --build <mmodern-build> --parallel 4 --target \
  mmodern_item_catalog_data mmodern_item_catalog_smoke
ctest --test-dir <mmodern-build> --output-on-failure \
  -R '^xeen_item_catalog($|_)'
<mmodern-build>/mmodern_item_catalog_smoke.exe <game-directory>
<mmodern-build>/mmodern_item_catalog_smoke.exe --verify-reference \
  <game-directory> <scummvm-source>
```

The pinned blob and generated text remain ScummVM GPLv3-or-later-derived material.
Preserve the upstream license notices, contributor attribution, exact revision,
blob/path identity and corresponding source availability in distributions.
Commercial `DARK.CC/mae.xen`, extracted `mae.cld`, original archives and
original-data fixtures remain external and must not be bundled.

## Bounded equipment restriction data

`XeenEquipment.cpp` adapts 34 weapon and eight armor decimal masks from pinned
ScummVM `6814ee9ba54582f5b5adcffab49efbbd8f589edd`,
`devtools/create_mm/create_xeen/constants.cpp`,
`LangConstants::ITEM_RESTRICTIONS` and `RESTRICTION_OFFSETS`. Private constexpr
uint8 arrays use validated ID minus one; their entries correspond to upstream
index `id` for Weapons 1..34 and `id+35` for Armor 1..8. No other masks,
generated catalog changes or runtime reference-table dependency are introduced.
The bounded equipment and frame contracts belong in the
[M25 specification](archive/milestone-25-plan.md#equipment-domains-and-frame-contract).

The predicates derive from `engines/mm/xeen/item.cpp`,
`InventoryItems::passRestrictions/removeItem` and
`WeaponItems/ArmorItems/AccessoryItems::equipItem` at that same revision.
This adapted numeric data and logic remain ScummVM GPLv3-or-later-derived
material, attributed to the ScummVM developers listed in upstream `COPYRIGHT`.
Preserve that attribution and license, and supply the corresponding adapted
source and pinned upstream source in distributions. Commercial resources are
not part of these tables.

## Bounded item table provenance

`src/games/xeen/detail/XeenItemTables.h` adapts numeric tables from
`devtools/create_mm/create_xeen/constants.cpp` at pinned revision
`6814ee9ba54582f5b5adcffab49efbbd8f589edd`: the weapon, armor, accessory and
misc base costs, the metal, elemental, attribute and resistance material
tables, and the weapon damage, armor class, misc spell and category tables that
combat and the item dialogs also read. `xeenItemPrice` in
`XeenItemServiceRules.cpp` prices Buy, Sell, Identify and Fix for all four
categories from them, adapting the material adjustments, action divisors,
integer division and minimum price of `ItemsDialog::calcItemCost` in
`engines/mm/xeen/dialogs/dialogs_items.cpp` at the same pin; attribute
materials follow the original program instead
([original quirks](original-quirks.md#sv-attribute-material-price)).
`XeenArmorRepair.h` applies those prices with Fix's broken-bit rule and also
carries the thirteen armor base costs; `XeenMerchantTables.h` adapts the
stock-generation tables of `Character::makeItem` at the same pin. These
adaptations are GPL-3.0-or-later, attributed to the ScummVM developers in
upstream `COPYRIGHT`, without warranty; preserve their notices and
corresponding source in redistribution. No commercial resource is embedded.
The Smith dispatch and browsing contracts are in
[M38](archive/milestone-38-plan.md) and [M42](archive/milestone-42-plan.md).

## Bounded actor interpretation and approach provenance

The 26A parser and approach adaptation use ScummVM
`6814ee9ba54582f5b5adcffab49efbbd8f589edd`: Xeen `map.cpp`
(`MonsterStruct::synchronize`, `MonsterObjectData::synchronize`), `party.cpp`
(`Party::synchronize/changeTime`), `interface.cpp` (`chargeStep`, `stepTime`,
`perform`, `draw3d`), `combat.cpp` (`moveMonsters`, `canMonsterMove`,
`moveMonster`, movement grids), `combat.h` (107-record capacity), and
`interface_scene.cpp` (`setOutdoorsMonsters`). The classifier reuses the existing
position constants sourced from `devtools/create_mm/create_xeen/constants.cpp`.
M26B's four bounded outdoor actor placements adapt selected-slot/order, anchor,
scale and clipping values from `interface_scene.cpp` (`OutdoorDrawList` and
`setOutdoorsMonsters`) at the same pin. `XeenOutdoorScene::actorCommands` emits
them through the existing ordered scene stream and sprite drawer.
This adapted logic remains GPL-3.0-or-later material attributed to the ScummVM
developers in upstream `COPYRIGHT`; distributions must preserve corresponding
source/provenance. It adds no engine linkage or copied commercial resource.

`readCloudsMonsterStatisticsFromDarkArchive` extends the existing lazy Dark
archive owner with a checked, explicitly named `xeen.mon` read. Its private
extent/read helper is shared only with `mae.xen`; the material size limit and
availability semantics remain unchanged. The 60-byte parser enforces the CC
65535-byte member bound separately. Missing data, malformed bytes and I/O failures
remain distinct. Actor admission/context/persistence contracts belong in
[M26](archive/milestone-26-plan.md#final-ownership-and-lifecycle).

## MMModern configuration and validation

Configure MMModern in its own new build directory with explicit dependency
paths:

```sh
cmake -S <mmodern-source> -B <mmodern-build> \
  -G "MSYS Makefiles" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSCUMMVM_SOURCE_DIR=<scummvm-source> \
  -DSCUMMVM_BUILD_DIR=<scummvm-build> \
  -DMMODERN_XEEN_DATA_DIR=<game-directory> \
  -DMMODERN_PROMUS_CAPTURES_DIR=<capture-directory> \
  -DMMODERN_M94_CAPTURES_DIR=<m94-capture-directory> \
  -DMMODERN_MT32_ROM_DIR=<mt32-rom-directory>
cmake --build <mmodern-build> --parallel 4
ctest --test-dir <mmodern-build> --output-on-failure
```

### macOS arm64 application build

From the MMModern source directory, with the pinned ScummVM source and its
separate build directory configured as above:

```sh
cmake -S . -B build-macos \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSCUMMVM_SOURCE_DIR=../scummvm-master \
  -DSCUMMVM_BUILD_DIR=../build-xeen-macos \
  -DMMODERN_ORIGINAL_DATA_TESTS=OFF
cmake --build build-macos --target mmodern --parallel 4
./build-macos/mmodern --version
./build-macos/mmodern --diagnostics "$(mktemp -d)"
```

### Optimized test build (`build-rel`)

`AGENTS.md` runs iteration (`ctest -L fast`) and the complete suite on
`build-rel`, an optimized build with debug information and assertions kept.
CMake's default RelWithDebInfo flags add `-DNDEBUG`, so override them. From the
MMModern source directory, with `C:\msys64\ucrt64\bin` on `PATH` (it provides
the compiler, Ninja, `objcopy` and `SDL2.dll`):

```sh
cmake -S . -B build-rel -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O2 -g" \
  -DSCUMMVM_SOURCE_DIR=<scummvm-source> \
  -DSCUMMVM_BUILD_DIR=<scummvm-build> \
  -DMMODERN_XEEN_DATA_DIR=<game-directory> \
  -DMMODERN_PROMUS_CAPTURES_DIR=<capture-directory> \
  -DMMODERN_M94_CAPTURES_DIR=<m94-capture-directory> \
  -DMMODERN_MT32_ROM_DIR=<mt32-rom-directory>
cmake --build build-rel
cmake --build build-rel --target mmodern_test_save   # optional tool, not built by default
ctest --test-dir build-rel -L fast -j 8 --output-on-failure
```

Add `-DGIT_EXECUTABLE=<path-to-Git-for-Windows>/cmd/git.exe` when another
`git` comes first on `PATH`, as described under the toolchain above.

`build-rel/build.ninja` must not contain `NDEBUG`. Outside Debug builds, test
and tool executables are linked with `-Wl,--strip-debug`: debug information is
about 97% of each one, and writing it dominated incremental builds (one source
change relinked 183 executables in 505 s). The generated code is unchanged and
`mmodern.exe` keeps its debug information. Configure with
`-DMMODERN_TEST_DEBUG_INFO=ON` to debug a test or tool in `build-rel`. The complete suite runs
through `tools/run-full-ctest.ps1 -BuildDir build-rel`, as described in
`tools/AGENTS.md`: explicit `-Jobs 1..256` overrides the machine's local
preference; without either, the runner uses half the logical processors,
at least 1 and at most 8 jobs. CMake runs the real-clock and native-window
tests alone and gives every test a load-independent hang-guard timeout. Play-tests and release
packages use `build-rel` as well; the Debug build above (`build-m44`) remains
for debugging only.

The local preference is `%LOCALAPPDATA%\MMModern\ctest-jobs.json`, shared
by this user's checkouts on the computer. For example, the maintainer's
measured and approved configuration is:

```json
{"computerName": "GCC", "jobs": 12}
```

Use the name reported by `[Environment]::MachineName` and a job count measured
on that machine. A preference for a different computer is ignored. Omitted
`-Jobs` or `-Jobs 0` uses the local preference or the conservative fallback;
`-Jobs 8`, for example, overrides it for one run. The iteration command
`ctest -L fast -j 8` is unchanged; the preference applies to the full-suite
runner only.

### Linux build under WSL (`build-linux-rel`)

On Linux, `build-linux-rel` is configured like `build-rel`, with the same
flags and data options, from a shell where the
[Ubuntu packages](#pinned-ubuntu-package-set) are installed:

```sh
cmake -S . -B build-linux-rel -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O2 -g" \
  -DSCUMMVM_SOURCE_DIR=<scummvm-source> \
  -DSCUMMVM_BUILD_DIR=<scummvm-build> \
  -DMMODERN_XEEN_DATA_DIR=<game-directory> \
  -DMMODERN_PROMUS_CAPTURES_DIR=<capture-directory> \
  -DMMODERN_M94_CAPTURES_DIR=<m94-capture-directory> \
  -DMMODERN_MT32_ROM_DIR=<mt32-rom-directory> \
  -DMMODERN_RU_DATA_DIR=<russian-folder>
cmake --build build-linux-rel
```

Test and tool executables are stripped as on Windows. The Windows-only tests
(`xeen_launcher` and its witness, which run PowerShell) are not registered.

On the maintainer's machine the Linux build runs in WSL 2 with Ubuntu 26.04:

- The source is a clone on ext4, `~/mmodern-linux`, whose `origin` is the
  Windows checkout (`/mnt/d/Projetos/MModern/mmodern`); ScummVM is checked out
  and built at the pin in `~/mmodern-linux-scummvm` and
  `~/mmodern-linux-scummvm-build` with the Linux configure command above.
  The machine has more packages than the pinned set; none of them may change
  ScummVM's recorded `USE_` set (no libcurl development package).
- The original data, the DOSBox captures, the MT-32 ROMs and the Russian
  translation are copied to ext4. Reading them over WSL's 9P mount of the
  Windows drives was too slow, and a save target under `/mnt` is refused as
  remote, so play-test saves and working copies also stay on ext4.
- The clone is a disposable copy of the Windows working tree. Before each
  Linux suite, run from the clone:

  ```sh
  bash tools/run-full-ctest.sh --build-dir build-linux-rel \
    --sync-from /mnt/d/Projetos/MModern/mmodern
  ```

  It fetches the Windows checkout's `HEAD`, checks it out detached in the
  clone, discards the clone's own changes and untracked files (the ignored
  build directories stay), applies the Windows tree's complete diff against
  `HEAD`, untracked files included, rebuilds the default targets and runs the
  complete suite. `summary.txt` records the synced `HEAD` and the SHA-256 of
  that diff. `--sync-only` stops after the sync, to build other targets
  first; a later run without `--sync-from` records the same values.
- WSLg shows the game window and plays its audio through PulseAudio. It
  settles that the Linux build plays, not input feel or native audio.

The cross-platform save check of M113 writes the same saves with both
builds, the four M44 gameplay scenarios and a few developer presets, and
loads each with the other build. It requires equal `mmodern_save_dump` output,
a byte-identical F9 re-save through `mmodern_m44_baseline_restart` and
identical bytes from both builds. The restart witness restarts quiet Clouds
Journey saves only, each under its own M44 scenario; both builds must refuse
the presets on Darkside or on an Event cell with the same expected reason,
read from the witness's own log, which each restart keeps in its own folder.
Build `mmodern_save_dump`, `mmodern_m44_baseline_witness`,
`mmodern_m43_cli_witness`, `mmodern_m44_baseline_restart` and
`mmodern_test_save` in both trees, then run on Windows:

```powershell
python -B tools/cross_platform_save_check.py --windows-build build-rel `
  --game "F:/Games/gog/Might and Magic 4-5" `
  --linux-repo /home/carbo/mmodern-linux --linux-build build-linux-rel `
  --linux-game <ext4-game-directory>
```

Its evidence holds original-data saves and stays in new directories under
both build trees (`xplat-save-check/<timestamp>/`).

The original-data checks that used to be opt-in smoke targets run with the
suite since M66 (`xeen_regional_original`, `xeen_party_original`,
`xeen_item_catalog_original`, `xeen_item_catalog_reference`,
`xeen_object_visual_original` and the `xeen_*_original` event, flag and
navigation checks); the item catalog's reference verification needs the pinned
ScummVM checkout, as the catalog section above describes. The floppy-era
integration programs that no longer matched the CD data (indoor map, indoor and
outdoor objects, Remove, WhoWill, Phirna, save/resume, the graphics smoke and
the M32, M35 and M36 witness drivers) were retired in M66; the registered unit,
original-data and process tests cover their subjects.

Validation of the pinned commit completed with:

- full MMModern build: passed
- complete CTest suite of that time: 32/32 passed
- all real-data smoke targets of that time: built and passed
- real archive text, party, flags, indoor map, navigation, event script,
  interpreter, and event-system checks: passed
- SDL dummy/software runtime checks for UI, outdoor map, indoor map, automatic
  event, manual interaction, Castle Basenji No, and Castle Basenji Yes: passed
- Milestone 14 manual presentation checks for `Air / Corner`, `Snake Oil`, and
  Castle Basenji text 19: passed
- Castle Basenji Action 44 No path without teleport and Yes path with teleport:
  passed
- generated 320x200 visual-validation frames for the sign, reduced door text,
  Castle question, No state, and Yes destination: visually inspected and passed

Real-data smoke tests require a legally obtained World of Xeen installation;
original game data is never part of MMModern or its dependency checkout.

## Continuous integration

`.github/workflows/ci.yml` is the executable form of this document. Every push
to a branch of `carbiecarbie/MMModern`, every pull request from a fork and a
manual dispatch start a run with two jobs. The first, `build-and-test`, runs
on a clean hosted `windows-2025` machine. It:

- sets up MSYS2 UCRT64 (`update: false`, `path-type: minimal`) and installs
  the [pinned package set](#pinned-msys2-package-set) from verified, cached
  package files, then checks that nothing else is installed and that this
  document lists exactly the pinned packages;
- fetches the pinned ScummVM commit at depth 1 with Git for Windows, requires
  it as a detached HEAD with an empty `git status --porcelain`, configures it
  with the command above (`.github/ci/scummvm.sh` holds it and the artifact
  build) before installing `libvorbis` and `libogg`, compares
  the `USE_` lines of its `config.h` and `config.mk` with the recorded set in
  `.github/ci/scummvm-use.txt`, builds the four artifacts and caches that
  build under the commit and the pins (a cache hit skips only configure and
  build; the checkout and the `USE_` check always run);
- configures MMModern as [`build-rel`](#optimized-test-build-build-rel) with
  `-DMMODERN_ORIGINAL_DATA_TESTS=OFF` and `-DGIT_EXECUTABLE` set to Git for
  Windows, checks that `build.ninja` has no `NDEBUG`, builds every target
  including the four EXCLUDE_FROM_ALL ones, logs the free disk and the
  `libstdc++-6.dll` and `SDL2.dll` found first on `PATH`, and runs `ctest`
  once with `--output-on-failure` at the machine's processor count, with no
  retries. A failed run uploads the CTest logs for seven days.

The recorded configuration defines `USE_RGB_COLOR`, `USE_HIGHRES`,
`USE_SAVEGAME_TIMESTAMP`, `USE_SCALERS`, `USE_HQ_SCALERS`, `USE_EDGE_SCALERS`,
`USE_ASPECT`, `USE_ZLIB`, `USE_LIBCURL`, `USE_BASIC_NET`, `USE_HTTP`,
`USE_OPENGL`, `USE_GLAD`, `USE_OPENGL_GAME`, `USE_OPENGL_SHADERS`, `USE_CURL`,
`USE_TRANSLATION`, `USE_DETECTLANG`, `USE_TASKBAR`, `USE_SYSTEM_PRINTING`,
`USE_SYSDIALOGS`, `USE_TTS`, `USE_BINK` and `USE_CLOUD` in `config.h`, and
`USE_SDL2` with the same set except `USE_DETECTLANG` in `config.mk`; every
other `USE_` option is off.

`MMODERN_ORIGINAL_DATA_TESTS` (default `ON`) selects the tests. `ON`, the
local setting, requires the data paths above and registers every test. `OFF`
needs no data path and registers only the tests that need nothing outside the
repository, the build tree, the pinned ScummVM checkout and temporary folders;
the tests that need original data, DOSBox captures, MT-32 ROMs or CD audio
are registered through `mmodern_data_test()` and left out. Configuration
prints both counts. Both settings build the same targets. The run therefore
never replaces the complete local suite.

A second job, `linux-build-and-test`, runs the same build and data-free
tests natively on Linux, in an `ubuntu:26.04` container pinned by digest on
a hosted `ubuntu-24.04` machine. It:

- installs the [pinned Ubuntu package set](#pinned-ubuntu-package-set) with
  `.github/ci/linux-apt.sh`: apt reads only `snapshot.ubuntu.com` at the
  timestamp of `.github/ci/linux-packages.txt`, and the job fails unless
  the installed set equals that file in every package and version. The image
  has no CA certificates, so apt reaches the snapshot service with the
  runner's CA bundle, mounted read-only, until the pinned `ca-certificates` is
  installed; every index is still verified against the Ubuntu archive key.
  The script downloads the signed indexes and the needed packages into a
  local mirror, which the job caches under the package list, the script and
  the image's own installed set, then installs from that mirror offline. apt
  checks the mirror exactly as the snapshot: the published `InRelease`
  signatures, the hashes of every index and package, the exact versions. The
  service is therefore needed only when the list or the image changes. The
  download fails at once, naming `snapshot.ubuntu.com`, when any index or
  package cannot be fetched (`APT::Update::Error-Mode=any`), after three
  attempts of the update and of the download, 30 and 60 seconds apart, each
  with apt's own retries; an outage never reaches the dependency resolver. A
  first checkout without Git reads the list; a second one, with Git, is the
  build source. The job then checks that this document lists exactly the
  requested packages;
- creates an unprivileged `builder` user that fetches, configures and builds
  ScummVM and MMModern and runs the tests, so the save-permission cases that
  skip under root run too;
- fetches ScummVM at the pin, configures it with the Linux command of
  `.github/ci/scummvm.sh`, compares its `USE_` lines with
  `.github/ci/scummvm-use-linux.txt`, builds the four artifacts and caches that
  build under the commit, the package list, `scummvm.sh` and the `USE_` file;
  MMModern's configure then refuses any define outside
  `.github/ci/scummvm-defines-linux.txt`;
- configures `build-linux-rel` as above with
  `-DMMODERN_ORIGINAL_DATA_TESTS=OFF`, checks for `NDEBUG`, builds every
  target including the four EXCLUDE_FROM_ALL ones, logs `ldd` of `mmodern`
  and runs `ctest` once at the machine's processor count. A failed run
  uploads the CTest logs for seven days.

The manual workflow `.github/workflows/linux-appimage.yml` (Actions,
**Linux AppImage**, **Run workflow**) builds the x86-64 AppImage in an
`ubuntu:22.04` container pinned by digest (glibc 2.35), with
`.github/ci/linux-bundle-packages.txt` pinned and cached the same way.
`desktop-file-utils` validates the desktop entry; `file` and its magic
database are required by appimagetool. Both are pinned in that list too.
Ubuntu 22.04's SDL2 2.0.20 lacks `SDL_GetDefaultAudioInfo` (2.24 or later), so
`tools/linux-bundle/build-sdl2.sh` builds SDL2 2.32.10 from its upstream release
with SHA-256 `5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165`.
It enables X11, Wayland, PulseAudio, PipeWire, ALSA and OpenGL/EGL and checks
that the backends load their host libraries dynamically. ScummVM and MMModern
use that SDL2; the package list contains its backend development dependencies
instead of `libsdl2-dev`. ScummVM's `USE_` set must match
`.github/ci/scummvm-use-linux-bundle.txt` (no `USE_SNDIO` on 22.04), and its
`DEFINES` must match `scummvm-defines-linux.txt`.

`tools/linux-bundle/make-appimage.sh` builds with the `build-rel` flags,
without `-g` (GCC 11 spends over an hour producing debug information for
single units, which the package strips anyway; maintainer, 2026-11-05).
It links the GCC runtime statically and gives `usr/bin/mmodern` exactly
`$ORIGIN/../lib` as **DT_RPATH**. Its transitive lookup also finds the bundled
Vorbis dependencies; DT_RUNPATH alone would not. Bundled shared libraries
have neither RPATH nor RUNPATH. `AppRun` starts the executable without
changing the environment, so `xdg-open` receives the caller's environment.
The builder checks that the executable needs no shared GCC runtime, uses the
pinned SDL2 and resolves all libraries without `LD_LIBRARY_PATH`.
The host supplies glibc, graphics drivers, X11/XCB, Wayland, sound-server
clients, D-Bus, systemd and zlib, retaining M113's library selection.

The AppDir contains `AppRun`, a validated desktop file, the project-owned
512x512 PNG icon (SVG source in `tools/linux-bundle/mmodern.svg`),
`usr/bin/mmodern`, `usr/lib` and `usr/share/doc/mmodern/`. The latter holds
`LICENSE.txt`, the source commit and dependency sources in `SOURCE.txt`,
and the copyright notices. No original game data or saves are included.

The builder downloads official GitHub release assets, verifies their
SHA-256 on every use (including cached downloads), and passes the runtime
explicitly with `--runtime-file`; neither URL uses `continuous`:

| Component | Release | x86-64 asset SHA-256 |
| --- | --- | --- |
| [appimagetool](https://github.com/AppImage/appimagetool/releases/tag/1.9.1) | 1.9.1 | `ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0` |
| [type-2 runtime](https://github.com/AppImage/type2-runtime/releases/tag/20251108) | 20251108 | `2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d` |

Both are MIT-licensed. The runtime statically links musl (MIT and BSD
notices), libfuse (LGPL-2.1), squashfuse (BSD-2-Clause), zstd (BSD-3-Clause),
zlib (zlib) and mimalloc (MIT); the pinned runtime's Makefile also names
mimalloc, beyond the list in its LICENSE. Their notices are carried under
`licenses/` and `SOURCE.txt` gives the source locations. The GCC runtime
notice and exception are included too. appimagetool is a build tool only.

The job runs the completed AppImage with `--appimage-extract-and-run` for
`--version` and `--diagnostics` on an empty folder without a loader environment
hint, and uploads `MMModern-<version>-x86_64.AppImage` and its `.sha256` for
30 days. It replaces M113's test bundle. It is manual and runs no CTest;
the normal CI tests the same source. A release's notes retain the 0.x policy:
one current save format, with older saves possibly refused clearly.

For a local build, run the workflow's steps in the same pinned container.
When no Docker or Podman is available under WSL, build the pinned SDL2 to a
local prefix, then configure `build-linux-rel` with that prefix,
`-DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++ -Wl,--disable-new-dtags"`,
`-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON` and
`'-DCMAKE_INSTALL_RPATH=$ORIGIN/../lib'`. Build `mmodern`, then append
`--from-build build-linux-rel` to the builder's five arguments (ScummVM source,
ScummVM build, SDL2 prefix, new output folder, full source commit).
This fallback retains every packaging check but requires the local system's
glibc; it does not establish Ubuntu 22.04 compatibility. Test the artifact
both normally through FUSE and with `--appimage-extract-and-run`.

The workflow, `.github/ci/` and this document change together: a toolchain,
ScummVM or configuration change that is not made in all of them makes the run
fail. The token is read-only, no secret is used and every action is pinned by
full commit SHA. The push-triggered CI keeps every result on `main` and
cancels older runs of a work branch. The manual AppImage workflow keeps the
newest requested run per branch.

## Release packaging

A release zip is built from the tested `build-rel` executable after the complete
suites pass:

```powershell
./tools/package-release.ps1 -BuildDir build-rel -OutputDir D:/Projetos/MModern/releases
```

The script strips debug information into a staging copy (`build-rel` keeps its
unstripped binary), copies exactly the non-system DLLs that `ldd` resolves, and
only from `C:\msys64\ucrt64\bin`, and adds `LICENSE.txt`, `README.md`,
`THIRD-PARTY.txt` (bundled libraries, the GCC runtime exception, the `v<version>`
tag and the pinned ScummVM revision), static Vorbis/Ogg notices and package
versions, license texts under `licenses\` (Vorbis COPYING is retained from
the upstream v1.3.7 release because the MSYS2 package omits it), and the
launcher: `tools/release/MMModern-launcher.ps1` with two CRLF batch files that
start it, `MMModern.bat` and `Change game folder.bat`. The launcher
finds the game through GOG's registry entries or a folder picker, accepts only
a folder with `WORLD\XEEN.DAT` and remembers it in
`%LOCALAPPDATA%\MMModern\game-folder.txt`. The output directory must be outside the
repository and the zip and staging folder must not exist yet. No game data, saves,
logs or build trees are packaged. Before publishing, unpack the zip into an empty
folder and start it with a `PATH` without MSYS2.

## Updating ScummVM

The pin is `SCUMMVM_COMMIT` in both `.github/workflows/ci.yml` and
`.github/workflows/linux-appimage.yml`; the Linux CI job fails when they differ.
Changing the pinned ScummVM revision requires repeating the complete dependency
validation in fresh source, ScummVM build, and MMModern build directories. The
new revision may be documented only after provenance is recorded, the checkout
is clean, all four artifacts build, sprite filtering is reviewed, the full
MMModern build and CTest suite pass, and the relevant real-data and Milestone 14
runtime/manual checks pass.
On an mt32emu change, re-verify that queue retirement is index-based and
that abort-delayed notes stay queued.

ScummVM is licensed under GPLv3 or later. MMModern must remain compatible with
the license of the reused code.
