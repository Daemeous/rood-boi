# Platform support

rood-boi builds and runs on **Linux x86-64** (developed on Ubuntu 24.04) and,
as a native 64-bit executable cross-compiled with MinGW-w64, on **Windows
10/11 x64** (tested on Windows 11 with an NVIDIA RTX 4070 and a DualSense:
boot, intro, title, New Game into the opening, Vulkan with ray queries and
NRD, WASAPI audio, the developer RPC; less of the game has been played than
on Linux). macOS is not supported.
This note explains how the Windows build works, where it differs from Linux,
and what is left.

How to build and play it is in the README ("Platform support", "Windows").

## The Windows build

`python3 pc/tools/build_boot.py --target windows` (on Linux or in WSL2;
implies `--arch 64`) writes `pc/build/win64/rood.exe`, a static executable
with no DLLs to ship. `pc/tools/windows_dist.py DIR` assembles a folder to
play from (the executable, `play.bat`, `data/`, the lighting packs and
`pc/assets`).

### Twin objects

The build's symbol work (which imports are data at PS1 addresses, which are
fail-fast traps, overlay-local renames, function tables, the LP64 data
imports' DWARF types read by gdb) parses ELF tools' output (`nm`, `objdump
-r`, `readelf`, gdb). Rather than teach all of that PE/COFF, every unit is
compiled twice:

- the **ELF object** (`name.o`), exactly as the Linux 64-bit build compiles
  it; the analysis runs on these, unchanged;
- its **COFF twin** (`name.obj`), compiled by `x86_64-w64-mingw32-gcc-posix`
  from the same source. Every `objcopy` edit made to an ELF object
  (globalised statics, ENDING/PLG prefixes, overlay-local data renames) is
  repeated on its twin (`objcopy()` in `build_boot.py`). The generated tables
  (`traps`, `function_tables`, `plg_entries`, `native_imports`) are compiled
  for both. `rood.exe` links the twins.

The Linux link is skipped; the Linux 64-bit build itself is unchanged.

### Differences the twins handle

