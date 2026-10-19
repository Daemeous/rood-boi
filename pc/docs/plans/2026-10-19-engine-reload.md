# The engine as a hot-reloadable library (plan A5)

Status: done, 2026-10-19 (tasks 1-6 below; "Result" at the end). Phase A5 of
`2026-10-18-freedom-and-fast-iteration.md`, on top of A4 (snapshot-resume).
Linux 64 bits only. Windows, the 32-bit build and the default 64-bit build
(`pc/build/boot64/rood_battle_probe`, what the tests and releases run) stay
one statically linked executable, unchanged.

## Goal

Edit a file of the engine (`pc/src`, outside the game's own units), and the
running game picks the change up at its next vsync, in a second or two: no
restart, no window reopening, the game state untouched (not even saved and
restored: it never moves).

A4 already gives "rebuild, then back at the same moment" in 6.2 s (build
5.7 s, restart 0.5 s). A5 replaces both halves: only the engine library is
rebuilt (one object and one link, not the game's whole build pipeline), and
the running process swaps it in place.

## Why it is tractable

A4 made the engine able to start cold beside a live game: a resumed save
state is a game whose engine has never run. Every engine subsystem already
rebuilds what it derives from the game (the hardware VRAM target from native
VRAM, native rooms and actors from their change checks, `pc_hw_resync`,
`pc_cd_restored`). An engine reload is the same start without the process
restart and without touching game memory: unload the engine, load the new
one, and start it exactly as a resumed snapshot starts it.

## The split

One build, a new output directory: `build_boot.py --arch 64
--engine-library` builds `pc/build/dev64/`:

- `rood_host`: the executable. The game and everything whose state is the
  game's:
  - the game's units (src/ and the pc/src units built like them: the
    bridges, room_geometry, battle_*, actor_*, ...), pinned at 0x18000000
    exactly as in A4, so its snapshots are interchangeable with the static
    build's (same code and layout keys);
  - the host units whose statics are game state (`STATE_HOST_UNITS`):
    boot_runtime (main, the vsync, the CPU rasteriser and native VRAM),
    cd_drive, mdec, xa_audio, audio_libspu, the GTE;
  - the units that read or drive game state outside a frame: snapshot,
    state_diff, state_rewind, overlay_statics, ownership, memory_card,
    input_record (recording and replay must not restart), charge_skip (it
    pumps the game);
  - audio output and the software SPU (the audio thread must never run
    library code; the SPU is game state);
  - the generated tables (traps, function tables, PLG entries, LP64 native
    imports, overlay statics, snapshot keys) and the owned Zig code;
  - SDL 3, a static library (so the window, the audio device and input
    survive a reload; not position-independent either);
  - new: `engine_host.c` (load, reload, the carry store) and the generated
    boundary (below).
- `librood_engine.so`: the engine, everything else in pc/: the window and
  input glue (boot_window, controller, gamepad, camera_stick), the RPC, the
  renderers (gpu_hw, GL, Vulkan, native rooms/actors/effects, shadows, GI,
  glows, AA, post), the UI and HUD (ui_text, prompts, ui/*.cpp, hud/*),
  the editor and Dear ImGui, Lua and the plugin host, settings, frame_cost,
  and NRD (its archive is position-independent but its symbols are hidden:
  the host could not export them, so it links into the library).
  Compiled `-fPIC` (`-fno-gnu-unique` for C++: a unique symbol makes glibc
  refuse to unload a library), linked with a preferred base of 0x64000000
  (above the game stack, below guest RAM: glibc honours it as a hint, so
  engine addresses still fit a 4-byte `psx_ptr`, as in the non-PIE static
  build).

The static build is untouched: the same objects (fpie) link one executable.
The dev build compiles the engine's objects a second time with -fPIC (once:
the object cache keeps both).

## The boundary (inventory, `nm` over the current 64-bit link)

Measured on main c95b004e with the split above:

| Direction | Count | How it is crossed |
|---|---|---|
| host → engine functions | 224 | a generated trampoline per name in the host (`jmp *slot`); the slots are re-resolved (`dlsym`) at every load |
| host → engine data | 46 | moved to the host (below) |
| engine → host (functions and data) | 108 | the host exports them (`--dynamic-list`) |
| engine → guest data imports (`imports.ld`) | 5 | exported (absolute symbols) |
| engine → SDL 3 | 109 | exported; pulled from the archive with `--undefined` |
| engine → NRD | 8 | linked into the library |
| engine → libc, libstdc++, libm | 278 | the library's own `DT_NEEDED` |

The host → engine functions are mostly the hardware rasteriser mirror
(`pc_hw_*`: 40, called per primitive by the CPU rasteriser), the modern UI
mailboxes (`pc_ui_shell_*`, `pc_ui_loot_*`, ... called by the game's
bridges), native rendering (`pc_room_native_*`, `pc_object_native_*`), the
vsync (`pc_window_tick/frame`, `pc_rpc_poll`, `pc_lua_tick`) and the
room catalog. A trampoline is one indirect jump; the address of an engine
function taken by host code is its trampoline, so it never goes stale.

The host → engine data are settings the game's units read (`pc_qol_*`: 26,
`pc_camera_stick_*`), renderer switches (`pc_hw_active`, `pc_present_rate`,
`pc_widescreen_layout`, ...) and a few tables. They become **host-owned
blocks**: the generated boundary defines their storage in the host, the
executable exports them, and the library's own references bind to the host's
copy (ELF interposition, as copy relocations do; checked on glibc 2.39). On
the first load the host copies each one's initial value from the library's
own definition (`dlsym` on the library handle). On a reload:
- writable data (`D`, `B`, weak) keeps its live value: it survives;
- read-only data (`R`) is copied again: a constant table edited in the
  engine shows;
- a word that holds an address inside the old library is reset from the new
  library's initial value, and logged;
- a size that changed refuses the reload (the host must be rebuilt: restart).

Nothing else crosses: the version script exports from the library only the
224 functions, the 46 data and its entry points, so everything else binds
inside the library.

What the generator refuses: a host → engine name that is neither a function
nor data in the library; a crossing variable whose initial value holds an
address (it would point into the old library after a reload) is allowed but
reported in the build's boundary file.

## What survives a reload, and where it lives

| State | Where | How |
|---|---|---|
| Game memory, game statics, game stack, overlays, VRAM, SPU, CD, MDEC, XA, cards | host | never touched |
| Audio stream and device | host (audio_output + SDL) | keeps playing through the reload |
| Settings read by the game (`pc_qol_*`, ...) | host-owned blocks | interposition (above) |
| Every setting (graphics, UI, lighting, AA, window mode, ...) as currently set, saved or not | carry store | `pc_settings_write` text before unload, applied after the new engine loads the file |
| RPC: listening socket, clients (fd, buffered request bytes, auth), pause and step budget, input holds, sound counters, the pending `reload` reply | carry store | dev_rpc save/load; the reply is sent by the new engine |
| The window (`SDL_Window*`), its size, position, fullscreen | carry store (handle; SDL is in the host) | the renderers adopt it instead of creating one |
| Interactive fog fields | carry store | the snapshot chunk functions `pc_fog_state_save/load` |
| Plugin storage (Lua) | files | `pc_lua_shutdown` writes it at unload (it is an atexit handler of the library), the new Lua host reads it |

The carry store is host-owned: named blobs (`pc_engine_carry_put/get`) that
outlive the library. Each subsystem saves what it needs before unload and
takes it back when it starts again.

What re-initialises (as at a resumed snapshot): the GPU device and every GPU
resource (rebuilt from native VRAM and the game's data: `pc_hw_resync`), the
renderer caches, Dear ImGui (its context and the overlay's open windows), the
editor (selection, camera, unsaved edits that are not settings), Lua plugins
(run again from their files; their in-memory state is lost, their storage is
not), the free camera, frame-cost counters, the frame pacing deadline, the
gamepad handle (reopened), keys held during the reload (released until
pressed again).

## Handles

- **Window:** kept. `SDL_Init` and the window stay in the host's SDL; the
  library's unload leaves the window open, and `pc_window_init` in the new
  library takes it from the carry store (the GL and Vulkan presenters create
  their context or surface on it). The renderer cannot change at a reload
  (the window was made for GL or for Vulkan): that is a restart.
- **OpenGL:** the context is destroyed at unload (which frees every GL object
  it owns: nothing is shared) and created again on the same window.
- **Vulkan:** destroyed and created again (instance, device, surface,
  swapchain, every buffer, image, pipeline). Most of it has no teardown today;
  vk_core gets a tracking layer: its loader wraps the create/destroy pairs it
  hands out (buffers, memory, images, views, samplers, shader modules,
  pipelines, layouts, descriptor pools and layouts, query pools, command
  pools, fences, semaphores, acceleration structures, swapchains), so
  teardown destroys whatever is still alive in reverse order, then the
  device, surface and instance. Dear ImGui's and NRD's own objects go through
  their shutdown functions first. `ROOD_VK_VALIDATE=1` reports anything left.
  The pipeline cache file makes the new device's pipelines cheap.
- **Audio:** kept (host).
- **Input:** SDL state is the host's; the gamepad handle is closed at unload
  and reopened.
- **RPC socket:** kept (carry store); the socket file is never unlinked.

## The reload

At the vsync boundary only, on the game thread, with no engine frame on the
stack:

1. RPC `reload` (engine) asks the host (`pc_engine_reload_request`) and
   returns; the pause loop of `pc_rpc_poll` returns too when a reload is
   wanted.
2. Back in the host's `pc_tick` (boot_runtime.c, a weak hook absent from the
   static build): the host checks the game stack from the vsync boundary up
   holds no return address into the library (else it waits for a later
   vsync); calls the library's `rood_engine_unload` (it saves its carry, waits
   for its background threads, shuts Dear ImGui down, tears the GPU down,
   closes the gamepad); `dlclose`; checks the library is really gone.
3. Loads the new file (through a hard link of it, so a build replacing the
   file meanwhile cannot change what is loaded, and the previous one stays
   loadable), re-resolves every slot, copies or checks the crossing data,
   scans the host's data for addresses into the old library (logged).
4. Calls `rood_engine_resume`: `pc_window_init` (adopting the window),
   settings from the carry, `pc_lua_init`, fog, `pc_hw_resync`.
5. The new engine's `pc_rpc_reloaded` replies to the request (vsync, time
   taken, generation) and carries on with the vsync; the rest of `pc_tick`
   (`tick_frame`: window events, audio, present) runs on the new library.

A library that fails to load (a missing symbol, a crossing size that
changed) is unloaded and the previous one is loaded again: the reply is an
error and the game goes on.

## The dev loop

- `pc/tools/engine_lib.py`: rebuilds only the library, from the manifest the
  full build writes (`pc/build/dev64/engine_library.json`: each engine
  source's compile command, the link command, the boundary). It recompiles
  the units whose source or headers (gcc depfiles) changed, relinks, checks
  the new library still provides the boundary (names and sizes) and that its
  undefined symbols are what the host exports, and renames it into place.
  A change to a host unit (the game, the runtime, the snapshot code), a
  boundary change, or a build-script change says so: that needs
  `build_boot.py --engine-library` and a restart (dev_loop.sh's A4 path).
- `pc/run-game.sh --reload`: builds pc/build/dev64 and runs `rood_host`.
- `pc/tools/dev_loop.sh --reload`: `engine_lib.py`, then RPC `reload`; it
  prints build and reload times. `--watch`: does that on every saved change
  under pc/src (inotifywait when installed, else polling), falling back to
  the A4 rebuild-and-restart when the change is not engine-only.

Measure: edit to the reload reply (the frame after is presented on the new
engine), against A4's 6.2 s.

## Risks

- **Stale addresses into the old library.** Function pointers or pointers to
  engine statics stored in host state, guest RAM or the heap. Covered: the
  trampolines (host-taken addresses), the carry store (no raw library
  pointers in it), the post-reload scan of the host's writable data
  (reported). Not covered: heap objects the host keeps with library pointers
  in them; none known. A string literal of the engine handed to the game and
  kept would be one.
- **A modern menu open during a reload:** its C++ state (screen, focus) is
  lost while the game's bridge still thinks it is open. Checked: the game
  then closes the menu itself, so a reload is refused while one is open (see
  Result).
- **Leaks per reload:** heap the old engine allocated and never freed
  (C statics with malloc'd buffers; Lua states if not closed). Measured as
  RSS growth per reload; fine for development, not a release feature.
- **Background threads in the library** (the atmosphere noise at start-up,
  the editor's `system()` helpers): unload waits for the noise; a reload is
  refused while a helper runs.
- **glibc not honouring the preferred base** (something mapped there): the
  library then loads above 4 GB; the host logs it, and storing an engine
  address in a guest pointer would stop the game (`pc_psx_bad_pointer`), as
  it would anywhere.
- **Interposition relies on the library's code addressing the crossing data
  through its GOT** (default for -fPIC with semantic interposition); a
  `-fno-semantic-interposition`, LTO or visibility change would silently
  break it. The smoke test checks a crossing value set through the engine is
  the one the host reads.
- **Debugging:** gdb follows `dlopen`; `where.sh resolve` needs the
  library's load address for library frames (the crash handler prints it).

## Tasks

1. This plan.
2. build_boot.py `--engine-library`: pc/build/dev64, the split, -fPIC
   engine objects, the boundary generator (trampolines, slots, crossing
   storage, dynamic list, version script, archive `--undefined`s), both
   links, the manifest. Host loader `engine_host.c` (first load only) and
   the weak start hook in main. The dev host boots and plays like the static
   build.
3. Unload/resume: carry store; window adoption; GL and Vulkan teardown
   (tracking layer); ImGui and NRD shutdown; RPC carry and `reload`;
   settings and fog carry; the stack check, base check, stale-address scan;
   fallback to the previous library.
4. `engine_lib.py` (library-only rebuild with the boundary check),
   `run-game.sh --reload`, `dev_loop.sh --reload` and `--watch`. Measure.
5. `engine_reload_smoke`: state survives (the game state after a reload
   equals before; N vsyncs later it equals a run without the reload), the
   game keeps running, an engine change (a rebuilt engine unit) takes effect,
   settings and RPC survive, a broken library falls back. GL by default;
   Vulkan with validation when available. Registered in run.py and
   affected.toml.
6. Docs: Building/README sections, the porting guide's note on where state
   must live.

## Result (2026-10-19)

Commits on branch `engine-reload`: the plan; the split build
(`build_boot.py --engine-library`, `pc/tools/engine_boundary.py`,
`engine_host.c`); the reload (`engine_reload.c`, the carry store, the Vulkan
tracking layer, the GL/Vulkan/ImGui/gamepad teardown, RPC `reload`);
`engine_lib.py`, `run-game.sh --reload`, `dev_loop.sh --reload/--watch`;
`engine_reload_smoke`.

The final boundary: 227 engine functions behind trampolines (the plan's 224
plus the reload's own entry points), 46 host-owned engine variables, 279
names the host exports. The snapshot keys of the split equal the static
build's (the layout key no longer names the build directory), so a save
state moves between them.

Measured (RTX PRO 6000, headless EGL unless noted):

| | GL | Vulkan |
|---|---|---|
| RPC reload (unload, load, start) | 40-50 ms | 300-380 ms (unload ~80: device idle and pipeline cache; start ~260: device and pipelines) |
| GL on Xvfb (llvmpipe), Vulkan on Xvfb with a swapchain | 710 ms | 360 ms |
| A renderer edit (gpu_hw.c) to the new engine running, `dev_loop.sh --reload` | | 2.5 s (library 2.2 s: compile 1.3, link 0.6) |
| The A4 restart loop, same edit, same machine and load | | 8.1 s (build 7.6, restart 0.5) |
| Resident memory per reload | 1.4 MB | none measurable |

What reloads: every unit of the library (renderers and shaders, UI and HUD,
editor, overlay, Lua host, RPC handlers, window and input glue, settings
code). What does not (a change there is `build_boot.py --engine-library`
and a restart, which `dev_loop.sh` does by itself): the game's units, the
host units listed above, a header any of them includes, the build scripts,
and a change to the boundary itself (a new engine function the game calls,
a host-owned variable's size, a host or SDL symbol the library newly needs:
`engine_lib.py` checks all three before swapping). Kept in the host on
purpose, not reloadable: audio output and the software SPU (the audio
thread), input recording and the charging skip (they drive game state
outside a frame), SDL (the window must outlive the library).

Found while building it:
- SDL's video subsystem is reference counted to zero by one `SDL_Quit`:
  quitting it at unload closes the kept window, so the library leaves it
  initialised (one count per reload, harmless).
- A scan of the stack for words inside the library's code found stale
  return addresses in frame padding; the check walks the real frames with
  the unwinder instead.
- `build_boot.py` turns an undefined function into a fail-fast trap: an
  engine unit calling a `static inline` it did not include links and stops
  at run time with the name (`pc_vk_on`), as in the static build.

- A modern menu open during a reload: its C++ state (screen, focus, model
  view) is the engine's while the game's bridge thinks it open; tried, the
  new library started with it closed and the game followed (the menu closed
  itself). That state is not carried: a reload is refused while the menu,
  the loot, map or save screen is open ("retry: ..."), and `dev_loop.sh`
  asks again until it goes through. A refused reload never runs a frame of a
  paused game; a live engine frame on the game stack is refused the same way.

Risks left: engine units that feed the game (the camera stick, the input
glue) restart from rest, so a reload while the right stick is held changes
what the camera does next (the game state at the reload itself is never
touched); the Vulkan teardown was checked by the object count and by running
on, not by the validation layer (not installed on this machine); heap the
old library allocated outside its C++ objects is not freed (measured: 1.4 MB
per reload on GL).
