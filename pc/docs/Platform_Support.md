# Platform support

rood-boi builds and runs on **Linux x86-64** only; it is developed and tested
on Ubuntu 24.04. Windows and macOS are **not supported and have never been
built or run**. This note lists what ties the port to Linux, found by reading
the build and the host layer, and how a Windows port could approach each
point. Nothing here has been tried on Windows yet; contributions and test
reports are welcome.

## What is already portable

- **Window, input, gamepads, audio:** SDL 3 (`pc/third_party/SDL3`, built
  from source by `build_boot.py`). `pc/src/boot_window.c`,
  `boot_gamepad.c`, `audio_output.c` and the Dear ImGui editor talk to SDL,
  not to X11, Wayland or PipeWire directly.
- **Rendering:** Vulkan 1.3 (`pc/src/vk_*.c`, shaders compiled to SPIR-V by
  the vendored glslang) and OpenGL loaded through SDL. Both exist on Windows,
  including `VK_KHR_ray_query` on current NVIDIA, AMD and Intel drivers.
- **NVIDIA NRD** supports Windows (its CMake project and SPIR-V shaders are
  the same; `pc/tools/nrd/CMakeLists.txt` would need checking with MSVC or
  MinGW).
- **The game code** (`src/`, compiled with `ROOD_PC`) is plain C against the
  PS1 SDK headers; the 64-bit port keeps PS1-layout structures with 4-byte
  guest pointers (`pc/src/psx_types.h`).

## Blockers

### 1. Fixed-address guest memory and the low game stack

`pc/src/boot_runtime.c` (`main`, `run_game_on_low_stack`):

- PS1 RAM is mapped at its PS1 address `0x80000000` (2 MB), the scratchpad
  at `0x1f800000` and an I/O page at `0x1f801000`, with
  `mmap(MAP_FIXED_NOREPLACE)`, before the graphics driver is loaded so the
  driver cannot take those ranges. Game code and ~940 linked data imports
  use these addresses directly.
- On 64 bits the game thread runs on a 64 MB stack mapped below 2 GB
  (`MAP_32BIT`), entered with `getcontext`/`makecontext`/`swapcontext`,
  because game code stores addresses of locals in 4-byte PS1 fields.