- **Preprocessing.** The game's units (`-nostdinc`) are preprocessed by MinGW
  for the twin (`name.win.i`), not reused from Linux: compiler macros such as
  `__UINTPTR_TYPE__` are expanded into the source, and Linux's is `unsigned
  long`.
- **LLP64.** Windows' `long` is 4 bytes. The 64-bit port is LP64
  ([`LP64_Audit.md`](LP64_Audit.md)): the SDK's longs are already respelled
  `int`, and the game's own `long`s (pointer casts through `long`, for one)
  were audited as 8 bytes. The twins' preprocessed game units get every
  `long` that is not already `long long` or `long double` as `long long`
  (`lp64_longs()`; string literals untouched), so they see Linux's types.
  Host code (`pc/src`, compiled from source) keeps Windows' 4-byte `long`;
  the pointer casts through `unsigned long` that `-Werror=pointer-to-int-cast`
  found there are `uintptr_t` now. Its other `long`s have not been audited
  one by one for LLP64.
- **Structure layout.** MinGW defaults to MSVC's bitfield and packed-struct
  layout, which breaks the PS1 layout assertions; twins use
  `-mno-ms-bitfields` (GCC's layout, as on Linux and the PS1).
- **Code model.** `-mcmodel=small`: extern data is addressed RIP-relative
  instead of through `.refptr` stubs, which are COMDATs named after the
  symbol and would survive the overlay-local renames pointing at the wrong
  address.
- **Image layout.** Linked at image base `0x400000` without ASLR
  (`--disable-dynamicbase --disable-high-entropy-va`): native code and data
  stay below 4 GB (psx_ptr fields hold native addresses) and within RIP reach
  of the guest RAM at `0x80000000`. The data imports' absolute addresses
  come from the same `imports.ld`, which MinGW's GNU ld accepts as is.
- **Overlay statics.** PE has no `__start_`/`__stop_` section symbols. An
  overlay's `.data$*` input sections are renamed
  `.data$rood_ovl_<key>_data_m` (GCC on MinGW puts zero-initialised statics
  in `.data$` too, so there is no `.bss` kind; the build stops if one
  appears); ld sorts grouped sections by name, so zero-length markers in
  `..._a` and `..._z` (`overlay_statics_win.c`) bracket them.
- **Name clashes.** The PS1 SDK's `EnterCriticalSection` and libsn's
  `__main` are renamed `psx_*` in the game's objects only: kernel32's
  `EnterCriticalSection` is used by the C++ and thread runtimes, and MinGW's
  `main` calls `__main` to run the C++ constructors (the game's would
  replace it). `rand`/`srand` override the C library's, as on Linux.
- **Weak data.** A weak declaration seen by the unit that defines the
  variable leaves a PE definition unresolved: `room_warp.h` declares
  `pc_room_warp_enemies`/`_scripts` non-weak on Windows.
- **Runtime libraries.** `-static` (libstdc++, libgcc, winpthreads), SDL 3
  built for Windows (`pc/build/sdl3_win`), and `binmode.o`, so files open in
  binary mode unless `"t"` is given, as on Linux.

### Runtime (`pc/src/boot_runtime.c`)

- Guest RAM (`0x80000000`, 2 MB) and one 8 KB block for the scratchpad and
  the I/O page (`0x1f800000`; Windows reserves 64 KB granules) are
  `VirtualAlloc`ed at their addresses first thing in `main`.
- **The game stack.** Windows cannot place a thread's or fiber's stack, so a
  64 MB stack is `VirtualAlloc`ed below 2 GB, also before the window: the
  graphics driver (NVIDIA's at least) reserves most of the low address space
  when it loads. `rood_call_on_stack` (a few lines of assembly with SEH
  unwind info, frame pointer `rbp`) switches the main thread to it, and the
  thread information block's stack bounds are set to it for code that checks
  them (exception dispatch, drivers).
- **Crash reports.** A vectored exception handler prints `[crash]` and
  `[stack]` (`CaptureStackBackTrace`) for faults in the executable's own
  code; resolve the addresses with `x86_64-w64-mingw32-addr2line -f -i -e
  pc/build/win64/rood.exe ADDR...`. Exceptions in other modules (a driver's
  own handled ones) are left alone.

### Host layer

- `pc/src/win_compat.h` is force-included into every host object of the
  Windows build (never the game's units): `<windows.h>` once, lean, without
  `min`/`max`, with `near`/`far` undefined; `strcasestr`; a `%TEMP%` scratch
  file; and `clock_nanosleep`, which the frame pacing needs to sleep to
  absolute `CLOCK_MONOTONIC` deadlines (winpthreads only supports
  `CLOCK_REALTIME` there and returns at once, so the game ran unthrottled): a
  high-resolution waitable timer to within half a millisecond, then polling.
- `pc/src/platform.h`: `pc_mkdir`, `PC_MTIME_NS` (Windows' `stat` has whole
  seconds) and `pc_capture_file` (instead of `open_memstream`).
- Two identifiers collided with Windows headers and were renamed
  (`editor_undo.cpp`'s `open` flag, `lighting_quality.cpp`'s `FLOAT`).
- **Developer RPC.** `ROOD_RPC=tcp:PORT` (any platform) listens on
  `127.0.0.1:PORT`; Windows has no Unix sockets for Python clients.
  `pc/tools/rood_rpc.py` connects to `tcp:` addresses and defaults to
  `tcp:47323` on Windows. A TCP port can be reached by any local program,
  web pages' requests included, so a line that is not a JSON object (an HTTP
  request line) closes the connection.
- **NRD.** `pc/tools/nrd.py`'s `ensure_windows()` compiles NRD and
  ShaderMakeBlob with MinGW by replaying the Linux NRD build's own compile
  commands (`ninja -t commands`), with its SPIR-V shaders (`_Shaders`, the
  same for every host), so DXC runs once, on Linux. MathLib calls `abs()` on
  floats, which MinGW's headers leave ambiguous: a force-included header
  brings `std::abs` in.

### Not on Windows

- The test harness (`pc/tests/run.py`, Xvfb/EGL headless runs, the probes
  `build_boot.py` runs on Linux) and `pc/run-game.sh`; on Windows,
  `play.bat` from `windows_dist.py` starts the game. `rood_rpc.py`'s
  `Game.launch` starts `rood.exe` windowed on Windows (there is no headless
  mode there).
- `SIGUSR1` stack dumps.
- A native Windows build environment (MSVC, or building in MSYS2): the
  decompilation's `make` needs Linux tools anyway, so WSL2 is the way to
  build on a Windows machine.

## WSL2

Building in WSL2 is the supported way to make the Windows build on a Windows
machine. Running the Linux build itself under WSLg is untested: WSL2 exposes
the GPU through Mesa's Direct3D 12 drivers, which do not offer Vulkan ray
queries, so the native Windows build is the better way to play on Windows.

## macOS

Not supported. In addition to the points above (macOS uses Mach-O, not ELF
or PE): there is no native Vulkan, and MoltenVK lacks geometry shaders, which
the Vulkan renderer requires (`pc/src/vk_core.c` checks for them), as well
as ray queries; Apple's OpenGL stops at 4.1, without compute shaders, so
only the basic OpenGL renderer could run. Apple Silicon machines are ARM64,
where the guest memory layout and the x86-specific build would need further
work.
