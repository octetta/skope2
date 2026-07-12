# skope — audio oscilloscope for skred

A Tektronix-inspired audio oscilloscope that reads from the `scope-ipc`
shared memory ring buffer written by skred. Built on
[raylib](https://www.raylib.com/) for portability across Linux, macOS, and
Windows.

```
skope [shm-name]
```

Defaults: `shm-name = skred-scope`. The app reconnects automatically while it
waits for skred to publish the shared-memory scope buffer. On Windows the name
is mapped into the current user's `Local\\` object namespace; pass the same
plain name to both programs.

---

## Channel layout (from synth-types.h)

```
RECORD_CHANNELS = RECORD_TRACK_COUNT * AUDIO_CHANNELS = 5 * 2 = 10
```

| CH | Description            |
|----|------------------------|
|  0 | Main bus — Left        |
|  1 | Main bus — Right       |
|  2 | Track 1 — Left         |
|  3 | Track 1 — Right        |
|  4 | Track 2 — Left         |
|  5 | Track 2 — Right        |
|  6 | Track 3 — Left         |
|  7 | Track 3 — Right        |
|  8 | Track 4 — Left         |
|  9 | Track 4 — Right        |

---

## Building

### Prerequisites

| Platform | Required packages |
|----------|-------------------|
| Linux    | `libasound2-dev libx11-dev libxrandr-dev libxi-dev libgl1-mesa-dev libxcursor-dev libxinerama-dev` |
| macOS    | Xcode command line tools |
| Windows  | Visual Studio 2022, or MinGW/MSYS2 |

raylib is fetched automatically from GitHub by CMake (tag 5.5) unless you
have a system installation.

### Quick build (Linux)

```sh
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
./skope
```

### macOS application bundle

```sh
cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release
cmake --build build-macos --config Release
open build-macos/skope.app
```

To install into a staging prefix or create a drag-and-drop DMG:

```sh
cmake --install build-macos --prefix stage
cmake --build build-macos --target package
```

The bundle identifier is `org.skred.skope`. The generated app is unsigned;
release distribution can apply the project owner's Developer ID signature and
notarization after packaging.

### Windows (MSVC)

```bat
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
Release\skope.exe
```

### Windows (MinGW / MSYS2)

```sh
pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
./skope.exe
```

### Using a pre-installed raylib

```sh
cmake .. -DSKOPE_DOWNLOAD_RAYLIB=OFF
```

CMake will use `find_package(raylib)` and fail with a helpful message if
it is not found.

### Integrating scope-ipc.c from your skred tree

If you want to keep a single canonical copy of `scope-ipc.c`/`.h`, point
CMake at them:

```sh
cmake .. \
  -DSKOPE_SCOPE_IPC_DIR=/path/to/skred/src
```

---

## Keyboard shortcuts

### Stereo pairs

| Key         | Action                                      |
|-------------|---------------------------------------------|
| `0` – `4`  | Toggle stereo pair / track on or off        |
| `[` / `]`  | Select previous / next pair for scaling     |
| `+` / `-`  | Vertical scale ÷2 / ×2 (selected pair)      |
| `,` / `.`  | Vertical position down / up (hold to sweep) |

### Trigger

| Key            | Action                                        |
|----------------|-----------------------------------------------|
| `T`            | Cycle trigger mode: AUTO → NORMAL → SINGLE    |
| `E`            | Toggle edge: RISING ↔ FALLING                 |
| `F1`–`F5`      | Set trigger source pair                       |
| `↑` / `↓`    | Adjust trigger level (hold Shift = ×5 step)   |
| `A`            | Re-arm single-shot trigger                    |
| `H` / `⇧H`   | Increase / decrease holdoff (1 ms steps)      |

### Time / View

| Key          | Action                                   |
|--------------|------------------------------------------|
| `←` / `→`  | Time/div ×0.5 / ×2 (50 µs – 1 s range) |
| `⇧←` / `⇧→` | Scroll older / newer within the IPC buffer |
| `V`          | Cycle STACKED → OVERLAY → LISSAJOUS view |
| `P`          | Toggle phosphor persistence              |
| `Space`      | Pause / freeze acquisition               |
| `G`          | Toggle grid                              |
| `L`          | Toggle dark phosphor / light paper theme |
| `K`          | Cycle LCD → Atari-vector-style text      |
| `D`          | Toggle HUD / status bar                  |
| `R`          | Reset all scales and positions           |
| `/`          | Toggle keyboard help overlay             |
| `Q`          | Quit                                     |

---

## Trigger modes

| Mode   | Behaviour                                                        |
|--------|------------------------------------------------------------------|
| AUTO   | Free-run: display refreshes whenever new data arrives            |
| NORMAL | Only draw when a valid edge crossing is detected                 |
| SINGLE | Capture once on the next trigger, then freeze (press A to re-arm)|

---

## Display modes

The thin bar in the top bezel is the buffer overview. Its end ticks mark the
oldest and newest samples currently present in the shared-memory ring buffer;
the highlighted segment marks the window being displayed. Use `Shift+Left`
and `Shift+Right` to scroll that window through the buffered audio.

When the connected IPC header provides track metadata, skope uses the published
track names in labels and HUD chips and shows each track's synth dB setting next
to the per-division scope scale.

**STACKED** — the plot is divided into equal horizontal bands, one per
enabled stereo pair. Left and right are drawn together in each band, with a
translucent fill between them and a correlation meter on the right edge.
Good for seeing the full bus and track set without overlap.

**OVERLAY** — all enabled pairs share one plot area. Each pair has its own
color, vertical scale (`volts/div`), and vertical offset (in divisions).
Left is drawn solid, right is dimmer, and the fill shows stereo spread.

**LISSAJOUS** — X/Y phase scope where left is X and right is Y. Enabled
pairs are shown as cells, which is useful for checking mono compatibility,
stereo imaging, and mid/side balance.

---

## Persistence (phosphor)

When persistence is enabled, each new capture is drawn at full brightness
and up to 3 older traces are overlaid with exponentially decaying alpha
(default 0.6 s half-life). This mimics the phosphor burn of an analog
scope and is useful for revealing transient irregularities or jitter.

---

## CPU / refresh strategy

skope does *not* run a fixed-rate render loop when audio is not
changing. The main loop polls `write_frame` in the shared memory header;
if it has not advanced since the last check, the loop sleeps ~33 ms and
skips the draw call entirely. When audio flows, it renders at up to 60 fps
(VSYNC-limited). This means CPU use drops essentially to zero when skred
is idle or skope is paused.

---

## HiDPI / Retina

`FLAG_WINDOW_HIGHDPI` is set at startup; raylib 5.5 scales the
framebuffer accordingly on macOS Retina and Windows high-DPI displays.
`GetWindowScaleDPI()` is queried once after window creation and stored in
`dpi_scale_x/y`; all font sizes and line widths are multiplied through it.

On Linux with Wayland, set `RAYLIB_PLATFORM=PLATFORM_DESKTOP_SDL2` (or
pass `-DPLATFORM=PLATFORM_DESKTOP_SDL2` to cmake) and install
`libsdl2-dev` for automatic Wayland fractional-scale support.

---

## File layout

```
skope/
├── CMakeLists.txt
├── README.md
├── compat/
│   ├── synth-types.h          ← real header from skred (RECORD_CHANNELS=10)
│   └── portable_atomic.h      ← inline shim: GCC __atomic / Win32 Interlocked
├── cmake/
│   ├── dpi.manifest           ← Windows Per-Monitor DPI awareness v2 manifest
│   └── dpi.rc.in              ← MinGW resource file to embed the manifest
└── src/
    ├── skope.c                ← oscilloscope application
    ├── scope-ipc.c            ← copy from skred (producer + reader impl)
    └── scope-ipc.h            ← copy from skred
```

---

## Updating scope-ipc from skred

The `src/scope-ipc.*` files are direct copies from the skred source tree.
They support POSIX shared memory and Windows named file mappings. When
skred's IPC format changes (new magic, version bump, header layout
change), copy the new files over and rebuild. The `_Static_assert` checks
in `scope-ipc.h` will catch layout mismatches at compile time before any
misreads can occur at runtime.
