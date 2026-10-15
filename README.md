# rood-boi

**rood-boi** is a native PC port of *Vagrant Story* for Linux (and Windows,
built from Linux or WSL). It is built on
the [rood-reverse](https://github.com/ser-pounce/rood-reverse) decompilation:
the game's original logic is the decompiled C, compiled for x86-64 and run
directly (there is no emulator and no MIPS interpreter), with a host layer that
stands in for the PlayStation's GPU, GTE, SPU, CD drive, memory cards and pad.
On top of that sits a modern renderer: the world is drawn from native meshes on
Vulkan (or OpenGL), lit with hardware ray-traced shadows, path-traced or
probe-based global illumination denoised with NVIDIA NRD, and volumetric haze,
while the PS1 look (nearest-filtered textures, original geometry and 2D) is kept.
A live, Unreal-style editor lets you fly a free camera and place and tune lights
while the game runs.

**Disclaimer.** This is an unofficial fan project. It is not affiliated with,
endorsed by or sponsored by Square Enix or Sony Interactive Entertainment. It
ships no game data (no disc image, game executables, models, textures, audio
or movies); you must own *Vagrant Story* and supply your own disc image. (The
menu string tables under `assets/` come from the upstream rood-reverse
decompilation, which needs them to rebuild the original files.) *Vagrant
Story*, PlayStation and all other trademarks belong to their respective owners.

## Contents

1. [Features](#features)
2. [Status](#status)
3. [Requirements](#requirements)
   - [Packages (Ubuntu 24.04)](#packages-ubuntu-2404)
4. [Getting the game data](#getting-the-game-data)
5. [Build](#build)
6. [Run](#run)
7. [Tests](#tests)
8. [Platform support](#platform-support)
   - [Windows](#windows)
   - [macOS](#macos)
9. [Licence and credits](#licence-and-credits)
   - [Thank you](#thank-you)
10. [The decompilation (upstream rood-reverse)](#the-decompilation-upstream-rood-reverse)

The detailed reference for the port (renderer, editor, settings keys,
developer RPC, test suites, architecture) is [`pc/README.md`](pc/README.md),
with design notes in [`pc/docs/`](pc/docs).

## Features

- **The original game code, natively.** Main executable, title, battle/field
  engine, menus and effects are the decompiled C (plus translations of the
  remaining hand-written assembly) built for the host. The PS1 build of the
  same sources still matches the original binaries byte for byte.
- **Renderers.** Vulkan 1.3 with ray queries (64-bit build) or OpenGL 4.6/3.3.
  Rooms, characters, weapons, drop shadows and particles are drawn from
  native meshes with real depth, at up to 8x internal resolution, with
  perspective-correct texturing, FXAA, and frame interpolation for smooth
  motion above the game's 30/15 fps.
- **Lighting.** Ray-traced soft shadows, per-pixel path-traced indirect light
  (Ultra, denoised with NVIDIA NRD), DDGI probe GI (Medium/High), shadow maps
  where there is no ray tracing, ambient occlusion and volumetric haze. Quality
  presets from *Original* (exactly the PS1's baked lighting) to *Ultra*, picked
  automatically on first run.
- **PS1 look preserved.** Nearest-filtered textures, the original 2D, menus,
  fonts and 4:3 framing by default; post effects (depth of field, tilt-shift,
  fog) and 16:9 are opt-in.
- **Live editor** (Escape): free camera, an Outliner of the room's lights and
  glows, light actors from JSON prefabs (candles, torches, lanterns...),
  one-click lighting of the room's fixtures, Houdini-style point/edge/surface
  snapping, undo/redo, and a **Room Browser** that warps to any room of the
  game for lighting work (saves are blocked while warped).
- **Lighting as data.** Room lighting lives in layered, diff-friendly text
  packs under `lighting/` (generated, hand-authored, personal), so lighting
  can be reviewed and contributed as pull requests. A **fixture catalog**
  classifies every candle, torch, lantern and brazier type in the game once
  (hashes and numbers, no game data), and a generator lights every instance
  from it; tuning a type updates all of its instances.
- **Optional weapon slots.** A quick weapon and shield swap on L2 + R2,
  ported from eremtsov's QoL mod; off by default, memory cards stay
  compatible with the mod and the unmodded game.
- **Camera and view options.** The right stick turns and zooms the game's own
  follow camera; optional *smart* cutaway walls hide only the walls actually
  between the camera and Ashley; optional MMPX upscaling for 2D/UI.
- **Room texture animations** (scrolling sigils and the like) run as GPU
  material effects driven by the game's own animation state.
- **Audio and video.** The original sound driver on a software SPU (music,
  effects, reverb), the intro movie (MDEC video and XA audio), memory cards
  as standard `.mcd` images that emulators also read.
- **Input.** Keyboard and SDL 3 gamepads (Xbox, PlayStation, Switch and
  others), hot-plugged.
- **Developer tooling.** A JSON-RPC control socket and Python client
  (`pc/tools/rood_rpc.py`) for scripted play, screenshots and memory
  inspection; headless GPU test suites.

## Status

The port is **playable but unfinished**. It boots, plays the intro movie, and
runs New Game (the opening cutscenes, then control of Ashley) and Continue
from a memory card; exploration, battles, menus, saving and loading work in
the areas played so far. The automated suites cover the boot, the opening,
the first rooms, a save room and long unattended soak runs. It has **not**
been played through to the end. Known gaps:

- **The ending is not ported.** `ENDING.PRG` is not compiled for the PC
  (the upstream decompilation of it is incomplete); reaching it stops the
  game with `[UNIMPLEMENTED] vs_ending_exec`.
- Code paths that have not been translated yet stop the game with a named
  `[UNIMPLEMENTED]` message (and a stack trace) rather than misbehaving.
  Rarely reached branches (some spells, effects and enemy AI routines) are
  less tested than the rooms the tests visit.
- The game sees a digital PS1 pad: a gamepad's left stick works as a
  D-pad (the right stick drives the camera); there is no analog movement,
  button pressure or rumble.
- Widescreen (16:9) is experimental and off by default; HDR output is not
  implemented.
- 32-bit Vulkan drivers have no ray tracing: the Vulkan renderer and the
  Ultra preset need the 64-bit build.
- Linux, and Windows through a build made on Linux or WSL (see
  [Platform support](#platform-support)).

## Requirements

- **OS:** Linux on x86-64. Developed and tested on **Ubuntu 24.04 LTS** (X11
  and Wayland/XWayland sessions). Other current distributions should work
  with the equivalent packages but are untested. Windows 10/11 x64 runs a
  build made on Linux or WSL2 (see [Windows](#windows)); macOS is not
  supported.
- **GPU and driver** (64-bit build, recommended):
  - **Vulkan 1.3** with dynamic rendering, synchronization2 and geometry
    shaders for the Vulkan renderer. With `VK_KHR_ray_query` (NVIDIA RTX,
    AMD RDNA2 and later, Intel Arc, on current drivers) you get ray-traced
    shadows and the Ultra/High presets; tested on NVIDIA (proprietary driver)
    and Intel integrated graphics (Mesa).
  - Without ray queries Vulkan still runs, with shadow maps and the Low /
    Medium presets. Without usable Vulkan the game falls back to **OpenGL**:
    4.3+ (compute shaders) for the OpenGL path tracer and probe GI, down to
    3.3 core for the basic renderer.
- **CPU/RAM:** any recent x86-64 CPU; 8 GB RAM is plenty for playing (building
  uses about 1-2 GB per compiler job).
- **Disk:** about 2.5 GB for a checkout with your disc image, the extracted
  data and both builds, plus about 2 GB free temporarily during the first
  64-bit build (the NRD shader compiler is unpacked and then deleted).
- **Network for the first build:** the decompilation's `make` downloads its
  toolchain (old GCC builds, mkpsxiso, Python packages for splat; tens of MB).
  The first 64-bit build downloads NVIDIA NRD with its dependencies, including
  the DirectX Shader Compiler: about **500 MB**, which is deleted again after
  NRD is built (about 40 MB stays in `pc/build/nrd`). Later builds work offline.

### Packages (Ubuntu 24.04)

Build tools, the SDL 3 platform headers (SDL is vendored and built from
source; its backends load their libraries at run time), Vulkan and the test
helpers:

```sh
sudo apt install git build-essential ninja-build pkg-config python3 python3-venv \
    gdb libvulkan-dev mesa-vulkan-drivers vulkan-tools \
    libx11-dev libxext-dev libxi-dev libxcursor-dev libxfixes-dev libxrandr-dev \
    libxrender-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev \
    libdecor-0-dev libpipewire-0.3-dev libpulse-dev libasound2-dev \
    libdbus-1-dev libudev-dev python3-numpy python3-pil xvfb
```

- **CMake 3.30 or newer.** NRD's dependencies require it, and Ubuntu 24.04's
  `cmake` package is 3.28: install a current CMake, for example
  `sudo snap install cmake --classic` (or Kitware's APT repository, or
  `pipx install cmake`), and make sure `cmake --version` reports it. With an
  older CMake the build still succeeds, but without NRD (it warns once and the
  Vulkan path tracer uses its own denoiser).
- **The decompilation's toolchain**, for step 3 of the build. Either
  **Docker** (`sudo apt install docker.io`, then add yourself to the `docker`
  group; the snap package works too), which is what this README was verified
  with, or install the upstream prerequisites on the host:
  `sudo apt install binutils-mipsel-linux-gnu clang-format unzip wget`
  (upstream lists newer minimum versions than Ubuntu 24.04 ships; see
  [Setup](#setup)). The port's own build also needs `mipsel-linux-gnu-ld`,
  and runs it from the same Docker image when it is not installed.
- **A graphics driver.** On NVIDIA, the proprietary driver
  (`nvidia-driver-*`, which includes Vulkan); on AMD and Intel, Mesa
  (`mesa-vulkan-drivers`, above). `vulkaninfo --summary` (from
  `vulkan-tools`) should list your GPU with apiVersion 1.3 or later.
- **Sound** goes through SDL to PipeWire, PulseAudio or ALSA, whichever is
  running (Ubuntu's desktop has PipeWire).

The **32-bit build** (optional, see [Build](#build)) additionally needs
`gcc-multilib g++-multilib` and, after `sudo dpkg --add-architecture i386 &&
sudo apt update`, the 32-bit runtime libraries of the backends you use:
`libx11-6:i386 libxext6:i386 libxi6:i386 libxcursor1:i386 libxfixes3:i386
libxrandr2:i386 libxss1:i386 libxrender1:i386 libpulse0:i386
libasound2t64:i386 libudev1:i386 libdbus-1-3:i386 libgl1:i386 libegl1:i386`,
plus the 32-bit OpenGL driver (`libgl1-mesa-dri:i386`, or your NVIDIA
driver's `libnvidia-gl-*:i386`). Multiarch packages must match the versions
of their 64-bit counterparts; check apt's list before confirming anything it
wants to remove.

## Getting the game data

You need your own copy of ***Vagrant Story* (USA), SLUS-01040** and a dump of
it made from your own disc: a raw (2352 bytes per sector) single-track BIN
image, as made by a disc-ripping tool's BIN/CUE mode. Other releases (Europe,
Japan) and other formats (ISO, CHD, PBP, ECM) are not supported.

1. Place the image at **`disks/SLUS-01040.bin`** in the repository (create
   `disks/`; it is git-ignored, as is everything extracted from it). A
   symbolic link to an image stored elsewhere also works for the host build
   (Docker only sees files inside the repository, so use a copy or a hard
   link there).
2. The decompilation's `make` (build step 3) extracts the disc's files into
   `data/` with mkpsxiso's `dumpsxiso`, splits the executables with splat,
   rebuilds them from the decompiled sources, and compares every rebuilt
   binary with the one from your disc. **`All files match`** at the end is
   the check that your image is the right one: a bad or different dump fails
   there (no separate checksum is published upstream).

Never commit anything from `disks/`, `data/`, `build/` or `pc/build/`.

## Build

The build has two parts: the upstream decompilation's `make`, which extracts
your disc and builds the original PS1 files (the port uses its extracted data,
symbol tables and linker maps), then the port itself
(`pc/tools/build_boot.py`, driven by `pc/run-game.sh`).

**1. Clone** with submodules (into your home directory if you use the Docker
snap, which cannot see `/tmp`):

```sh
git clone --recursive https://github.com/noprompt/rood-boi.git
cd rood-boi
```

**2. Add your disc image** as `disks/SLUS-01040.bin` (see above).

**3. Extract and build the original files** (once, and again only when
`src/` or the disc changes). With Docker, build the toolchain image once and
run `make` in it as yourself:

```sh
# The upstream Dockerfile also copies the disc into the image for CI;
# building it without that line keeps your disc out of the image.
sed '/^COPY disks/d' Dockerfile | docker build -t rood-reverse-dev:local -
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD:/work" -w /work \
    rood-reverse-dev:local make -j8
```

(Or, with the toolchain installed on the host, just `make -j8`.) It ends with
`✔ All files match`. The image name `rood-reverse-dev:local` matters: the
port's build uses it to run `mipsel-linux-gnu-ld` when that is not installed.

**4. Build and play** (recommended: the 64-bit build with Vulkan):

```sh
pc/run-game.sh --vulkan
```

`pc/run-game.sh` builds the port (unless `--no-build`), then starts the game
in a window from the repository root, with settings in `pc/user/settings.cfg`.
On a hybrid-graphics NVIDIA machine it renders on the NVIDIA GPU (PRIME
offload; `ROOD_GPU=integrated` keeps the default GPU).

| Option | Effect |
| --- | --- |
| `--vulkan` | 64-bit build, Vulkan renderer (`ROOD_RENDERER=vulkan`). Recommended. Falls back to OpenGL if Vulkan 1.3 is not available. |
| `--64` | 64-bit build with the OpenGL renderer |
| *(none)* | 32-bit build (`build_boot.py --battle`) with OpenGL: the original port configuration; needs the multilib/i386 packages above and has no Vulkan or NRD |
| `--no-build` | skip the build step and run the existing binary |
| `--skip-movies` | (passed to the game) skip the intro movie |

To start at the title every time (no publisher and developer logos, no intro
movie), turn on System > Gameplay > Skip intro, or put `skip_intro 1` in
`pc/user/settings.cfg`. It is off by default: the original start.

The builds go to `pc/build/boot64/rood_battle_probe` (64-bit) and
`pc/build/boot/rood_battle_probe` (32-bit). To build without running:
`python3 pc/tools/build_boot.py --arch 64` (add `--nrd off` to skip NRD and its
download) or `python3 pc/tools/build_boot.py --battle` for 32-bit.

**Build times** (measured on a 20-thread desktop with a fast connection):
building the Docker image takes a minute or two and step 3 under a minute;
the first 64-bit build about 3 minutes (building the vendored SDL 3 and
glslang, and fetching and compiling NRD); the first 32-bit build about 2
minutes. A later build with nothing changed does nothing (well under a
second: `pc/build/<tree>/build-stamp.json` records every input of the last
complete build), and after a change only the units it touches recompile
(a few seconds; `pc/tools/build_cache.py`). `build_boot.py --check` says
whether a build would do anything. `--no-build` skips even the check.

## Run

`pc/run-game.sh --vulkan --no-build` starts the game without rebuilding. The
intro movie plays first (Start skips it), then the title: New Game, or
Continue to load from the memory card.

**Keyboard**

| Key | PS1 button |
| --- | --- |
| Arrow keys | D-pad |
| Z | Circle (confirm, attack) |
| X | Cross (cancel) |
| A | Square |
| S | Triangle |
| Q / W | L1 / R1 |
| 1 / 2 | L2 / R2 |
| Enter | Start |
| Tab | Select |
| J / L, I / K, O | camera: turn, zoom, reset (the right stick and R3 on a gamepad) |
| Escape or F1 | open/close the editor |
| F2 | free camera (opens the editor) |
| ` (backtick) | fast-forward while held (both stick clicks, L3 + R3, on a gamepad) |

**Gamepads** are SDL 3 gamepads (SDL's controller database; extra mappings
from `SDL_GAMECONTROLLERCONFIG` or a `gamecontrollerdb.txt` in `pc/user/`).
Buttons map by position, so the east face button is Circle (confirm) and the
south one Cross (cancel) on any pad; D-pad or left stick moves; shoulders,
triggers, Start/Select (Back) and the left stick click map to their PS1
equivalents. The **right stick** turns the camera around Ashley (left/right,
faster the further it is pushed) and zooms (up/down); **R3** turns it back to
the game's 45-degree view and distance. L1/R1 still turn in 45-degree steps;
Start (or R3 with the reset turned off) is the first-person view. Settings:
the editor's Graphics > Controls (speed, dead zone, curve, invert, zoom
range, idle reset; see `pc/README.md`, "Right-stick camera").
PlayStation controllers, including the **DualSense** and DualShock 4 over USB
or Bluetooth, work as ordinary gamepads; their touchpad, motion sensors,
lightbar, rumble and adaptive triggers are not used. Gamepads can be plugged
in at any time; `ROOD_CONTROLLER=none` disables them. While the editor is
open the keyboard and mouse belong to the editor, but a gamepad still plays.

**Saves** are standard 128 KiB memory card images: slot 1 is
`pc/save/card1.mcd` (created on the first save), slot 2 `pc/save/card2.mcd`
(inserted only if the file exists). Save at a save point and load with
Continue on the title screen. The images are compatible with PS1 emulators.

**Settings.** Graphics and lighting settings are changed in the editor and
saved with File > Save settings (Ctrl+Shift+S) to `pc/user/settings.cfg`, a
plain text file (edits made outside the game apply live). Per-room lights and
lighting overrides are saved with Ctrl+S to `pc/lights/`. `pc/save`,
`pc/user` and `pc/lights` are yours and git-ignored.

**Lighting quality.** The editor toolbar's *Lighting* dropdown picks a preset:

| Preset | Shadows | Indirect light | Needs |
| --- | --- | --- | --- |
| Original | none: the PS1's own baked lighting | baked | anything |
| Low | shadow maps | baked | integrated GPUs |
| Medium | soft shadow maps | probe GI | no ray tracing needed |
| High | ray-traced soft shadows | probe GI | ray queries (entry RT) |
| Ultra | ray-traced (2 rays) | path tracing + NVIDIA NRD | discrete RT GPU, Vulkan |

On first run, before any settings are saved, the preset is detected: Ultra
on a discrete GPU with Vulkan ray queries, High on an integrated one with
them; without ray queries Medium on a discrete GPU, Low otherwise.
`ROOD_LIGHTING_PRESET=low` (any preset name, or `auto`) overrides it.
Choices a backend cannot run fall back to the next one (the editor shows
why). `ROOD_GI=0` turns the dynamic lighting off entirely.

**The editor** (Escape or F1) is laid out like Unreal's: a toolbar,
an Outliner of the room's lights and glows, a Details panel (lighting,
atmosphere, post effects, graphics, diagnostics), Place Actors for light
prefabs, and Help > Keyboard shortcuts. Right-drag in the view flies a free
camera (WASD/QE); Play returns to the game camera. See
[`pc/README.md`](pc/README.md) ("Renderer and developer overlay") for
everything it does.

Useful environment variables (more in `pc/README.md`): `ROOD_WINDOW=WxH`
(window size), `ROOD_SCALE=1..8` (internal resolution, default 4),
`ROOD_WIDESCREEN=1`, `ROOD_AUDIO=none`, `SDL_VIDEO_DRIVER=x11|wayland`.

**If something goes wrong**, the terminal shows the log. A crash prints
`[crash]` and `[stack]` lines (`pc/tools/where.sh resolve` turns them into
function names); `[UNIMPLEMENTED] name` means the game reached code that is
not ported yet. Please include those lines, your GPU and driver, and what you
were doing when you report a problem.

## Tests

The test runner starts the game headless (on the GPU through EGL, no X
server, or on a private Xvfb display with software rendering) with scratch
memory cards and settings, so your saves and settings are never touched:

```sh
cmake -S pc -B pc/build -G Ninja && cmake --build pc/build   # CMake probes, once
python3 pc/tests/run.py --list                               # the suites
python3 pc/tests/run.py --arch 64 --renderer vulkan          # everything, 64-bit Vulkan
python3 pc/tests/run.py --arch 64 --renderer vulkan rpc      # suites matching "rpc"
python3 pc/tests/run.py                                      # the 32-bit build
```

The runner prefers rendering on the GPU through EGL (`python3
pc/tools/rood_rpc.py headless` shows which mode and GPU it gets) and falls
back to Xvfb with software rendering, which is several times slower. It
prints one table with each suite's result and log
(`pc/build/tests/<suite>/`), and exits non-zero if any failed. It does not
build the game: build first. Some suites need a save on `pc/save/card1.mcd`
and are skipped without one; golden-frame references are generated locally
(`pc/tests/golden_frames.py --update`) and never committed. A quick boot
check without the runner:

```sh
pc/build/boot64/rood_battle_probe --skip-movies --title-checkpoint
```

exits with status 0 once the original title code reaches its input loop.

## Platform support

| Platform | Status |
| --- | --- |
| Linux x86-64 | Supported (Ubuntu 24.04 tested; X11, Wayland via XWayland or natively) |
| Windows 10/11 x64 | Native 64-bit build, cross-compiled from Linux or WSL with MinGW-w64 (Windows 11, NVIDIA RTX 4070 tested; Vulkan with ray queries and NRD) |
| macOS | Not supported, not tested |
| Linux on ARM | Not supported (the build and the guest memory layout assume x86) |

### Windows

The 64-bit build runs natively on Windows as `rood.exe` (Vulkan or OpenGL,
ray-traced lighting and NRD, WASAPI audio, XInput/HID gamepads). It is
built on Linux, or in **WSL2** on the Windows machine itself, since the
decompilation's `make` and the port's symbol analysis need Linux tools: the
build compiles every unit twice, for Linux (the analysis) and with MinGW-w64
(the executable). How it works and its limits are in
[`pc/docs/Platform_Support.md`](pc/docs/Platform_Support.md).

1. In Linux or WSL2 (Ubuntu), follow [Build](#build) steps 1-3 (in WSL, keep
   the checkout in the Linux file system, e.g. `~/rood-boi`, not under
   `/mnt/c`), and install MinGW-w64: `sudo apt install mingw-w64`.
2. Build: `python3 pc/tools/build_boot.py --target windows` (it also builds
   the Linux 64-bit objects it analyses; NRD's Windows library is compiled
   from the Linux NRD build's sources and shaders).
3. Make a folder to play from (on WSL, a Windows drive):
   `python3 pc/tools/windows_dist.py /mnt/c/Games/rood-boi --disc`.
4. On Windows, run `play.bat` in that folder. The log is `rood.log`; saves
   and settings stay in the folder's `pc\save` and `pc\user` across updates.

The developer RPC takes `ROOD_RPC=tcp:PORT` on Windows (a loopback TCP port:
Windows' Python has no Unix sockets); `pc/tools/rood_rpc.py` connects to it
from Windows or WSL. Any local program can reach that port: on a shared
machine use `ROOD_RPC=tcp:PORT:TOKEN`, which requires the token (rood_rpc.py
sends it when the address includes it). The Linux-only test harness (`pc/tests`, Xvfb/EGL
headless runs) does not run on Windows.

### macOS

Not supported. Besides needing a Mach-O equivalent of the Windows work
([`pc/docs/Platform_Support.md`](pc/docs/Platform_Support.md)), macOS has no native Vulkan
(MoltenVK lacks the geometry shaders and ray queries the Vulkan renderer
uses) and caps OpenGL at 4.1 without compute shaders, and its ARM machines
would need the guest memory layout reworked.

## Licence and credits

The project's own code (the decompilation inherited from rood-reverse and the
port under `pc/`) is dedicated to the public domain under **CC0 1.0** (see
[`LICENSE`](LICENSE)). Vendored third-party code under `pc/third_party/`
(SDL 3, Dear ImGui, ImGuizmo, glslang, PsyCross) keeps its own licence, and
NVIDIA NRD and its dependencies are downloaded at build time under their own
terms and never committed; see [`THIRD_PARTY.md`](THIRD_PARTY.md).

### Thank you

rood-boi exists because of the [rood-reverse](https://github.com/ser-pounce/rood-reverse)
decompilation.

- **[ser-pounce](https://github.com/ser-pounce)** started rood-reverse and has
  done the overwhelming majority of the work on it. Turning Vagrant Story back
  into readable C that rebuilds the original game byte for byte is what made a
  native port like this possible at all. None of this would exist without it.
- **[Lil' Sassy](https://github.com/lil-sassy)** is one of the decompilation's
  most prolific contributors; their work is part of the game code this port
  compiles and runs.
- **[eremtsov](https://github.com/eremtsov)** made the
  [Vagrant Story QoL mod](https://github.com/eremtsov/rood-reverse) (CC0); its
  optional weapon slots quick swap (L2 + R2) is ported from it
  ([`pc/README.md`](pc/README.md#weapon-slots-optional)).
- **[Kenney](https://kenney.nl)** made the
  [Input Prompts](https://kenney.nl/assets/input-prompts) icons (CC0) that the
  button prompts draw for keyboards and PlayStation, Xbox and Nintendo
  controllers ([`pc/README.md`](pc/README.md#button-prompts)).
- **[Andrew Kuhar](https://andrewkuhar.com)** designed the
  [Vagrant Story UI Remaster](https://andrewkuhar.com/blog/the-phantom-pain/)
  ([Figma prototype](https://www.figma.com/proto/roPMrnFqsqd8lths3rJG6n/The-Phantom-Pain?node-id=429-804&starting-point-node-id=429%3A804)),
  the inspiration for the optional modern menus and HUD
  ([`pc/docs/Modern_Menus.md`](pc/docs/Modern_Menus.md)). Our layouts are our
  own implementation; no assets from the prototype are used.
- Thanks also to every other rood-reverse contributor, and to the projects and
  people credited in the upstream section below.

## The decompilation (upstream rood-reverse)

The rest of this README is the upstream project's, lightly adapted. rood-reverse is
a decompilation project for Vagrant Story for PlayStation 1, with a goal of
enabling interoperability with modern hardware and fixing bugs in the original
game. It does not contain any of the original files from the game disk; you
must own a copy of the game. The PS1 build in this repository still matches
the original binaries (`make` reports `All files match`).

Many thanks to the [Chrono Cross Decompilation](https://github.com/jdperos/chrono-cross-decomp) for much of the sound library code.

Heavily inspired by and indebted to the incredible work of the [sotn-decomp](https://github.com/Xeeynamo/sotn-decomp) team and the [PS1/PS2 decompilation](https://discord.gg/hKx3FJJgrV) Discord server.

Grateful to the authors of Data Crystal’s [Vagrant Story](https://datacrystal.tcrf.net/wiki/Vagrant_Story) page for their excellent work and insights.

Thanks also to the following amazing projects (and their dependencies):
- [splat](https://github.com/ethteck/splat)
- [maspsx](https://github.com/mkst/maspsx)
- [decomp.me](https://github.com/decompme/decomp.me)
- [Decomp permuter](https://github.com/simonlindholm/decomp-permuter)
- [Old GCC](https://github.com/decompals/old-gcc)
- [mkpsxiso](https://github.com/Lameguy64/mkpsxiso)
- [objdiff](https://github.com/encounter/objdiff)
- [decomp.dev](https://decomp.dev/)

### Decompilation progress (upstream)

These badges track the upstream repository (ser-pounce/rood-reverse), not this fork's additions.

![Overall progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&label=Overall+progress)

#### Core libraries
![SLUS_010.40 progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=lib.SLUS_010.40)

#### Main game engine
![BATTLE.PRG progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=battle.BATTLE.PRG)
![INITBTL.PRG progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=battle.INITBTL.PRG&color=%2332a934)
![SCREFF2.PRG progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=battle.SCREFF2.PRG)

#### Effects
![Total effects progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects)

<details><summary>Per file</summary>

![EFFPURGE.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.EFFPURGE.BIN&color=%2332a934)
![PLG005.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG005.BIN)
![PLG044.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG044.BIN)
![PLG045.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG045.BIN)
![PLG047.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG047.BIN)
![PLG049.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG049.BIN)
![PLG050.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG050.BIN)
![PLG051.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG051.BIN)
![PLG053.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG053.BIN)
![PLG057.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG057.BIN)
![PLG061.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG061.BIN)
![PLG065.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG065.BIN)
![PLG065.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG069.BIN)
![PLG073.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG073.BIN)
![PLG077.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG077.BIN)
![PLG081.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG081.BIN)
![PLG082.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG082.BIN)
![PLG085.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG085.BIN)
![PLG086.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG086.BIN)
![PLG087.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG087.BIN)
![PLG114.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG114.BIN)
![PLG128.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG128.BIN)
![PLG129.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG129.BIN)
![PLG134.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG134.BIN)
![PLG135.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG135.BIN)
![PLG137.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG137.BIN)
![PLG138.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG138.BIN)
![PLG139.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG139.BIN)
![PLG141.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG141.BIN)
![PLG142.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG142.BIN)
![PLG143.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG143.BIN)
![PLG146.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG146.BIN)
![PLG147.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG147.BIN)
![PLG148.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG148.BIN)
![PLG149.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG149.BIN)
![PLG150.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG150.BIN)
![PLG152.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG152.BIN)
![PLG153.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG153.BIN)
![PLG154.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG154.BIN)
![PLG156.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG156.BIN)
![PLG158.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG158.BIN)
![PLG160.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG160.BIN)
![PLG161.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG161.BIN)
![PLG162.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG162.BIN)
![PLG165.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG165.BIN)
![PLG166.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG166.BIN)
![PLG167.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG167.BIN)
![PLG168.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG168.BIN)
![PLG169.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG169.BIN)
![PLG198.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG198.BIN)
![PLG224.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG224.BIN)
![PLG232.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG232.BIN)
![PLG232.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG236.BIN)
![PLG242.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG242.BIN)
![PLG246.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG246.BIN)
![PLG250.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG250.BIN)
![PLG255.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG255.BIN)
![PLG260.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG260.BIN)
![PLG263.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG263.BIN)
![PLG265.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG265.BIN)
![PLG265.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG266.BIN)
![PLG272.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG272.BIN)
![PLG273.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG273.BIN)
![PLG291.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG291.BIN)
![PLG297.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG297.BIN)
![PLG300.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG300.BIN)
![PLG300.BIN progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=effects.PLG301.BIN)

</details>

#### Menus
![Total menu progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=menus&color=%2332a934)

<details><summary>Per file</summary>

![MAINMENU.PRG progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=menus.MAINMENU.PRG&color=%2332a934)
![MENU0.PRG progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=menus.MENU0.PRG&color=%2332a934)
![MENU1.PRG progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=menus.MENU1.PRG&color=%2332a934)
![MENU2.PRG progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=menus.MENU2.PRG&color=%2332a934)
![MENU3.PRG progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=menus.MENU3.PRG&color=%2332a934)
![MENU4.PRG progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=menus.MENU4.PRG&color=%2332a934)
![MENU5.PRG progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=menus.MENU5.PRG&color=%2332a934)
![MENU7.PRG progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=menus.MENU7.PRG&color=%2332a934)
![MENU8.PRG progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=menus.MENU8.PRG&color=%2332a934)
![MENU9.PRG progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=menus.MENU9.PRG&color=%2332a934)
![MENUB.PRG progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=menus.MENUB.PRG&color=%2332a934)
![MENUC.PRG progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=menus.MENUC.PRG&color=%2332a934)
![MENUD.PRG progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=menus.MENUD.PRG&color=%2332a934)
![MENUE.PRG progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=menus.MENUE.PRG&color=%2332a934)
![MENUF.PRG progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=menus.MENUF.PRG&color=%2332a934)

</details>

#### Title and Ending Sequence
![TITLE.PRG progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=title.TITLE.PRG&color=%2332a934)
![ENDING.PRG progress](https://decomp.dev/ser-pounce/rood-reverse.svg?mode=shield&category=ending.ENDING.PRG)

### Editable content
The entirety of the game's menu code and data is decompiled, including all files in `MENU` and `SMALL`, with the exceptions of `MENU/MCDATA.BIN`, `MENU/OPMCIMG1.BIN` and `MENU/OPMCIMG2.BIN`.

Much of the menu code has been refactored and is readable, and the menu executables are completely relocatable. It should be possible to rewrite the menu code arbitrarily, the method and data addresses in other modules will be relinked appropriately.

Due to the presence of a large number of empty sectors at the start of each directory on the disk, it should also be possible to rebuild the disk without affecting sector-based data loading in `BATTLE.PRG`, as long as the number of dummy sectors is adjusted accordingly.

### (Naive) Quickstart
For decompilation work, which is best contributed upstream to
[rood-reverse](https://github.com/ser-pounce/rood-reverse):

0. n.b.: You may want to consider limiting the jobs spawned by make to something sensible like `-j8`.
1. Fork the project
2. Install system dependencies and copy disk image (see "Setup" below)
3. Run `make -j` to perform an initial split & build
4. Pick a function to decompile, use `make decompme` to create a scratch on decomp.me
5. Claim and decompile the scratch
6. Copy the decompiled code where it belongs 
7. Optionally use `make permute` if needed
8. Run `make remake -j`, and finally `make format` if the code compiles successfully
9. Open a PR

### Setup
The project is mostly self-configuring but requires a minimal amount of setup.
- Make sure the following packages are installed, older versions may also work but this is not tested. This configuration works as-is on Ubuntu 26.04 on WSL; depending on your distro you may need additional packages.

| Package                      | Minimum Version |
|------------------------------|-----------------|
| clang-format                 | 21.1.8          |
| g++                          | 15.2.0          |
| binutils-mipsel-linux-gnu    | 2.45.90         |
| make                         | 4.4.            |
| python3                      | 3.14.4          |
| python3-venv                 | 3.14.4          |
| unzip                        | 6.00            |
| wget                         | 1.25.0          |

Take a look at `./Dockerfile` for a minimal setup on Ubuntu
- Dump your original disk to `disks/SLUS-01040.bin`
- Run `make -j` to finish installation and perform an initial build

### Build targets
- `make -j` should be all that is needed most of the time. The first execution will configure the remaining dependencies in the `tools` directory and extract the files from the disk; from then on it will perform a minimal rebuild. 
- `make decompme TARGET=path/to/nonmatchings/source.s` uploads the target function to a new decomp.me scratch.
- `make permute TARGET=path/to/nonmatchings/source.s` invokes the permuter for the target assembly file. 
- `make remake -j` will delete and re-split the targets; this can be necessary if the dependency management has failed to identify everything that needs rebuilding (in which case a bug report is appreciated!), or if the configuration has changed enough to be no longer valid (e.g. orphaned .d files). It's good practice to run this before submitting a pull request to make sure everything still works.
- `make format` formats the source files and symbol addresses for consistency. Please run this before submitting a PR.
- `make clean` simply deletes the `build` and `nonmatchings` directories
- `make clean-all` will completely reset the project, including dropping any source changes and data files (but not the disk image). Use with caution.