Windows approach: `VirtualAlloc(MEM_RESERVE | MEM_COMMIT)` at the same
addresses as the very first thing the process does (they are normally free
in a 64-bit process, but ASLR'd DLLs can land anywhere, so fail loudly).
For the low stack, `VirtualAlloc` at a chosen address below 2 GB, then
either a fiber (`CreateFiber` cannot place its stack, so this needs a small
assembly stack switch, or a thread created with a custom stack via
`NtCreateThreadEx`/`RtlCreateUserStack`) or a hand-written
`swapcontext` equivalent.

A 32-bit Windows build would additionally need `/LARGEADDRESSAWARE` for
`0x80000000` to be user space at all; given that 32-bit is legacy here
(no ray tracing in 32-bit Vulkan drivers), target 64-bit only.

### 2. Linking: non-PIE ELF with absolute symbols and ELF tooling

`pc/tools/build_boot.py`:

- Links with GNU `ld` through `gcc` as a **non-PIE ELF** (`-no-pie`; 32-bit
  also `-Wl,-Ttext-segment=0x00400000` so native code sits below 16 MB). The
  64-bit build needs native code and data below 4 GB so their addresses fit
  in 4-byte fields; Windows x64 images default to `0x140000000`, so a port
  needs `/BASE` (or `--image-base`) below 4 GB and `/DYNAMICBASE:NO`
  (`/HIGHENTROPYVA:NO`).
- Generates `imports.ld`, a GNU linker script assigning hundreds of data
  symbols to absolute PS1 addresses (`D_8002F548 = 0x8002f548;`). MSVC's
  `link.exe` has no equivalent; lld's MinGW driver and GNU ld for PE accept
  symbol assignments, or the imports can become pointers/macros generated as
  C.
- Uses ELF `__start_<section>`/`__stop_<section>` symbols for each overlay's
  statics (`overlay_statics_table.c`, `pc/src/overlay_statics.c`). PE needs
  grouped sections (`.data$a`/`$m`/`$z`) or generated bounds.
- Rewrites and inspects objects with `objcopy` (symbol renames and
  globalisation), `readelf`, `objdump -r`, `nm` and `gdb` (DWARF types for
  the 64-bit data imports, `pc/tools/import_types.py`). With MinGW these
  tools exist for PE/COFF, but the parsing of their output assumes ELF.
- Reads the PS1 reference ELFs it links with `mipsel-linux-gnu-ld` (host
  binutils or the `rood-reverse-dev:local` Docker image).
- Hard-codes Linux paths: `/usr/lib/x86_64-linux-gnu` (and i386) for the
  host libraries whose symbols it lists and for SDL's backend libraries,
  `/usr/lib/gcc/x86_64-linux-gnu/<version>`.

Approach: **MinGW-w64 GCC or Clang (MSYS2 UCRT64)** keeps `gcc`, GNU `ld`
(or lld) and binutils, so most of `build_boot.py` carries over with the path
and output-format assumptions made conditional. MSVC would mean replacing
the linker-script and objcopy steps entirely.

### 3. Data model: LLP64

The 64-bit port was audited for LP64 (`pc/docs/LP64_Audit.md`): `long` is
8 bytes there and the SDK's `long` fields are respelled as `int` under
`ROOD_PC_LP64` (`pc/include/psx_sdk_long_begin.h`). Windows x64 is LLP64:
`long` stays 4 bytes, which matches the PS1 layouts, but any remaining
`(long)` / `(u_long)` cast of a pointer would truncate on Windows (GCC's
`-Werror=pointer-to-int-cast`, already on for 64-bit builds, catches those).
The audit should be re-run for LLP64.

### 4. POSIX and Linux APIs in the host layer

| Where | What | Windows equivalent |
| --- | --- | --- |
| `pc/src/boot_runtime.c` | `signal` handlers for SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT, `backtrace` for `[crash]`/`[stack]`, SIGUSR1 to dump a running game's stack | vectored exception handler, `CaptureStackBackTrace`/DbgHelp; no SIGUSR1 (a hotkey or RPC method) |
| `pc/src/dev_rpc.c` | Unix-domain socket (`AF_UNIX`, `fcntl` non-blocking) for the developer RPC | `AF_UNIX` exists in Winsock since Windows 10 1803, or a named pipe or TCP on localhost |
| `pc/src/lights.cpp`, `project_settings.cpp`, `light_prefabs.cpp`, `room_settings.cpp`, `memory_card.c` | `stat` with `st_mtim` (Linux field name) for live reload, `opendir` | `_stat64`/`GetFileAttributesEx`, MinGW's `dirent.h` |
| `pc/src/audio_output.c` | `pthread` | MinGW winpthreads, or SDL threads |
| `pc/src/boot_window.c`, `glows.c`, `vk_nrd.cpp` | `clock_gettime(CLOCK_MONOTONIC)` | MinGW provides it; else `SDL_GetTicksNS` |
| `pc/src/boot_gamepad.c` | `ROOD_CONTROLLER=/dev/input/...` device paths | names only (SDL handles the devices) |

These are small and mechanical compared with 1 and 2.

### 5. The decompilation's build (`make`)

The upstream matching build is Linux-only: bash recipes, `wget`/`unzip`,
Linux binaries of decompals' old GCC, mkpsxiso and objdiff, and
`mipsel-linux-gnu` binutils (`tools/make/*.mk`). The port needs its outputs
(`data/` extracted from the disc, `build/config/*` linker maps and symbol
lists, `build/data/*.elf`). Upstream documents running it on Ubuntu under
**WSL**, so a Windows user could run `make` in WSL2 and the port's Windows
build could read the results from the same checkout.

### 6. Scripts and tests

`pc/run-game.sh` (bash, NVIDIA PRIME variables, `/proc/driver/nvidia`),
`pc/tests/run.py` (process groups, `killpg`), the headless test modes (SDL
offscreen + EGL device, Xvfb, `pc/tests/send_key.c` with Xlib),
`pc/tools/memcap` (systemd scopes) and `pc/tools/where.sh` are Linux tools.
A Windows port would need a launcher (`.ps1` or a small native one) and
could start with the RPC-driven suites, which only need the socket.

## Suggested approach

1. Run the decompilation's `make` under WSL2 (or keep it on Linux CI) and
   treat `data/` and `build/` as inputs.
2. Build the port natively with **MSYS2 UCRT64 + GCC or Clang**, 64-bit only,
   Vulkan renderer: SDL 3, glslang and NRD build there with CMake.
3. Replace blocker 1 with a small `platform_memory.c` (mmap or VirtualAlloc
   at fixed addresses, low stack and stack switch), and blocker 4 with a
   `platform.c` layer.
4. Make `build_boot.py`'s link step PE-aware: image base below 4 GB, no ASLR,
   absolute import symbols, overlay-statics bounds; drop the Linux library
   paths (or derive them from the compiler).
5. Re-run the LP64 audit for LLP64, then the RPC-driven test suites.

## WSL2 as a stopgap

Running the Linux build inside WSL2 with WSLg is **untested**. WSLg gives
Linux programs a window and GPU acceleration through Mesa's Direct3D 12
drivers (OpenGL on `d3d12`, Vulkan on `dozen`). To our knowledge those do
not expose Vulkan ray queries, and Dozen's Vulkan 1.3 support may not cover
everything the Vulkan renderer requires, so the realistic expectation is the
OpenGL renderer (possibly with the OpenGL path tracer or probe GI if the
driver offers OpenGL 4.3 compute) and no Ultra preset. The guest memory
mappings and the rest of the Linux host layer work unchanged there, since
WSL2 runs a real Linux kernel. Reports are welcome.

## macOS

Not supported. In addition to the points above (macOS uses Mach-O, not ELF
or PE): there is no native Vulkan, and MoltenVK lacks geometry shaders, which
the Vulkan renderer requires (`pc/src/vk_core.c` checks for them), as well
as ray queries; Apple's OpenGL stops at 4.1, without compute shaders, so
only the basic OpenGL renderer could run. Apple Silicon machines are ARM64,
where the guest memory layout and the x86-specific build would need further
work.
