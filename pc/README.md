# rood-boi: the native PC port

The detailed reference for the port: renderer, editor, settings, developer
RPC, tests and architecture. Installation, building and playing are covered
in the [root README](../README.md); this file assumes that setup. Later
sections keep the port's development history (checkpoints that were reached
on the way to a playable game), so some of their "not yet" statements
describe the state at that time.

**Legal.** This is an unofficial fan project. It is not affiliated with,
endorsed by or sponsored by Square Enix or Sony Interactive Entertainment.
It ships no game data: no disc image, game executables, models, textures,
audio or movies. To use it you must own *Vagrant Story* and supply your own
disc image. *Vagrant Story*, PlayStation and other
trademarks belong to their respective owners. The project's own code is
released under CC0 (see `LICENSE`); vendored third-party code keeps its own
licence, listed in [`THIRD_PARTY.md`](../THIRD_PARTY.md).

## Build and run

Supply your own NTSC-U SLUS-010.40 disc as `disks/SLUS-01040.bin`. First complete
this repository's normal matching build (see the root README, which also
lists the packages to install). The host build uses its extracted assets,
symbol manifests, and object files. Nothing extracted from the disc belongs
in Git. `pc/run-game.sh --vulkan` (64-bit, Vulkan) is the recommended way to
build and play; the commands below are the 32-bit build.

Host dependencies: Python 3, GCC/G++ with x86 multilib libc, CMake and (optionally)
Ninja. The window, input, gamepads and audio use SDL 3 (`pc/third_party/SDL3`,
vendored, zlib licence), which the builder compiles as a static 32-bit library in
`pc/build/sdl3`; distributions do not ship 32-bit SDL 3 development packages.
SDL loads its platform libraries at run time, so the build only needs their
headers (on Ubuntu: `libx11-dev libxext-dev libxi-dev libxcursor-dev
libxfixes-dev libxrandr-dev libxss-dev libwayland-dev libxkbcommon-dev
libdecor-0-dev libpulse-dev libasound2-dev libdbus-1-dev libudev-dev`), and
running needs the 32-bit (`:i386`) runtime libraries of the backends used:
`libx11-6 libxext6 libxi6 libxcursor1 libxfixes3 libxrandr2 libxss1` for X11,
`libwayland-client0 libwayland-cursor0 libwayland-egl1 libxkbcommon0` (and
`libdecor-0-0` for title bars on GNOME) for native Wayland, `libpulse0` or
`libasound2` (`libasound2t64` on Ubuntu 24.04) for sound, and the 32-bit OpenGL driver. The reference linker can be
installed locally as `mipsel-linux-gnu-ld`, or run through the existing
`rood-reverse-dev:local` Docker image. This is currently a Linux-only
port; see `docs/Platform_Support.md` for other platforms.

From the repository root:

```sh
python3 pc/tools/build_boot.py
pc/run-boot.sh
```

Arrows navigate, Z is Circle (confirm/attack), X is Cross (cancel), A is Square,
S is Triangle, Q/W are L1/R1, 1/2 are L2/R2, Enter is Start and Tab is Select;
Escape or F1 opens the editor (see below), and closing the window quits. Gamepads
are SDL gamepads, mapped through SDL's built-in GameController database, so face
buttons go by position whatever their labels: D-pad or left stick navigates;
Circle/east confirms, Cross/south cancels; face buttons, shoulders, triggers,
Start/Select (Back) and stick clicks map to their PS1 equivalents. Extra mappings
come from `SDL_GAMECONTROLLERCONFIG`, `SDL_GAMECONTROLLERCONFIG_FILE`, or a
community `gamecontrollerdb.txt` in `pc/user/` (or at `ROOD_GAMECONTROLLERDB`).
Input is suppressed while unfocused. Gamepads connect and disconnect at any time;
disconnection clears held buttons and the next connected gamepad takes over. The
game sees a digital PS1 pad (analog pressure and rumble are not yet
implemented); the right stick drives the camera (below). To choose a gamepad, set `ROOD_CONTROLLER` to its device path
(`/dev/input/event*`, or the older `/dev/input/js*` name of the same device) or to
part of its name; set it to `none` to disable gamepad input. Keys and gamepad
elements can be rebound per PS1 button (settings `bind_key_<button>`,
`bind_pad_<button>`; see [Button prompts](#button-prompts)), and the buttons the
game shows follow the device used last and the bindings.
`pc/build/boot/controller_probe --hardware` reports the detected gamepad and its
current buttons. Automated mapping tests (`controller_probe`, also in CTest)
cover positional face buttons, shoulders, Start/Select, stick clicks, the stick
dead zone, overlapping D-pad/stick input, and triggers. Physical button presses
still need hands-on validation. The intro movie plays before the title
(see [Intro movie and CD drive](#intro-movie-and-cd-drive)); `--skip-movies`
skips it. Remaining untranslated routines exit with a named `[UNIMPLEMENTED]`
diagnostic.

Memory cards are standard 128 KiB raw card images (`.mcd`, the format PS1
emulators use), so saves move between this port and an emulator. Slot 1 is
`pc/save/card1.mcd` (always inserted, created on the first save); slot 2 is
`pc/save/card2.mcd` and is inserted only when that file exists. Override either
with `ROOD_CARD1` / `ROOD_CARD2`. Save at a save point (Data > Save) and load
from the title screen's Continue. `pc/save/` is ignored by git.

For a deterministic headless title checkpoint:

```sh
pc/build/boot/rood_boot --skip-movies --title-checkpoint
```

It exits successfully only when the original title code reaches its input loop.
`pc/build/boot/loading.ppm` (in `$ROOD_OUT` when set) contains the latest captured display (including the
title screen at this checkpoint). `--loading-checkpoint` is an earlier checkpoint
and does not establish title boot. `--fast` disables window pacing for tests.
Without `--skip-movies`, the intro movie plays first (about 38 seconds of
video before the title; Start skips it).

### Right-stick camera

The right stick turns and zooms the game's own follow camera; the PS1 pad
the game sees stays digital and L1/R1 turn in 45-degree steps as before.
On by default (it does nothing until the stick moves); Graphics > Controls
in the editor, `camera_stick_*` in the settings file, RPC `camera_stick`.

| Input | Keyboard | Does |
| --- | --- | --- |
| Right stick left/right | J / L | turn around Ashley, speed proportional to the deflection (after the dead zone and response curve) |
| Right stick up/down | I / K | zoom in/out (times the game's distance, within the zoom range and the room's size) |
| R3 (right stick click) | O | back to the nearest 45-degree line and the game's distance |
| L1 / R1 | Q / W | the game's 45-degree turns (from between two lines: the nearest line) |
| Select | Tab | the game's near/far distance toggle (the zoom scales it) |
| Start | Enter | the game's first-person view (R3 too when "R3 resets the camera" is off) |

| Setting | Default | |
| --- | --- | --- |
| `camera_stick` | 1 | the stick turns and zooms |
| `camera_stick_speed` | 120 | degrees per second at full deflection |
| `camera_stick_deadzone`, `camera_stick_curve` | 0.15, 1.6 | dead zone (share of the travel); response exponent (1 linear) |
| `camera_stick_invert_x`, `camera_stick_invert_y` | 0, 0 | invert turning, zooming |
| `camera_stick_zoom`, `camera_stick_zoom_speed` | 1, 1 | stick up/down zooms; doublings per second |
| `camera_stick_zoom_min`, `camera_stick_zoom_max` | 0.6, 1.5 | zoom range, times the game's distance (out also at most 1 + the room's size / 2304) |
| `camera_stick_zoom_pitch` | 5 | degrees steeper fully out, flatter fully in |
| `camera_stick_collision` | 1 | zoomed out beyond the game's distance, stop 64 units before the first drawn room surface (cutaway walls excepted) |
| `camera_stick_r3_reset` | 1 | R3 resets the camera (off: R3 is the game's) |
| `camera_stick_idle_reset` | 0 | seconds without stick input before the camera resets itself (0 off) |
| `camera_stick_keys` | 1 | the keyboard keys above |

The stick turns the game's own camera yaw, so the cutaway walls (facing
masks), Ashley's camera-relative movement and room entries follow it; it
works wherever the game lets L1/R1 turn (the field, battle mode, the
targeting sphere) and not during events, combat camera moves, menus or the
first-person view, where the zoom also eases back to the game's distance.
`pc/src/camera_stick.c` explains the game's camera and the design;
`pc/tests/camera_stick_smoke.py` and the CTest `camera_stick_probe` test it.
Hands-on stick feel (speeds, curve, dead zone) still needs a playtest.

### Weapon slots (optional)

A quick weapon swap ported from eremtsov's
[Vagrant Story QoL mod](https://github.com/eremtsov/rood-reverse) (CC0; the
`qol-mod-classic` branch, which adds only this feature). **Off by default**:
settings key `qol_weapon_slots 1`, editor Graphics > Gameplay, RPC
`weapon_slots {enabled}`. With it off the game is unchanged. Only the weapon
slots are ported, none of the mod's other control changes.

| Input | Keyboard | Does |
| --- | --- | --- |
| Hold L2 + R2 | 1 + 2 | the L2 shortcut overlay shows the 8 weapon slots by name |
| ... then Circle, Triangle, Square, Cross / Up, Down, Right, Left | Z, S, A, X / arrows | equip that slot's weapon and shield (slots 1-8) |
| ... then Select | Tab | assign mode (orange labels, a prompt): the next slot button stores the equipped weapon there, with the equipped shield if it is one-handed; a weapon in another slot moves, its own slot clears |
| ... then Start | Enter | the equipped shield becomes the shield of every occupied one-handed slot (none equipped: those slots lose theirs) |
| R2 + Select | 2 + Tab | the equipped weapon (and shield) into the first free slot, or out of its slot |

- A slot is an exact loadout: swapping equips its shield or takes the shield
  off; a two-handed weapon always takes it off, and a shield no longer in the
  bag counts as none. With the weapon drawn, Ashley sheathes and draws the new
  one; its name shows in the information bar for about 2.5 s.
- Swaps happen only at safe moments: not in menus, cutscenes or screen
  transitions, not mid-attack or during chain/defense ability timing, not while
  something loads. The D-pad slots react only to the real D-pad (not the left
  stick the game folds into it). L2 alone keeps the normal shortcuts.
- No conflict with the right-stick camera: it uses R3 (reset) and the stick;
  R2 + Select swallows only that Select press (no near/far camera toggle).
- **Save compatibility:** the slots use the mod's format, so memory cards move
  freely between rood-boi, the mod (DuckStation) and the unmodded game. A
  weapon's slot (1-8, 0 none) is the 4th gem byte of its inventory record
  (`weapons[i].gems[3]`, which no grip uses); a shield's 4th gem byte
  (`shields[i].gems[3]`) is a bit mask of the slots it belongs to (bit N =
  slot N+1). The unmodded game leaves both 0 and ignores them. Version 2
  favourites (several weapons marked 1) are moved to slots 1, 2, 3, ...
- **Container fix:** saving the Container's contents remapped all four gem
  bytes of weapons and shields as gem indices, scrambling the slot bytes. As
  in the mod it now remaps three (MENU7 `_copyContainer`, `#ifdef ROOD_PC`).
  This applies whether or not the option is on (a card may come from the mod):
  for the unmodded game's data it changes nothing, because the fourth byte is
  always 0 and the original loop skips zero bytes. Selling a shield or
  combining it in a workshop still drops its slots, as in the mod.

`pc/src/weapon_slots.c` documents the details; `pc/tests/weapon_slots_smoke.py`
tests it (option off, swaps, assign mode, shields, saving and reloading).

### Modern menus (in progress)

`ui_menus 1` (settings, RPC `ui_menu {modern}`) replaces the game's top-level
command list with a tabbed shell drawn by the PC runtime (`pc/src/ui`, design
in `pc/docs/Modern_Menus.md`): L1/R1 switch tabs (Ashley, Equipment, Items,
Abilities, Magick, Leá Monde, System; tabs with nothing unlocked yet are
skipped), Up/Down move, Circle opens, Cross or Triangle close. Every row still
opens the game's own screen for now (native screens replace them one by one),
and the game keeps deciding what is allowed: a greyed row (e.g. Break arts
with the weapon sheathed, Magick at 0 MP) says why in the card and still
opens, as in the original, where the game's screen greys what is off. Only
the Triangle menu is replaced: L2 shortcuts, loot, containers, the save
point and ability-learned messages stay the game's own. System > Menus
switches back to the original menus from the next open (the current menu
session keeps the shell). Without the Dear ImGui overlay (backend init
failed) the original menus are used. With the option off (the default) the
game is unchanged.

The Equipment tab is native: Gear lists the eight slots (Circle opens a list
of what fits with a STR / INT / AGL / DP and class / affinity compare against
what is worn, Square takes off), Quick swap (L2 / R2) shows the eight field
swap buttons as two diamonds and assigns any weapon in the bag plus a shield.
The mouse works in the shell: hover focuses, a click opens or confirms (tabs,
sub-tabs and hints too), the wheel moves through lists, a right click is
Cross.

The Ashley tab is native too: a summary (HP / MP / RISK, the weapon and
shield, STR / INT / AGL, the condition of each limb, status effects with the
time they have left, the area, room and play time) beside Ashley's live 3D
model. The right stick (with the camera stick's settings; J / L with
`camera_stick_keys`) or a mouse drag over the model turns it; it never turns
on its own. Circle on the weapon or
shield opens it on Equipment; Square opens the game's original Status screen.
The model needs room in the game's heap and stays hidden while the editor's
free camera is on (the screen says so).

The Abilities tab is native: sub-tabs Chain, Defense and Break Arts (L2 / R2).
Chain and Defense show the three button slots and the learned abilities with
their RISK cost and description; Circle on a slot picks one for it, and
Circle / Triangle / Square on a listed ability put it on that button
(pressing its own button again takes it off), as the original menu does.
Break Arts lists the weapon categories (the equipped one first) with their
arts: learned ones with HP cost and description, the rest as "???" with the
kills still to go. Circle on a usable art of the equipped weapon closes the
menu and casts it through the game's own targeting; a greyed art says why
(weapon sheathed, not enough HP, another weapon type). Cancelling the target
opens the original Break Arts list once, as the game does; Cross there comes
back to the shell.

- `pc/src/ui/menu_bridge.c` (battle builds) takes over `func_800C930C`, the
  top level, from one `#ifdef ROOD_PC` hook; `ui_shell.cpp` is the navigation
  state (ctest `ui_shell_probe`), `ui_draw.cpp` draws it on Dear ImGui's
  background draw list inside the game picture with the open fonts and the
  button prompt icons.
- Theme: `pc/assets/ui/theme.json` (colours, sizes, timings, font files;
  reloaded live; `ROOD_UI_THEME` overrides the path).
- `pc/tests/ui_menus_smoke.py`: off = the game's menu; the shell's tabs, an
  original screen (Status) and back, System > Menus (from the next open), an
  L2 shortcut (stays original), a greyed Break arts opening, closing, 16:9.
- `pc/tests/ui_ashley_smoke.py`: the Ashley summary against RAM, the live
  model (shown, turned by the stick and a drag, rebuilt after an equip, the
  heap check, the free camera), the original Status and back, the world's
  camera and freeze state given back, 16:9, a soft reset.
- `pc/tests/ui_abilities_smoke.py`: Chain / Defense assignments byte-for-byte
  equal to the original menu's, the lists and Break Arts against RAM, greyed
  arts with their reason, casting (confirm and cancel: the original list on
  the equipped category with the cast art highlighted), no cast while the
  menu is still loading.

## Intro movie and CD drive

The title's own movie code (`src/TITLE/TITLE.PRG/22C.c`) plays MOV/TITLE.STR
(320x224 MDEC frames at 15 fps with interleaved 37.8 kHz stereo XA audio) on
host versions of the SDK calls it makes:

- `pc/src/cd_drive.c` is the CD drive: libds commands (Setloc, Setmode,
  Setfilter, ReadN/ReadS, Pause, Getloc..., DsPacket/DsCommand with
  completion callbacks one tick later, DsReadyCallback and the ready system,
  DsGetSector), DsRead2 streaming and the libcd streaming library
  (StSetRing, StSetStream, StGetNext, StFreeRing, StGetBackloc, ...). A read
  delivers sectors from the vsync tick at the disc's rate (150 sectors a
  second at double speed), so the movie and its audio advance in real time as
  on the PS1. Stream data sectors are assembled into whole frames in the
  game's ring buffer; while the drive is filling it, a StGetNext poll lets a
  vertical blank pass (the PS1 loop busy-waits on the CPU meanwhile).
  Ordinary file loads stay synchronous (`pc_disc_read`).
- `pc/src/xa_audio.c` decodes XA-ADPCM sectors that the drive plays in
  real-time mode (filtered by file/channel with DslModeSF), applies the
  DsMix/CdMix attenuators, resamples to 44.1 kHz and feeds the software SPU's
  CD-audio input, which applies the CD volume and enable bits the game sets.
  The movie's audio track keeps playing under the title menu until the game
  stops the stream, as on the PS1. The decoder matches ffmpeg's `adpcm_xa`
  sample for sample.
- `pc/src/mdec.c` is libpress over a software MDEC (psx-spx: dequantisation,
  integer IDCT with the uploaded scale table, YCbCr to 4/8/15/24-bit RGB).
  `DecDCTvlc2` is a label-by-label translation of Sony's MIPS
  (`libpress/VLC_C.s`, BS v2 and v3) over the table from the game's own
  `DecDCTvlcBuild` (`libpress/BUILD.c`, compiled natively). DecDCTout
  decodes before it returns and then raises the output callback; callbacks
  that start the next strip run from a loop, not recursively.
- `PutDispEnv` passes `isrgb24`; the presenters (OpenGL, software and the PPM
  screenshots) show 24-bit display mode from native VRAM, since the scaled
  hardware target only holds 15-bit pixels.

`pc/build/boot/movie_probe` (run by the builder) checks synthetic macroblocks,
XA sound groups and the first 60 frames of TITLE.STR; `--dump DIR` and
`--wav PATH` write frames and audio, which matched ffmpeg's `psxstr` decoding
at about 49-50 dB PSNR (rounding) and exactly for audio.
`python3 pc/tests/movie_smoke.py` plays the movie headless: 24-bit frames
advancing at 15 fps, XA audio in the recorded output, Start to the title and
New Game after the stream stops. `Game.launch(movies=True)` (or `rood_rpc.py
launch --movies`) plays it; the other tests skip it.

## Renderer and developer overlay

The platform layer is SDL 3 (`pc/src/boot_window.c`, `boot_gamepad.c`,
`audio_output.c`, `gl_present.c`, `dev_overlay.cpp`). SDL uses native Wayland
where the compositor supports it well (it needs the fifo-v1 protocol for
dependable presentation, e.g. GNOME 48 and later) and X11 otherwise, including
XWayland; `SDL_VIDEO_DRIVER=wayland` or `x11` chooses explicitly. If OpenGL is
not available on Wayland (the 32-bit NVIDIA driver cannot create EGL window
surfaces there), the game reopens on XWayland rather than a software surface,
which Wayland cannot show without OpenGL either. Installing the `:i386` Wayland
libraries alongside an amd64 `libdecor-0-0` from a PPA can make apt remove
desktop packages (multiarch versions must match): check apt's removal list. Window and mouse
coordinates are pixels on both. The window opens at the largest 4:3 size within
two thirds of the primary display's height (1440 x 1080 on a 4K screen, at least
960 x 720); `ROOD_WINDOW=WIDTHxHEIGHT` sets it, and `ROOD_UI_SCALE` sizes the
overlay (by default the desktop's content scale). Closing the window exits.
The frame loop paces itself to 59.94 Hz deadlines (`--fast` runs unpaced), with
swap interval 0. Sound goes to SDL's audio output (PipeWire, PulseAudio or ALSA)
at 44.1 kHz stereo, about 50 ms ahead, starting after a 2048-frame prebuffer;
`ROOD_AUDIO=none` is silent and `ROOD_AUDIO=wav:PATH` records instead.

The window uses an OpenGL 4.6 core context, or 3.3 core where 4.6 is not
offered (`pc/src/gl_present.c`; SDL loads libGL or libEGL at run time and
resolves the entry points in `gl_api.h`). Without OpenGL, or with
`ROOD_RENDERER=software` (`x11`, its old name, still works), a 640 x 480
software surface is presented instead. The
hardware rasterizer (`pc/src/gpu_hw.c`) mirrors every primitive, fill, VRAM
copy and upload into a VRAM render target at `ROOD_SCALE` times native
resolution (1-8, default 4; 0 disables it). The CPU rasterizer stays the
reference for native VRAM, so StoreImage, effects and texture decoding are
unchanged; textures are decoded from native VRAM in the shader and re-uploaded
per 64x256 tile only when a draw samples a tile the CPU changed. Colors keep
8-bit precision, the four semi-transparency modes map to GL blending (opaque
and translucent texels in separate passes), and the window is resizable with
4:3, stretch or integer-scale presentation and nearest or bilinear filtering.
Not yet mirrored: mask-bit testing (reported once if a game path uses it).
24-bit display mode (the intro movie) is presented from native VRAM.

Widescreen (`ROOD_WIDESCREEN=1`, the overlay checkbox or RPC `graphics`) is
16:9 Hor+: the GTE compresses projected X by 3/4 about the screen centre, so the
game's own screen-bounds culling admits the wider view, and the frame is shown
at 16:9. 2D primitives keep their 4:3 shape: the GTE logs every screen position
it projects, and the hardware rasterizer compresses primitives whose vertices
were not projected (HUD, menus, sprites) about the drawing area's centre. 2D
elements therefore sit within the central 4:3 area. 2D that stands for the
whole screen draws over the full 16:9 view instead (`pc_hw_set_bounds` in
`gpu_hw.c`): primitives spanning the screen's width (fades, flashes, the
battle-mode message bar), untextured shapes reaching well past its top or
bottom (wipes), full-height sprites at its edges (the map's background picture)
and screen copies (textures sampling the frame buffer where they draw: the
map's rippling transition, menu backgrounds). In widescreen the frame buffers
are read back from the hardware target when the game samples or reads them, so
such copies hold the widescreen picture. `ROOD_WIDESCREEN_DEBUG=1` logs them;
`ROOD_WIDESCREEN_COVER=0` compresses them as before (comparisons;
`pc/tests/widescreen_smoke.py`).

The layout of the rest (`ROOD_WIDESCREEN_LAYOUT=0` or RPC `graphics
widescreen_layout` turns it off for comparisons): menus stay laid out for
the 4:3 middle, but untextured shapes reaching one edge of the screen (menu
rows, title bars such as COMMAND or MAP / Wine Cellar, the RISK bar)
stretch to that edge of the view; compressed 2D is clipped to the
compressed drawing area, so what the game parks beyond the screen's edges
stays hidden; 2D whose vertices the GTE projected this frame (the map
screen's rooms, effects drawn as packets) is not compressed a second time
(the GTE keeps logging projections in widescreen for this). 2D the game
places at a projected point (damage numbers, ability and timing labels over
characters, target lines, icons over actors, the map screen's room icons)
takes un-narrowed projections (`pc/src/widescreen.h`,
`PC_GTE_UNNARROWED`/`pc_gte_unnarrow_x`), so after compression it sits on
the narrowed 3D. Screen furniture at the edges is anchored by packet ranges
from the game code (`PC_ANCHOR_PACKETS`): the status display and the
timing/chain labels beside it at the left, BATTLE MODE at the right, the map
screen's controls help and compass at the left. A still that covers the
screen (the idle cutscene's VKP Headquarters) is magnified to fill the view
in its proportions (`PC_ANCHOR_FILL`; its top and bottom eighths fall
outside). A picture the game loads straight into the frame buffer (the title
screen, its copyright; the game code marks those LoadImages, `PC_UPLOAD_AS`)
is drawn compressed like the 2D over it; StoreImage
reads back only what native VRAM lacks (native 3D), so the CPU's own 2D
round trips stay 4:3; a texture sampling the frame being drawn (the
memory-card screens' text box) reads the target back first. Movie frames
(24-bit) are pillarboxed. RPC `graphics widescreen_trace` (N ordering
tables) logs every 2D primitive and how it was placed.

What the sides show. Nothing the game projects is culled to the 4:3 frame:
the room renderer tests polygons against the screen in narrowed X (the whole
16:9 view) and otherwise only by depth (the near clip, which is a depth, not
a side test), and characters and effects only by depth; a census of every
room polygon in the opening's and the town's cutscenes found none left out
for lying beyond the 4:3 sides. Where a 16:9 side shows a set that stops
(a wall or the water ending in a diagonal edge, black or sky beyond), the
modelled room ends there: the PS1 camera simply never looked that way.
Some of the 3D view is 2D, though, placed by the game in 4:3 screen space:
the sky panorama of outdoor scenes (32x32 tiles of a 1024-wide picture
scrolled with the camera, BATTLE `func_8009147C`; the opening's view of the
manor) and the weather particles (Snowfly Forest's floating motes, snow and
falling glints, `146C.c` `func_8008FAC8`..`func_80090434`). These were
compressed into the 4:3 middle and clipped there; now the panorama and the
snow draw compressed like the narrowed 3D but clipped to the whole view
(`PC_ANCHOR_SCENE`), the game draws the panorama's tiles and accepts the
particles as far past the 4:3 sides as the view reaches
(`PC_WIDESCREEN_MARGIN`, `SKY_LEFT`/`SKY_RIGHT`, `WEATHER_LEFT`/`WEATHER_RIGHT`,
all `ROOD_PC` and widescreen only), and the screen-space motes are drawn over
the whole view (`PC_ANCHOR_COVER`). (The rain is projected lines and already
covered the view. Particles past the 4:3 sides now count as on screen, so in
widescreen their motion and the random numbers they draw follow the wider
view, as the rain's always did; 4:3 is unchanged.) `ROOD_WIDESCREEN_SCENE=0` or RPC `graphics
widescreen_scene` turns this off for comparisons
(`pc/tests/widescreen_scene_smoke.py`).

The fill rule for the sides: they show what the game draws there and nothing
else. Where no geometry reaches, that is the game's own background: the
sky it draws behind outdoor scenes (the panorama, or the gradient sky dome
of the town's cutscenes, both the game's) and otherwise the frame's clear
colour (black, or a room's fog colour such as Snowfly Forest's grey), as on
the PS1 wherever its set ends inside the 4:3 frame. The renderer's own sky
radiance (Sun and sky, below) lights surfaces and the haze; it never paints
pixels where nothing is drawn, so the sides are never filled with it.

`SetDispMask(0)` shows black until `SetDispMask(1)`, as on the PS1 (it
blanks the display while the game loads, e.g. skipping the idle cutscene);
`ROOD_DISPLAY_MASK=0` ignores it, RPC `status` reports `display_masked`.

The same log stores each projected vertex's GTE view depth (SZ), so 3D
primitives carry depth into the hardware path: the target has a depth buffer
written in the game's draw order (never tested; the PS1 sorts back to front),
with 2D/UI at depth 0 and cleared areas at far. This is the input for post
effects (depth of field, fog, ambient occlusion, lighting). With depth known,
textures can also be mapped perspective-correct instead of the PS1's affine
mapping (on by default; `ROOD_PERSPECTIVE=0` turns it off). The overlay and RPC `graphics` (`perspective`,
`show_depth`) toggle both; the depth view shows far as black, UI as magenta
and 3D as grey (near is brighter).

Native room geometry (on by default; `ROOD_NATIVE_ROOM=0`, the editor's Graphics
checkbox or RPC `graphics native_room` switch it) draws the room from meshes built once per room
(`pc/src/room_native.c` through the render API in `pc/src/render.h`) instead of
reconstructing it from packets: exact per-pixel depth and normals, real
near-plane clipping, the PS1's per-polygon rules (back faces, ordering-table
range, distance UV halving) and the game's group visibility and cutaways. The
list is drawn at its place in the ordering table and hides the packets drawn
after it where they are behind it. Semi-transparent room polygons stay
packets; `2` also stops building the opaque room packets for the CPU's
reference VRAM (less CPU, but StoreImage readers then miss the room). RPC
`render_stats` and the Diagnostics tab count native and packet submissions.
Design notes: `pc/docs/Native_Rendering.md`.

Optional post effects run in the presentation pass and never touch UI pixels
(depth 0): depth of field (auto focus from the screen centre, or a fixed SZ),
tilt-shift (a sharp horizontal band, blur above and below) and depth fog.
They are off by default; the overlay's Post effects section tunes them, and
RPC `graphics` takes `post` (bit 0 DoF, 1 tilt-shift, 2 fog).

### Anti-aliasing

Graphics > Lighting quality > Display (not part of the lighting presets),
the settings key `aa` and RPC `graphics` `aa` choose the methods, written as
one value joined by `+` (`pc/src/antialias.c`); every one works on Vulkan and
OpenGL. None of them filters textures: the PS1's texels stay one nearest
texel per pixel (TAA is not implemented yet).

| `aa` word | What it does |
| --- | --- |
| `off` | nothing |
| `fxaa` | FXAA on the lit image (the default, as before): cheap, but it smooths any contrast, texel steps and the HUD included |
| `smaa` | SMAA 1x (the reference shader, `pc/third_party/smaa`) on the lit image, with its colour edges kept only where the geometry breaks (`aa_geometry_edges`, on): texels inside a surface and 2D/UI pixels never blend |
| `msaa2`, `msaa4`, `msaa8` | multisampled 3D: rooms, characters, effects meshes (and 3D packets) get coverage anti-aliasing; still one shading sample per pixel |
| `ssaa`, `ssaa1.5` ... `ssaa4` | supersampling: the internal resolution raised to factor x the view (up to 8x), the lit image drawn at that resolution and filtered down (`aa_downsample`) |

A post-process filter combines with MSAA and supersampling, e.g. `msaa4+smaa`.
Old settings files' `antialiasing 1/0` still load (FXAA/off); RPC `aa`
true/false still means FXAA/off.

**MSAA** (`gpu_hw.c` "MSAA", `vk_hw.c`, `ms_resolve.glsl`): the VRAM target
stays single-sampled, so everything that reads VRAM (StoreImage, screen
copies, the replay copy for interpolation, picking, the path tracer) is
unchanged. The 3D is drawn into multisampled twins of the target over one
drawing area at a time: the first native list in an area loads the target
there into every sample; native lists, their translucent markers, and the
fills and packets inside the area then draw into the twins, 2D with every
sample at the pixel centre (`VK_EXT_sample_locations`; OpenGL:
`GL_MULTISAMPLE` off), so the HUD and text are bit-identical; anything else
that touches the area (VRAM copies and uploads, readers, the replay copy,
presenting) first resolves it. Sample shading is off and the 3D varyings are
`centroid`: coverage is multisampled, texels are not. The resolve averages
the colour (the anti-aliased picture with the game's baked light) and keeps
one representative surface per pixel for the G-buffer the present pass
lights from (the surface most samples agree on, among lit ones; albedo
summed over the lit samples, so dynamic light fades with their coverage):
lighting stays at pixel rate and is right on interior pixels and on
silhouettes against the void or the HUD; where two lit surfaces share a
pixel, its dynamic light is the main surface's (the averaged colour and
albedo still blend). The native surfaces' depth that shaders read
(occlusion, object lists' second pass) stays single-sampled (sample 0).
Interpolated frames are drawn the same way into the replay target. The
twins cover the target from (0, 0) past the drawing area (about 2560 x 1024
at 4x: 0.25 GB at 4 samples); at most 1 GiB, fewer samples past that. RPC
`graphics` reports `msaa_active` (samples the 3D was last drawn with) and
`msaa_max`. Polygon seams inside a surface are edges to MSAA too: where two
polygons' textures meet, the seam's pixels blend them.

**SMAA** runs on the lit image before the depth of field (as does FXAA now;
alpha, the blur radius, passes through), at the view's size or, with
supersampling, the internal resolution.

**Supersampling**: `pc_hw_supersample` raises the internal resolution to
`ceil(factor x view height / display height)`, at most 8 (the setting
`internal_resolution` stays as chosen; `scale_setting` in RPC `graphics`),
and the present pass then lights the display area at the internal
resolution and filters it to the view (`downsample.glsl`): `aa_downsample`
`area` (default: an exact box over the destination pixel's footprint),
`lanczos` (Lanczos-3, clamped to the pixels underneath against ringing) or
`bilinear`. The 8x cap limits it: at 1440 x 1080 `ssaa2` gets 1.78x per
axis, at 4K (2160 lines) the cap is below the view and supersampling does
nothing. The older Graphics `Filter` (Box) only averages the target's colour
under each window pixel, lighting one texel.

Cost and pictures per method: `pc/tools/aa_bench.py` (RPC `perf` with Vulkan
GPU sections, and the same frame redrawn under each mode, RPC `redraw`);
`pc/tests/aa_smoke.py` checks every method.

GPU time each method adds (ms per frame, Vulkan GPU sections, save room with
a light, internal resolution 4x; "4K" is a 3840 x 2160 window, a 2880 x
2160 view):

| Method | RTX PRO 6000, 1440 x 1080 | RTX PRO 6000, 4K | Intel Arc iGPU (ARL), 1440 x 1080 | Intel, 4K |
| --- | --- | --- | --- | --- |
| `fxaa` | 0.035 | 0.10 | 1.3 | 6.0 |
| `smaa` | 0.036 | 0.09 | 1.6 | 9.7 |
| `msaa2` | 0.10 | 0.11 | 5.2 | 8.0 |
| `msaa4` | 0.12 | 0.10 | 9.1 | 10.4 |
| `msaa8` | 0.13 | 0.13 | 19 | 16 |
| `msaa4+smaa` | 0.15 | 0.18 | 10.6 | 14 |
| `ssaa1.5`, `ssaa2` (8x: 1.78x per axis) | 0.27 | - (the 8x cap is below 4K) | 21 | - |

MSAA's share on the RTX: loading the twins 0.025, copies of the surfaces'
depth 0.05, their single-sampled copy 0.012, the resolve 0.027 (8x: 0.046);
on the iGPU each is 1-2 ms at 4x (it pays for multisampled bandwidth). On
OpenGL (RTX) the methods cost about the same as on Vulkan; OpenGL on the
Intel iGPU, which also keeps the single-sampled surfaces in step after each
object list, takes 25-40 ms for MSAA (not recommended there).

Defaults: the built-in value (and tests) stay `fxaa`. A first run with a
settings file (`pc/run-game.sh`) picks for the GPU: `msaa4+smaa` on a
discrete GPU (edges smooth, texels and HUD exact, ~0.15 ms on the RTX),
`smaa` on an integrated one (FXAA's cost, but texels stay crisp); a
settings file that has `aa` (or the older `antialiasing`) keeps it.
`ROOD_AA=...` overrides both. These are Display settings, independent of
the lighting presets; per preset the sensible picks are: Original/Low
`smaa`, Medium `smaa` (iGPU) or `msaa4+smaa`, High/Ultra `msaa4+smaa`
(`msaa8+smaa` on fast GPUs, `ssaa2` only for screenshots below 4K).

| Key | Default | Meaning |
| --- | --- | --- |
| `aa` | `fxaa` (first run: per GPU) | the methods (above) |
| `aa_downsample` | `area` | supersampling's filter: `area`, `lanczos`, `bilinear` |
| `aa_geometry_edges` (RPC only) | true | SMAA's edges only where the geometry breaks |

Escape (or F1) toggles the editor (`pc/src/dev_overlay.cpp`, Dear ImGui
vendored under `pc/third_party/imgui`, MIT), laid out like Unreal's: a menu
bar (File: Save / Reload area and settings; Edit: Undo, Redo, Delete, Undo
history; View; Help > Keyboard shortcuts; Play/Pause, Step), a one-row
toolbar over the viewport (undo and redo arrows, Move/Rotate, snapping,
view mode, Show, See into rooms, Lighting quality, Inspect, `?` for the shortcuts),
and an Outliner and a Details panel docked on the right; the game image fits
the space left. While it is open the keyboard and mouse belong to the editor (a
gamepad still plays; Dear ImGui's own SDL 3 backend, `imgui_impl_sdl3`, feeds
it). Right-drag in the view flies a free camera (WASD/QE, Shift faster; the
pointer is hidden and held in relative mouse mode meanwhile, and returns where
the drag began) that drives the game's own live rendering: func_8007ACB0 builds
the view from it (RPC `editor_camera` sets or reads it); Play returns to the
game camera. The Outliner lists the room's lights and the game's glows (the
path tracer's emitters) by name ("Save point glow", "Flame (candle) #2",
"Embers #3"), with mute and solo; both show in the view (a glow's diamond sits
at its smoothed centre) and can be selected there. Each glow is one source
(a fixture's room polygons, a particle system, an object) with an id that
stays for the room visit; its position, colour and size are smoothed, so
flickering sprites no longer make it jump ([docs/Glows.md](docs/Glows.md)).
Glows come in three kinds: fixtures (candles, torches, lanterns, room
flames), magic (the save point) and transient effects (attacks, spells, hit
sparks, particles), which never cast shadows, are not listed one by one
(an "Effect glows (N active)" row) and are controlled as a kind. Per kind:
enabled, intensity, reach, cast shadows, smoothing (Details > Lighting >
Game glows by kind; room settings `glow_<fixtures|magic|effects>_<enabled|
intensity|reach|shadows|smoothing>` with project defaults; RPC `graphics`
takes the same keys). A fixture's or magic glow's overrides (enabled,
intensity, tint, size, reach) are saved with the room, by the glow's key. W/E pick the gizmo, I inspects, Ctrl+S saves the area;
Help > Keyboard shortcuts (or the toolbar's `?`) lists every binding, with a
search field.

**Undo and redo** (`pc/src/editor_undo.cpp`): Ctrl+Z undoes, Ctrl+Y or
Ctrl+Shift+Z redoes; Edit names the action ("Undo Move Candle 2") and lists
the history (click an entry to go back or forward to it); the toolbar has
arrows. Every editor edit is a transaction: lights and light actors (add,
place, delete, duplicate, Alt+drag copies, move, rotate, every Details
value including prefab parameter overrides, fixtures, Light fixture / Light
all fixtures, Break into lights), glow overrides and mutes, the room's
Lighting, Glows-by-kind and Atmosphere values, texture effect overrides,
room group visibility, and graphics and lighting quality settings changed
in the editor (a preset with the keys it set). A gizmo, slider or drag
field held from press to release is one transaction, a typed value one on
Enter; Escape leaves nothing. Undo puts back only what the action changed,
with the selection it had. Changes are found by comparing snapshots of the
editor's data each frame, so no edit path has to register itself. The
history is per room visit, up to 200 steps: entering another room, Reload
area or the room file changing on disk clears it (its states belong to the
room file as loaded, and Unreal likewise clears it on loading a map);
saving keeps it. Not undoable: the editor's view preferences (overlays,
snapping, See into rooms: the `editor_*` settings), prefab files (Apply to
prefab, Save as prefab, the prefab editor) and Use as the project default.
**Delete** (or Backspace) deletes the selected light(s) or light actor.

**Typing values**: a number field takes a click (without dragging; a drag
scrubs it) to type a value, a slider a double-click (or Ctrl+click); Enter
commits, Tab commits and moves to the next field, Escape cancels the edit
(the editor stays open). While a field has the keyboard no editor shortcut
fires (W/E, I, L, C, F, Delete, Ctrl+S, Ctrl+Z, RMB+WASD/QE flight) and no
key reaches the game: the overlay hands every key to the field, and the
shortcuts check Dear ImGui's `WantTextInput` (and a held item).

The viewport draws overlays over the lit picture (`pc/src/editor_overlays.cpp`;
toolbar Show, or the View menu): the room's edges (wireframe; a quad's
diagonal is not an edge) and points, and the characters' wireframes. They
follow what the view draws (groups hidden in the Outliner, black shells, the
cutaway walls and the cut to focus leave theirs out) and are depth-aware:
edges and points behind other room surfaces, or only on back faces, are
drawn dimmed (Show > Hidden lines sets how much; 0 hides them). The hidden
test casts rays against a BVH of the shown room polygons a few milliseconds
per frame, so a still view costs nothing; the overlays only draw while the
editor is open.

Moving a light snaps like Houdini or Unreal: **Grid** (the step next to it;
rotations use the rotation step), **Point** (the room point nearest the
cursor within a search radius, visible points first, the nearest to the
camera on ties), **Edge** (the nearest point on an edge; with Edge midpoints
on, its middle when near), **Surface** (the surface under the cursor, held
off it along its normal by an offset). A point or edge target wins over the
surface, which wins over the grid; holding **V** while dragging toggles point
snapping (and, without a drag, shows where a drag would snap), Ctrl inverts
grid snapping. While snapping to a point the target lights up with the
points and edges around it, brighter the nearer they are (falloff over the
highlight radius), with the snapped coordinate next to it; points and edges
appear during point/edge snap drags even with the overlays off. Shift+click
and Ctrl+click (move or add a light) land on the snapped point or edge, else
off the surface by the same offset. The snap settings (the arrow after the
toggles) hold the steps, midpoints, search and highlight radii and offset.

These switches persist with File > Save settings:

| Key | Default | Meaning |
| --- | --- | --- |
| `editor_show_lights` | 1 | light and glow icons in the view |
| `editor_wireframe`, `editor_points` | 0 | room edges / points |
| `editor_character_wireframe` | 0 | characters, weapons and props |
| `editor_occluded_opacity` | 0.25 | opacity of hidden edges and points |
| `editor_snap_show` | 1 | points and edges appear during point/edge snap drags |
| `editor_snap_grid`, `editor_snap_move`, `editor_snap_angle` | 0, 8, 15 | grid snapping and its steps (units, degrees; the snap settings offer 1-128 and 1-90) |
| `editor_snap_point`, `editor_snap_edge`, `editor_snap_midpoint`, `editor_snap_surface` | 0 | snap modes |
| `editor_snap_radius` | 14 | search radius around the cursor (pixels at UI scale 1) |
| `editor_snap_highlight` | 128 | highlight radius around a snapped point (world units) |
| `editor_snap_offset` | 64 | surface snapping's distance off the surface |
| `light_animation` | 1 | light actors' flicker and pulse (0 holds them still) |
| `environment` | 1 | the rooms' sun and sky (0: off in every room, for comparison; per room: Details > Atmosphere, "Sun and sky" below) |
| `fog` | 1 | the rooms' ground fog and fog volumes (0: off in every room, for comparison; per room: Details > Atmosphere, "Haze, ground fog, fog volumes and sun shafts" below) |
| `editor_cutaway`, `editor_cutaway_radius` | 1, 320 | See into rooms > Cut to focus (key C) and its radius at the focus |
| `editor_cutaway_opacity` | 0.3 | share of a cut surface still drawn as a ghost, with its edges (0: a hole) |
| `editor_cutaway_auto` | 1 | cut only while room surfaces hide the focus from the camera |

### Cutaway walls in gameplay

Vagrant Story hides every wall that faces the camera's side of the room (a
facing mask per wall group, one bit per 45 degrees of camera yaw), whether
or not it stands in the way. In rooms that are not a simple box that leaves
black void where a far wall belongs and walls Ashley walks into without
seeing them. Graphics > Display and rendering > Cutaway walls (settings
keys below, RPC `graphics`) chooses:

| Key | Default | Meaning |
| --- | --- | --- |
| `gameplay_walls` | `original` | `original`: the game's mask, exactly as on the PS1. `smart`: a wall group the mask hides is drawn unless it stands between the camera and Ashley (or the battle target) or would be seen in front of the play area; walls fade in and out over a few frames. Black wall shells stay hidden; cutscenes and the editor's free camera keep the original |
| `gameplay_walls_ghost` | 0 | smart: walls in the way are drawn as dithered ghosts with their edges instead of left out |
| `gameplay_walls_opacity` | 0.3 | a ghost's share of pixels |
| `gameplay_walls_radius` | 64 | world units around Ashley's body a wall must keep clear of |

Ray tracing and lighting always use the whole room either way.
pc/docs/Native_Rendering.md "Smart walls" has the details; RPC `room_groups`
reports each group's decision (`shown`, `occluding`, `cells`).

### 2D / UI: MMPX and sub-pixel text

Graphics > 2D / UI (RPC `graphics`, settings keys below) chooses how 2D
textures (HUD, menus, text, talk bubbles, icons; every textured primitive
the GTE did not project and no native draw covers) reach the internal
resolution. 3D textures always keep nearest sampling. Whatever the scale,
a 2D primitive samples only the texels the PS1 does: its texels are clamped
to its UV range, and flipped sprites drawn as polygons (U or V decreasing,
such as the enemies' emote bubbles facing left) take their texels at the
pixel's corner as the PS1 does, not one past the image (`pc_hw_set_flipped_uv`).

| Key | Default | Meaning |
| --- | --- | --- |
| `ui_upscale` | `nearest` | `nearest`: the PS1's blocks (original). `mmpx`: MMPX (McGuire & Gagiu 2021), the style-preserving pixel-art magnifier: curves, diagonals and corners reconstructed, palette, transparency and one-pixel details kept exactly |
| `ui_subpixel` | 0 | text glyphs drawn where the game places them in 16.16 fixed point instead of whole pixels (even spacing while talk bubbles zoom open and shut) |
| `ui_text` | `original` | `original`: the PS1's glyphs. `font`: the game's text drawn from open-licensed fonts (below) at the internal resolution (2x and up) |
| `ui_text_layout` | `line` | with `ui_text font`: `line` sets each line of dialogue and menu text (talk bubbles, menus, the information box) with the font's own spacing and kerning (below); `glyph`: each glyph inside the original's 12-pixel cell at the game's advance, for comparison |
| `ui_text_<style>_font`, `_stretch`, `_slant`, `_embolden`, `_aspect` | the chosen fonts | per style (`dialogue`, `menu`, `info`, `digits`): a TrueType file (relative to the repository root), horizontal scale, added shear, distance-field offset in em (bolder > 0), and for `digits` the least share of the font's own width/height a digit size may be condensed to (0.78; 0: any); saved only when changed |
| `ui_lines` | `joined` | 2D lines (GP0 0x40-0x5F: the map, the minimap, the targeting sphere, talk bubble outlines) at the internal resolution. `joined`: one PS1 pixel thick at any scale, joins closed, no light dots along outlines (below). `legacy`: the earlier one-pixel quads, for comparison |
| `ui_outline_boost` | 0 | 2D lines (and vector bubble outlines) 0-3 output pixels thicker: a bolder inked look |
| `ui_bubbles` | `original` | `original`: talk bubbles drawn from the game's packets. `vector`: each bubble the game builds drawn as one smooth shape (below) |
| `ui_bubble_outline` | 1 | `vector`: the outline's thickness in PS1 pixels (0.25-8; the overlay offers 0.5-4) |

Nothing is shipped: each region a 2D primitive samples (texture page, CLUT,
texel rectangle) is decoded from the game's own VRAM when drawn and
magnified once into an atlas (`pc/src/ui_upscale.c`): 2x per MMPX pass, so
2x, 4x or 8x for internal resolution 2, 3-4 or 5-8 (3x samples the 4x atlas),
on the GPU (Vulkan or OpenGL 4.3 compute, `pc/shaders/vulkan/ui_mmpx.comp`) or
on the CPU without compute (`ROOD_UI_UPSCALE_CPU=1` forces it). The texels are
PS1 colours (5:5:5 + semi-transparency bit, 0 transparent), and MMPX only
copies input texels, so the opaque/translucent passes and the colour key work
unchanged. Regions are cached by content; a region whose VRAM tiles nothing
wrote since is a hit without hashing, new content (a texture page rewritten,
text drawn) is magnified again. Each region is magnified clamped to its own
rectangle (sprites packed next to each other never bleed); strips a few
texels high (the title menu's waving labels) take three texels of the page
around them as context. Not magnified: regions inside a drawing area in use
(the framebuffer used as a texture) and regions whose content changes on
most frames. `ROOD_UI_DEBUG=1` reports atlas resets and bypassed regions, 2
every new region, 3 sub-pixel text. RPC `ui_upscale` reports the cache (`verify: true` reads
the atlas back and checks it against the CPU reference). The MMPX kernel is
ported from the authors' MIT-licensed reference ([THIRD_PARTY.md](../THIRD_PARTY.md)).

**Text in modern fonts (`ui_text font`).** The game keeps drawing its text
exactly as on the PS1 (which characters, where, colours, brightness
gradients, translucency, when a bubble types out or zooms); only the texels
of each glyph's box are drawn again at the internal resolution from a font
(`pc/src/ui_text.c`), through the same 2D atlas as MMPX, whatever
`ui_upscale` is. A 2D region is a glyph by its page, CLUT and texel
rectangle:

| Style | Where the game draws it | Font (`pc/third_party/fonts`) |
| --- | --- | --- |
| `dialogue` | talk bubbles: italic hand-lettered capitals, small capitals for lower case (font page (832, 0), glyphs 189-377, 12 x 12 cells, proportional advances) | Permanent Marker (Apache 2.0), slanted 0.12 |
| `menu` | menus, item and ability names, the information box (glyphs 0-188 of that page, 6-pixel fixed advance; menu rows come as 12 x 1 strips with a brightness gradient) | Asap Condensed Bold |
| `info` | wide outlined capitals: RISK ZERO, NEXT, the map screen's BACK / ROTATION / ZOOM (field UI page (768, 0) rows 76-95, found through the game's own width and position tables, kept from when the field's overlay holds them so the map screen's overlay does not lose them; also digits, : / % + - in PLAY TIME, NEXT, map percentages) | Chakra Petch Bold |
| `digits` | HUD numbers (HP, MP; old-style figures in two sizes and the small slash, row 0 of that page); the dot between current and maximum HP / MP is drawn as a disc of the original's size and place, outlined like the digits | Alegreya |
| labels | words the game draws as images (below): HP, MP, COMMAND, MENU, INFORMATION, CAUTION, BATTLE MODE, ... | the `info` font |

Each glyph is a signed distance field (64 pixels per em) made with stb_truetype
(from Dear ImGui), a few each frame ahead of use once the option is on (about
2 s for every style; text shown before then makes its own, a few ms each), placed on
the original glyph: the original's baseline and
cap height (measured from the game's VRAM on its H; digits on 0; small
capitals on its x), centred on the original glyph's ink, a per-style
horizontal scale, descenders squeezed into the 12-pixel cell as the PS1
faces do. The HUD's digit boxes are narrow (five texels of ink between
outlines) and old-style figures are round: fitted to the original's height
the large size (7 x 12) would be condensed to about 0.6 of the font's own
proportions (stretched tall), so each size shares one scale at which its
widest digit keeps at least `ui_text_digits_aspect` (0.78) of them; the
small size (0.9) is unchanged, the large one is drawn a little shorter.
Ink stays a pixel inside the box's edges, so neighbours at the game's
advances (5 and 6) never touch. Its coverage is mapped through the original glyph's own CLUT ramp
(index 1 up to the highest index the glyph uses; inside a one-pixel outline
of index 1 for the outlined styles), so palettes and the colour key are the
game's. Characters a font lacks and the pictographs (button symbols,
arrows, notes, stars) stay original. While a bubble zooms, its
glyphs are sampled nearest from the atlas like MMPX's.

**Lines (`ui_text_layout line`, the default).** The game sets its text a
glyph at a time in 12 x 12 cells at its own advances (about 6 pixels a
letter in menus and the information box, proportional in talk bubbles). A
font's letters in those slots space unevenly (an i floats in a wide slot,
wide letters collide, a period lands on the letter before it) and descenders
must be squeezed into the cell. So dialogue and menu text is set again a line
at a time (`pc/src/ui_text.c`, "string-level layout"): the glyph sprites the
game draws one after another (a text box's glyphs, a menu row's 12 one-pixel
strips per glyph, a zooming bubble's scaled glyph quads) are held back by the
hardware path until something else is drawn, grouped into lines (one style,
one cell top, one drawing area; a gap of more than three spaces or an overlap
starts a new segment: columns, separately placed strings) and each segment is
set from where the game's first letter's ink starts with the font's advances
and kerning, its spaces (each 6-pixel space the game left) the font's space
widened by half. A letter never runs past the game's own letter by more than
a pixel (tightened where the font is wider), and on every row its ink stays
half a pixel from its neighbour's (that wins). Each glyph is drawn whole, its
descenders below the cell, in the original sprite's colour, brightness and
blending (a strip-drawn menu glyph row by row with each strip's brightness, the
rows below the cell with the last strip's). A letter's place depends only on
the letters before it, so text the game types out never moves: each letter
appears where it stays, when the game draws it. Line breaks, colours,
timing and the menu cursor are the game's; lines start where the game's do
and end where the font's spacing takes them (usually a little shorter). The
original glyph sprites and the laid-out quads are both recorded for each
frame, so a redraw of the same frame can show either (RPC `ui_upscale
redraw` after `graphics {ui_text_layout}`). RPC `ui_text {lines: PATH}` writes
the lines laid out in the newest frame (each glyph's original place, advance
and ink, its laid-out pen and ink row by row) for tests.

**Labels.** Words the game draws as pictures rather than glyphs are known
by place (`pc/src/ui_labels.h`: the texture page, the word's box in it and
a hash of its CLUT indices; only coordinates, hashes and the words): 47 on
the field UI page (768, 0): HP, MP, RISK, WT, NO., WAITING for COMMAND!,
TIME, SAVE, COMMAND, MENU, NUM., WARNING, HELP, INFO., CLEAR, MAP, CAUTION,
EXIT, FACTORY, Locked, Container, CHECK, INFORMATION, Open, is,
Unlockable, the equipment slots and enemy classes (WEAPON ... LEGS, HUMAN
... EVIL), the affinities (PHYSICS ... DARKNESS) and WORKSHOP; 41 on the
battle page (448, 256): BATTLE MODE, MOVE, 200%, 50%, RISK ATTACK Parry,
STR INT AGL, the status changes (Quicken ... Regenerate, -up, -down), the
spell schools (WARLOCK ... ENCHANTER), MP, ITEMS BREAK ARTS CHAIN DEFENSE
(the L2 shortcut cross's labels), and that cross's connector dot (the 6 x 6
sprite beside each label), drawn as a disc of the original dot's size and
place in its lettering colour with the labels' outline (the HUD's HP / MP
dot is drawn the same way).
A 2D region is drawn from the font when every word it touches still has its
hash and all of its ink belongs to those words; the word is then set in the
`info` font with its kerning, at the original's cap height and baseline
(the commonest bottom of its letters, so descenders hang below), centred on
its ink and condensed only where it would be wider, in the original's
colours: the outline's CLUT colour (indices 1-3) and the lettering's (the
brightest colour the word uses often; the PS1 bevel and banding are not
kept: a font's letters do not fall on the original's rows), with the `info` style's one-pixel outline. Left original:
pictographs and icons (button symbols, SET / EQP on its button, the lock
and key icons, the compass letters N S E W, drawn as rotated strips), the
Japanese line on the field UI page, the + between status words, the save
screen's NEW DATA / USED / ERROR / NO DATA (8-bit pictures with a soft glow
that a font outline would lose), the save slots (the game draws them into
VRAM and shows the result as a 16-bit picture) and the location captions
of cutscenes. `ROOD_UI_DEBUG=3` prints each label drawn, `4` also 2D
regions on the glyph pages that are neither glyphs nor labels. RPC
`ui_text` reports the styles and counters (`labels`, `labels_drawn`),
takes style overrides and draws a single region (`render`, tests).

The fonts were chosen with `pc/tools/font_match.py`, which never ships
anything of the game: `capture` reads the glyphs from your running game's
VRAM (a save on `pc/save/card1.mcd`), `fetch` downloads OFL/Apache
candidates from the Google Fonts repository and `rank` places each candidate
exactly as `ui_text.c` does and scores it against the original glyphs (soft
IoU and chamfer distance of the ink at 1x, stroke weight, distortion), with
contact sheets and `report.txt`, all in the ignored `pc/build/font_match`.
Its runner-up fonts can be tried without rebuilding through the
`ui_text_<style>_font` settings. `digits` compares `ui_text_digits_aspect`
values per digit size (proportions kept, height against the original's,
score, the narrowest gap between neighbours; `digits.txt`, `digits.png`),
and `labels` finds each label word's exact box in the captured VRAM from
the approximate ones in the script and checks (or with `--write`
regenerates) `pc/src/ui_labels.h`.

### Button prompts

The game names PS1 buttons in its text (the font's triangle, square, circle
and cross pictographs: the information box, the quick manual) and with
16 x 16 button sprites (the four face buttons and L1, R1, L2, R2: the quick
manual's page keys, the name entry screen, the item menus' L1/R1, PRESS
BUTTON). With `button_prompts` other than `original` each one is drawn as the
key or gamepad button **bound** to that PS1 button, from Kenney's
[Input Prompts](https://kenney.nl/assets/input-prompts) icon fonts (CC0,
`pc/third_party/fonts/KenneyInputPrompts`); Graphics > 2D / UI > Button
prompts in the editor, RPC `button_prompts`.

| Key | Default | Meaning |
| --- | --- | --- |
| `button_prompts` | `auto` | `auto`: the device used last: key caps after a bound key, the gamepad's own labels after a gamepad button or stick (a gamepad connecting counts; disconnecting the last one goes back to the keyboard). `playstation`, `xbox`, `nintendo`, `keyboard`: always that family. `original`: the PS1's glyphs, exactly as before (nothing goes through the 2D atlas for them) |
| `button_prompts_style` | `color_outline` | how the icons look (no backing behind any): `solid` (a filled cap, the symbol knocked out), `outline` (ring and symbol, no fill), `color_solid`, `color_outline` (the same in the family's colours: PlayStation's teal triangle, red circle, blue cross, pink square; Xbox's green A, red B, blue X, yellow Y; Nintendo, key caps and other buttons neutral). Outline styles use each glyph's `_outline` variant; text pictographs keep the text's colour |
| `button_prompts_dpad` | `dim` | the L2 shortcut cross's D-pad (below): `cross` the family's whole D-pad in the style; `dim` the same with the arms that have no usable action beside them dimmed (no label, or the game's dimmed label: a locked Break Arts, items while unusable; weapon slots: an empty slot); `lit` the arms with a usable action filled and the others hollow in the outline styles (Kenney's `_<dir>_outline` glyphs joined), in the solid styles bright with the others' insides dim |
| `button_prompts_scale` | `0.7` | 0.4-1: each 16 x 16 button sprite's icon fits in a circle this fraction of its cell across, centred on the original's (the shortcut cross's face-button diamond has diagonal neighbours 9 PS1 pixels apart: at 0.7 they keep about 1.5 pixels apart; key caps and wide shoulder tabs come out smaller than round buttons) |
| `bind_key_<button>` | see Controls | the key(s) for a PS1 button (`circle`, `cross`, `triangle`, `square`, `l1`, `r1`, `l2`, `r2`, `start`, `select`, `l3`, `r3`, `up`, `down`, `left`, `right`): one or two names separated by a comma (`a`-`z`, `0`-`9`, `return`, `tab`, `space`, `escape`, `backspace`, arrows `up`..., `lshift`, `lctrl`, `lalt`, ..., `f1`-`f12`, `minus`, `comma`, `period`, `slash`, ...) or `none`; saved only when changed |
| `bind_pad_<button>` | positional | the gamepad element for a PS1 button: `south`, `east`, `west`, `north`, `leftshoulder`, `rightshoulder`, `lefttrigger`, `righttrigger`, `back`, `start`, `guide`, `leftstick`, `rightstick`, `dpup`, `dpdown`, `dpleft`, `dpright`, `misc1`, `paddle1`-`4`, `touchpad` or `none`; saved only when changed |

The gamepad family comes from SDL (`SDL_GetGamepadType`): PS3/PS4/PS5 show
PlayStation icons (Start/Select as the PS3's, Options/Share on a PS4,
Options/Create on a PS5), Xbox 360/One/Series show Xbox icons (360: Start and
Back), Switch Pro and Joy-Cons show Nintendo icons; generic, Steam and
GameCube pads get the Xbox labels SDL's standard layout uses. Because SDL
reads face buttons by position, the icon is always the physical button that
presses the PS1 button: on a Nintendo pad Circle (east) shows **A** and Cross
(south) **B**, on an Xbox pad B and A. Rebinding changes the icon with it
(`bind_key_circle c` shows a C key cap).

How: a 2D region the game draws is a prompt by its page, CLUT and cell (font
page (832, 0), CLUT row 222, glyphs 0xAE-0xB1 of either table; field UI page
(768, 0), CLUT (944, 223), the 16 x 16 cells at (0, 128), and the L2
shortcut cross's D-pad, 30 x 30 at (184, 160), drawn as the family's D-pad
when the directions are bound to one: the arrow keys on a keyboard, solid or
outline arms by the style, its arms lit by `button_prompts_dpad` from what
the game reports as it draws each label: `pc_weapon_slots_label`, ids 4-7 =
up, left, right, down); it goes
through the 2D atlas like `ui_text` glyphs, and its texels are drawn from the
icon's signed distance field (ui_text.c's stb_truetype path, 128 pixels per
em) at the internal resolution: text in the original's ink box (one pixel
larger, inside its 12 x 12 cell), sprites inside a circle
`button_prompts_scale` of the cell across centred on the original's ink (so
neighbours never touch), the D-pad in its arms' box. Text icons take the
original glyph's CLUT ramp (the text's colour, brightness and translucency);
sprites are the icon alone in white or the family's colour
(`button_prompts_style`), its anti-aliased edge darkening (texels are opaque
or not). The game still decides which button, where and when. At internal
resolution 1 the icons are drawn one texel per pixel (blocks of a 2x atlas).
A button bound to nothing, or to a key without an icon, keeps the original.
Not replaced: the D-pad arrows of the font (also used as plain arrows),
button names spelled in letters, art baked into images. `pc/tests/button_prompts_smoke.py`
covers it. The native (Dear ImGui) menus get the same icons as images in one
ImGui texture: `pc_prompt_glyph`, `pc_prompt_device`, `pc_prompt_generation`
in `pc/src/button_prompts.h`.

**Lines (`ui_lines joined`).** The PS1 draws a line one pixel per step of
its major axis, end pixels included (`pc/src/boot_lines.inc` is the
reference). Upscaled, a line is a band one PS1 pixel across its minor axis
through the pixel centres, so 1x gives exactly the CPU rasterizer's pixels
(halves round away from the start, as it does) and 4x is the same line 4
pixels thick; each end pixel is drawn whole, so the segments of a polyline,
or separate lines that share an end, meet without notches at any angle
(not for semi-transparent lines, which would blend twice there). The game
outlines filled shapes edge by edge (talk bubbles: one line per outline
edge over the fill polygons). A polygon's edge runs through the pixels'
corners, half a pixel from the line's centre: at 1x those pixels are the
line's, upscaled the fill showed beside the line as a sliver, light dots
along a bubble's dark outline (`legacy`). A joined line along an edge of a
2D polygon drawn earlier in the same ordering table therefore also reaches
that edge (up to half a pixel wider on that side); other lines keep their
width. `ui_outline_boost` widens every 2D line by whole output pixels.

**Vector talk bubbles (`ui_bubbles vector`).** A talk bubble's frame is
built each frame by BATTLE.PRG `func_800CD3E4` from one of nine shapes
(rectangle, rough, jagged, shout, oval ...; mirrored by two flip bits, one
of several tails): outline vertices placed in the box's rectangle, then a
drop shadow (the fill polygons at +2, +2 in black, each outlined), the fill
polygons with a screen-anchored 64 x 64 paper texture at the box's
brightness, and one dark line per outline edge; the text comes after. The
routine reports its packets (`ROOD_PC` hook, with the box's unrounded
rectangle from `func_800CB83C`); `pc/src/ui_bubbles.c` places the shape's
vertices exactly as the routine does, checks them against every line and
polygon packet (a bubble that does not match is drawn as the original;
RPC `ui_bubbles` counts them) and draws, in the packets' place in the
ordering table, the shadow, the fill (the game's polygons, texture page,
CLUT, window and brightness, nearest) and one closed anti-aliased stroke
along the outline through the game's vertices (the fill's edges run along
its centre: no seams, nothing light outside it; round at turns, one output
pixel of edge). The zoom, its overshoot, size changes, tails and mirroring
are the game's frame by frame; with `ui_subpixel` the vertices come from the
unrounded 16.16 rectangle (smooth zoom). The CPU rasterizer still draws the
packets, so native VRAM is the PS1's. `ROOD_UI_DEBUG=5` prints each bubble's
points, outline and polygons.

### Lighting the room's fixtures, light actors and Place Actors

Lanterns, torches and candles are usually room geometry (a bracket and
cage with a flame texture, a candle in a niche). **Light fixture** (toolbar;
**L** lights the one under the cursor; a glow's Details has *Light this
fixture*) gives one a light in one click: the flame is the nearest game glow
within 128 units (linked), else the brightest, warmest texels of the room
polygons within 80 units of the click (`ptgi.c` `pc_gi_flame_near`). The
actor's prefab comes from the glow's colour and size or the flame's strength
(the prefabs' `glow.suggest` rules: a candle, lantern, torch or magic glow),
and its first light sits on the flame. The fixture is recorded with the
actor (`fixture=x,y,z fixture_radius=r`): its polygons near the flame cast
no shadow from that light (shadow rays stop `fixture_radius` short of the
light, UE's "ignore owner"; Details > *Fixture casts shadow* turns that off,
e.g. for an open cage whose bars should shadow the wall). Selecting the
actor outlines the fixture's polygons; the Outliner marks it `[fixture]`.
**Light fixture > Light all fixtures in room** (or Glows > *Light all
fixtures in room*) lists an actor for every unlit glow (ticked) and every
small flame polygon (unticked: less certain), drawn as ghosts, to review
and add. Flames painted into larger textures are only found by clicking
them (a room-wide brightness test caught patterned sandstone).

Light actors are prefab instances (`pc/src/light_prefabs.h`, editor in
`light_actors.cpp`): a prefab is a JSON file of lights placed relative to a
pivot plus exposed parameters (colour temperature, tint, intensity, radius,
flame height, flicker, sway, shadows, light size, the linked glow's
treatment) that the lights' fields use in small expressions. Built-in
prefabs (Candle, Candelabra, Lantern, Wall torch, Brazier, Fireplace, Window
light, Magic glow) are in `pc/assets/prefabs` (format:
`pc/assets/prefabs/README.md`); yours go to `pc/user/prefabs` (`ROOD_PREFABS`)
and override a built-in with the same file name. Both reload live. An
instance stores its prefab, transform, seed and only the parameters it
overrides (`actor` lines in the room file, beside the old `light` lines,
which still load), so editing a prefab changes every instance; Details
shows the parameters with overridden ones highlighted and a reset arrow,
*Apply to prefab*, *Save as prefab...*, *Edit prefab* (a live editor of the
JSON), *Break into lights*. *Create prefab...* on a light (Ctrl+click more
lights in the Outliner) makes a prefab of them. Flicker and pulse are
deterministic functions of time and the actor's seed, sampled once per
presented frame, and reach every lighting path (present shader, GL PTGI,
Vulkan RT). A glow linked to an actor is driven by its flicker (or
suppressed). Vulkan with ray tracing takes every light (a storage buffer;
the shadow rays go to lights picked per pixel, so a hall of 67 candles costs
about what 16 lights did: [pc/docs/Vulkan_Renderer.md](docs/Vulkan_Renderer.md),
*Many lights*). The other paths have a budget: OpenGL's present 64 lights
(16 with its path-traced shadows), shadow maps for the 4 most important
casters. Past a budget the most important lights are kept (brightness x
reach / distance from the camera, in view first, with hysteresis so the
choice does not flicker), and the Outliner says how many were left out.
RPC `actors` reports `lights_wanted` and `dropped`; `actors {budget: N}`
caps the Vulkan list (comparisons; 0: every light).

**Place Actors** (the tab beside the Outliner, Window > Place Actors,
Shift+1, or the toolbar's **+ Place** menu) lists the light types and the
prefabs with icons and colour swatches, a search field and categories. Drag
one into the view: a lit ghost follows the cursor, placed by the snapping
rules (point / edge snap with the neighbour highlight, else the surface: a
light off it by the snap offset, an actor's pivot on it, facing the normal
for wall prefabs); release to place it (selected, gizmo ready); Esc or a
drop off the view cancels. Double-click places it at the surface under the
view's centre; Ctrl+click in the view adds the panel's current item.

Lighting and atmosphere settings are per room (`pc/src/room_settings.cpp`):
each has a project default (built in, a pack's `lighting/<pack>/defaults.txt`,
or yours in `pc/user/lighting/defaults.txt`), and changing it overrides it for
the current room (highlighted, with a reset button; right-click to make a
value the project default). A room's lights, actors and overrides come from
its lighting layers (next section); Save area writes your user layer
`pc/user/lighting/zoneZZ_roomRR.txt`. Settings not tied to a room (graphics,
post effects, path tracer quality, shared lighting, `lighting_packs`) are
saved from the editor (File > Save settings, Ctrl+Shift+S) to the file named
by `ROOD_SETTINGS` (`pc/run-game.sh` uses `pc/user/settings.cfg`; tests leave
it unset) and loaded at startup; a save writes `<file>.tmp` and puts it in
place, so the file is never half-written. Environment variables that
override a saved key for one run (`ROOD_WIDESCREEN`, `ROOD_PERSPECTIVE`,
`ROOD_SCALE`, `ROOD_AA`, `ROOD_LIGHTING_PRESET`, `ROOD_GI=0`,
`ROOD_PRESENT_RATE`, `ROOD_NATIVE_*`, `ROOD_RECONSTRUCT`) are not saved as
yours: a save keeps that key as the file had it, unless you changed the
setting since (`ROOD_GI=0`'s keys: unless changed in the System tab). Both
kinds of file are data: edits made
outside the game (by hand, by a script, by `git pull`) take effect live
within half a second. `pc/user` is user content and git-ignored. The RPC
`graphics` method sets the renderer options and `gpu_dump` writes the scaled
target.

### Lighting packs: layers, ids, Promote

Lighting is data under source control, in layers merged by stable ids
(`pc/src/lighting_layers.cpp`; format, ids and workflow:
[lighting/README.md](../lighting/README.md)):

1. `lighting/<pack>/generated/zoneZZ_roomRR.txt`: generator output
   (tracked, regenerated wholesale, never edited by hand; generated items
   take deterministic ids from their source);
2. `lighting/<pack>/rooms/zoneZZ_roomRR.txt`: hand-authored lighting and
   overrides of generated items by id (`override <id> key=value`,
   `delete <id>`), tracked, what pull requests change;
3. `pc/user/lighting/zoneZZ_roomRR.txt`: your own changes (untracked).

Packs stack in the order of the settings key `lighting_packs` (default
`default`; `lighting_active_pack` picks the one Promote writes, else the
highest). Files are one item per line, sorted by id, numbers at fixed
precision, comments allowed, with a `rood-lighting 2` header. The old
`pc/lights/zoneZZ_roomRR.txt` files still load (as the user layer, when
`pc/user/lighting` has no file for the room) and are migrated when saved.

- **Save area** (Ctrl+S) writes only what differs from the packs: new items,
  `override` lines with the changed fields of a pack's lights and actors,
  `delete` for removed ones.
- The Outliner shows each light's and actor's layers as a badge (`[G]`
  generated, `[P]` pack, `[U]` yours, `[G>P>U]` changed on the way up).
  Right-click a row: **Revert to the layers below** (an undoable edit),
  **Promote to pack**, Copy id. Outliner > Layers lists the room's files,
  items you deleted (Restore) and problems: an override or delete whose id
  no lower layer defines (it was deleted or regenerated under it) is
  reported, kept in its file and never silently dropped.
- **File > Promote room to pack** / **Promote selection to pack**: saves,
  then moves your user-layer entries for the room (or the selection) into
  the active pack's authored file, keeping its other lines and comments.
- Every layer reloads live. When a pack file changes under unsaved edits (a
  pull, a regeneration), the edits are kept on top of the new packs; when
  your own user file changes under them, the editor says so (Reload area
  takes the file).

`ROOD_LIGHTS` names the user layer's directory (tests: a scratch one; then
no packs load unless `ROOD_LIGHTING` names a packs directory, so tests stay
isolated); `ROOD_LIGHTING` names the packs' directory (`none`: no packs).
RPC `lighting` reports the layers, the effective items with badges, problems
and the user layer as it would be saved, and saves, reverts, promotes and
switches packs. `pc/tools/lighting_lint.py` checks the packs (run.py runs it).

### Room Browser: go to any room

**Window > Room Browser** (toolbar **Rooms**, Ctrl+R) lists every zone and
room of the game, with the game's own room and area names, searchable, the
current room highlighted, Previous/Next and Go (or double-click). Nothing is
shipped: the list is read from your disc at run time (`pc/src/room_catalog.c`,
a few milliseconds): the executable's location table, the zone files' room
lists, each room's door table (where doors lead and where Ashley arrives)
and the names the map screen uses (`MENU5.PRG`, `SCENnnn.ARM`).

Go makes the game itself change rooms: the field loop takes the request
where it checks doors (`src/BATTLE/BATTLE.PRG/146C.c`, a hook under
`ROOD_PC`; game side `pc/src/room_warp.c`) and starts the game's own door
transition (`func_8007C4E0` within a zone, `func_8007C5C0` across zones,
which loads the other zone file first) with a door record taken from a real
door into the target room, so Ashley arrives where that door puts her, from
any room. The editor stays open; the free camera and the pause come back
after the fade-in. While the room loads (a second hook in `func_80089DC0`)
the warp leaves its enemies out and its scripts unrun by default (toggles:
Enemies, Room scripts): no cutscene, no story flag set by entering; the map
screen's "rooms seen" bits are put back afterwards. The rooms' own temporary
flags reset as on any room or zone change. Rooms you then walk into through
doors behave as in play (their events may run). Rooms only reached by an event or
a fall (no door leads in; 55 of 419) are listed greyed and cannot be warped
to.

Saving after a warp would record a location the story never reached:
**while warped the memory card refuses writes** (Refuse saves, on by
default: opening a card file for writing, renaming and erasing fail in
`pc/src/memory_card.c`, so the save cannot complete; how the game's save
screen words that has not been checked by hand). **Return** goes back to
the room the warps started from (through one of its doors) and ends the
warped state; **Stay (allow saving)** accepts the current room. RPC `rooms`
lists the rooms (and the warp's state), `room_goto {zone, room}` warps
(`{action: "return" | "clear" | "cancel" | "status"}`, `enemies`, `scripts`,
`block_saves`); `pc/tests/room_browser_smoke.py` covers both with the
lighting layers.

### Fixture catalog and generated lighting

Fixtures are lit per **type**, not per instance
([pc/docs/Fixture_Catalog.md](docs/Fixture_Catalog.md)). The catalog
`pc/assets/fixtures/catalog.json` (tracked, hashes and numbers only) lists
every fixture model of the game's rooms. That is 239 types: candles,
torches, lanterns, braziers, the save point and windows, but also cobwebs,
floor arrows, glow decals and fake light shafts. Each type has a match rule
(emitter texture content hashes, polygon count, size range, holder
signature) and its treatment: a prefab with parameters, a light offset from
the flame, a fixture radius (no self-shadow), and its game glow (drive,
suppress or leave). Inferred fill lights (the save point, windows) are off
by default. `pc/tools/fixture_scan.py` builds and checks the catalog from
your disc. `pc/tools/generate_lighting.py --zone N` writes the generated
layer: one actor per flame (`actor:gen.<type>.<hash>`, stable ids, naming
`fixture_type`) and a typed entry for each game glow of a catalogued
fixture. Cobwebs', shafts' and decals' glows are then suppressed by their
type. Every zone is generated (338 rooms, 1152 actors);
`pc/tools/lighting_gallery.py --all --out DIR` captures the whole game
before/after as JPEG pages (local only) for review.

The game reads the type's parameters, offset, radius and glow treatment
from the catalog, live (`pc/src/fixture_catalog.cpp`). Details >
**Edit type T00x** changes every instance at once. **Save catalog** writes
the file, and **Regenerate room/zone** reruns the generator. Per-instance
tweaks are ordinary overrides by id in the authored or user layer.
`pc/tools/lighting_gallery.py` (or `generate_lighting.py --gallery DIR`)
captures a zone's rooms before and after, with close-ups and per-type
contact sheets. These are game images: keep them local. Zone 9 is
generated and committed so far.

### Sun and sky (outdoor rooms)

Rooms open to the sky (the town's streets, Snowfly Forest, the Keep's
courtyard, the cathedral's roofs) have a **sun** (here mostly a moon) and a
**sky**: `env sun` and `env sky` items of the room's lighting layers
(`pc/src/environment.cpp`; format: [lighting/README.md](../lighting/README.md),
"Environment"). The sun is a directional light (azimuth, elevation, colour,
intensity, angular size for soft shadows, casts shadows); the sky is the
radiance a ray brings back when it leaves the room (a zenith -> horizon ->
ground gradient, optionally tinted around the sun). Roofed rooms have
neither, so they look exactly as before.

- **Ray tracing** (Vulkan ray queries, OpenGL compute; High/Ultra): the sun's
  shadows are traced with its own rays every pixel (it never competes with
  the room's lights in their sampling), and it lights every bounce of the
  path tracer and every probe; GI rays that escape bring the sky, so sky
  occlusion under arches and trees falls out of the trace (NRD keeps its
  energy, the probes carry it).
- **No traced shadows** (Low/Medium, Vulkan without ray queries): the sun gets
  a shadow map (an orthographic 1024 x 1024 map of the room and characters,
  `shadows.c` / `vk_shadows.c`, 8 rotated taps widened by its size); without
  GI the sky becomes a subtle hemisphere ambient term (upward faces see more
  of it; the screen-space AO darkens it).

`pc/tools/generate_lighting.py` writes them into the generated layer for
every room `pc/tools/outdoor.py` finds open to the sky (82 rooms): its floor's
upward rays escape (>= 40 %), it has little modelled ceiling (< 0.3 x the
floor) and its floor has baked vertex light, plus the decisions in
`pc/assets/environment/outdoor.json` (the Undercity's unmodelled vaults
are roofed). Defaults are moody: a low, cool, soft moon (intensity 0.7,
35 degrees up, 2 degrees wide) and a dim overcast night sky, both leaning
toward the hue of the room's own vertex light. Editor: Details >
**Atmosphere** > "Sun and sky": every value live, Save area (Ctrl+S) writes
the user layer (`override env:sun elevation=...`), and a sun gizmo in the
view (drag the disc: horizontal turns it, vertical raises it). Settings key
`environment` (0) switches every room's sun and sky off for comparison. RPC
`environment`. Test: `pc/tests/sky_smoke.py`. Details and costs:
[pc/docs/Vulkan_Renderer.md](docs/Vulkan_Renderer.md), "Sun and sky".

### Haze, ground fog, fog volumes and sun shafts

The room's air is one **medium** (`pc/src/atmosphere.cpp`,
`pc/shaders/vulkan/medium.glsl`): the uniform **haze** (room settings
`haze_density`, `haze_anisotropy`, `haze_glow`), a **ground fog** (`env fog`:
thickest at its base on the floor under each point (the room's lowest
up-facing level face there, a top-down grid: the streets, not the roofs),
thinning upward with a scale height, animated by drifting noise wisps
carried by a wind) and **fog
volumes** (`fog <id>` items: boxes or spheres with soft edges, their own
density, colour, fill light and wisps) for local mist. Format:
[lighting/README.md](../lighting/README.md), "Environment" and "Fog volumes".

- **Lit by every light and the sun** with a Henyey-Greenstein phase
  (`haze_anisotropy`: forward scattering). High (traced) haze: each cell of
  the haze's froxel volume (Vulkan) or each march step (OpenGL) traces one
  shadow ray to a picked light, one to the sun's disc and one to the sky, so
  moonlight **streams through arches, branches and gaps** (shafts), the air
  under a canopy or arch loses the sky, characters cast volumetric shadows.
  Low haze (Medium; no ray tracing): the same medium marched per pixel in the
  present pass, unshadowed lights, the sun through its shadow map.
- **Soft, slow, wispy**: three octaves of domain-warped gradient noise (no
  blotches, no lattice), drifting with the wind in world units per game
  second (slow by default: mist creeps; it stops with the game); each
  froxel holds the medium's average over its cell, so thin layers and
  volume edges never show slices or steps.
- **Stable**: blue-noise jitter per froxel stratified over frames, a
  reprojected temporal blend, a low-resolution volume (`haze_cell` display
  pixels per cell: Ultra 2, High 3) upsampled by depth (each pixel reads the
  volume at its own depth, so PS1 texels stay sharp: nothing is blurred
  across the image).
- **Never hides the scene**: whatever the densities, along any ray the
  atmosphere hides at most `haze_max_opacity` (0.55) of the scene, adds at
  most `haze_max_light` (0.15, soft knee) of light, forward scattering
  toward the moon peaks at `haze_max_forward` (3) and the sun shafts keep a
  limited contrast (`haze_shafts` 0.5); room settings, so a mood may change
  them. `pc/tools/atmosphere_sweep.py` warps to every room with a haze or a
  ground fog and checks, from the game camera and the free camera at 8
  angles and toward the moon, that the picture keeps its detail (Weber
  contrast with vs without the atmosphere) and is never veiled; a quick
  subset is the `atmosphere_sweep` suite.
- **Generated moods** (`pc/assets/environment/atmosphere.json`, written by
  `generate_lighting.py`): Snowfly Forest low ground mist, the Undercity damp
  haze and floor mist, the town, walls and Keep at night a faint moonlit
  haze, a thin mist on the streets and faint shafts, any other open room a
  faint haze; roofed rooms keep
  the project's faint default haze (unchanged).
- **Editor**: Details > **Atmosphere** > "Ground fog" (every value live) and
  "Fog volumes" (Add volume on the surface under the view's centre; Move /
  Size / Turn gizmo in the view; Show the haze alone). Save area writes
  `env fog` / `fog <id>` lines. Settings key `fog` (0) switches every room's
  ground fog and volumes off. RPC `environment` {`haze`, `fog`, `volumes`,
  `remove_fog`, `fog_time`, `fog_global`, `floor_grid` (0: the main floor
  alone, as before; comparisons)}, `graphics` {`haze_debug`,
  `haze_cell`}. Test: `pc/tests/atmosphere_smoke.py`. Costs:
  [pc/docs/Vulkan_Renderer.md](docs/Vulkan_Renderer.md), "Haze, ground fog
  and sun shafts".

### Lighting quality presets

Lighting has quality presets so lower-spec PCs run it (`pc/src/lighting_quality.cpp`):
the toolbar's **Lighting** dropdown picks one, and Details > Graphics >
**Lighting quality** shows every setting by group (like Unreal's scalability
settings). A preset sets its keys; changing any of them afterwards (editor,
RPC, settings file) makes the preset **Custom**. Per-room values (light
intensity, light size, haze density: the Lighting and Atmosphere tabs) still
apply on top.

| Preset | Shadows | Indirect light | Haze | AO | For |
| --- | --- | --- | --- | --- | --- |
| Original | none (the PS1's baked light only; dynamic lights off) | baked vertex colours | off | off | any |
| Low | shadow maps, soft PCF | baked | off | on | integrated GPUs, no ray tracing |
| Medium | shadow maps, contact-hardening PCSS | probe GI (else off) | low (unshadowed) | on | mid-range GPUs, no ray tracing needed |
| High | ray-traced soft shadows, glows/candles cast shadows, characters shadow the baked light | probe GI (else off) | high (volumetric shadows) | on (traced) | entry ray tracing |
| Ultra | ray-traced soft shadows (2 rays), glow and character shadows | per-pixel path tracing, NVIDIA NRD | high | off (the path tracer) | discrete ray-tracing GPUs |

Each preset value is a list of choices in order of preference, and applying a
preset takes the first one this backend can run: on Vulkan without ray
queries (or `ROOD_VK_RT=0`) and on OpenGL without 4.3 compute shaders,
ray-traced shadows become shadow maps (PCSS), high haze becomes low, path
tracing becomes off; OpenGL with compute runs the GL path tracer (NRD is
Vulkan only: the filter there). What runs is decided again every frame, so a
Custom "traced" on a machine without a tracer draws shadow maps, and the
editor greys out what cannot run with the reason for this backend. GI
resolution is capped at 1.5 on integrated GPUs. Probe GI (`gi_method
probes`) comes from the probe GI work; until a build has it (a
`pc_probe_gi_available()` that returns 1), Medium and High fall back to the
next lower choice (off) and the editor lists "Probes (not available)".
Screen-space GI is reserved (not implemented).

What the tiers add: **AO** darkens the baked and indirect light in creases
and under props: traced with two short rays per GI pixel (accumulated with the
baked-light visibility) where Vulkan ray queries exist, else screen-space AO
from the depth buffer in the present pass (12 samples, no extra pass).
**Shadow map filters**: hard (one tap), PCF (8 taps, `shadow_map_softness`)
and PCSS (a blocker search over the light's size, then a penumbra as wide as
the blockers' distance gives: sharp at contact, softer further away; slope
scaled biases), the same on GL cube maps and Vulkan's atlas
(`vk_shadows.c`). **Haze** Low is unshadowed in-scattering of every light
and the sun (through its shadow map) in the present pass (8 jittered steps
through the room's medium: haze, ground fog, fog volumes; the traced haze's
phase function and densities); High is the traced haze (Vulkan froxel
volume, GL per-pixel march) with the lights', the sun's and the sky's
shadows. Haze off also switches the ground fog and the volumes off. **Original** is the PS1's look exactly: no
dynamic lights, the baked light at its own scale (the room's `baked_lighting`
only applies with dynamic lights), the game's drop shadows.

First run: with no settings file yet, the preset is detected (a settings file
from before presets, without `lighting_preset`, loads as Custom so its tuned
lighting keys are kept): Vulkan ray queries -> Ultra on a discrete GPU,
High on an integrated one; no ray queries (OpenGL, or Vulkan without them)
-> Medium on a discrete GPU, Low on an integrated (or software) one.
`ROOD_GPU_CLASS=integrated|discrete` overrides the GPU class,
`ROOD_LIGHTING_PRESET=name|auto` the preset. Detection is not saved by
itself (it repeats until the settings are saved); a short benchmark could
refine it later. Without `ROOD_SETTINGS` (tests) the built-in values stay and
the preset shown is whichever matches them.

Keys (settings file; RPC `graphics` reads and writes the same names, with
`lighting_preset` applied first):

| Key | Values | Preset key |
| --- | --- | --- |
| `lighting_preset` | `original`, `low`, `medium`, `high`, `ultra`, `custom` | |
| `dynamic_lights` | 0/1 | yes |
| `gi_method` | `off`, `screen` (reserved), `probes`, `path` | yes |
| `gi_resolution` | GI pixels per native pixel (0.25-4) | yes |
| `bounces`, `rays_per_pixel` | path tracer | yes |
| `gi_denoiser` | `raw`, `filter`, `nrd` (numbers 0-2 still load) | yes |
| `shadows` | `off`, `maps`, `traced` | yes |
| `shadow_map_filter` | `hard`, `pcf`, `pcss` | yes |
| `shadow_map_softness` | PCF cone (and PCSS's least penumbra) | yes |
| `shadow_rays` | traced shadow rays per light per pixel | yes |
| `glow_shadows` | glows (candles, torches) shadow their light | yes |
| `characters_shadow_baked` | characters' contact and glow shadows on the baked light | yes |
| `haze` | `off`, `low`, `high` | yes |
| `haze_cell` | the traced haze's froxel size in display pixels (Vulkan; 2-8: smaller gives sharper shafts) | yes |
| `ao`, `ao_radius` | ambient occlusion and its reach (world units) | yes |
| `ao_strength` | 0-1 | no |

The older keys `path_traced_light` and `traced_shadows`, and RPC `gi` /
`rt_shadows`, still work (they set `gi_method` and `shadows`). The section's
Display part (anti-aliasing, frame pacing) is not part of the presets. Measured GPU
time per preset (save room, a test light, RTX PRO 6000, RPC `perf`):
`pc/tests/lighting_presets_smoke.py` prints it for each backend.

A GPU too slow for the path tracer (OpenGL): at most two presented frames
are queued on the GPU (`gl_present.c`; `ROOD_FRAMES_IN_FLIGHT` 1-8), so the
CPU waits for the GPU instead of queueing frames without bound (that grew
to seconds of latency and, ~20 s deep, the NVIDIA driver dropped the work:
a black frame). When the tracer alone takes longer than its budget (RPC
`graphics` `gi_budget`, `ROOD_GI_BUDGET`; auto 16 ms, 50 ms in fast mode,
0 traces every present) it runs on every n-th present and the presents
between show its last result. `ROOD_GI_SLOW=n` (testing) slows the tracer
n-fold; `pc/tests/gi_lag_smoke.py` covers both.

## Developer control interface (JSON-RPC)

Set `ROOD_RPC=/path/to/socket` and the game listens on that Unix socket (mode
0600) for JSON-RPC 2.0, one object per line. Requests are serviced on the game
thread once per vsync, so each method runs at a frame boundary:

| Method | Params | Result |
| --- | --- | --- |
| `status` | | vsync and battle-frame counters, pause state, injected buttons, `display_masked` (SetDispMask(0)) |
| `pause` | `paused` (default true) | stop or resume at vsync boundaries |
| `step` | `vsyncs` | run exactly N vsyncs, then pause |
| `input` | `buttons` (name or list), `right_stick` (`[x, y]`, -1..1, down +y), `vsyncs` | hold PS1 buttons and/or the right stick (the camera); works unfocused |
| `camera_stick` | the settings without `camera_stick_` (`enabled`, `speed`, ..., `r3_reset`, `idle_reset`, `keys`), `action` (`reset`) | the right-stick camera: settings and state (zoom, distances, collision hit, yaw) |
| `weapon_slots` | `enabled` | the optional weapon slots (battle builds): the option, overlay/assign/swap state, field mode, each weapon's slot and each shield's slot bits, what Ashley's battle record has equipped |
| `ui_menu` | `modern`, `pointer` | the modern menus (`pc/src/ui`, battle builds): sets `ui_menus`; `model_heap_extra` (bytes, tests: the Ashley model's heap check asks for that much more); `pointer {x, y, click, right, wheel}` is the next drawn frame's mouse in layout units (1280 or 960 x 720; hit tests as a moved pointer); the shell: visible, holding (closed, a result held during a model load, still drawn), menus, tab, tabs, rows (id, label, enabled, reason when greyed, value), focused row, the Equipment screen |
| `release` | | drop injected buttons |
| `state` | | zone, room, start state, music, HP/MP, risk, Ashley's position |
| `read` / `write` | `address`, `length` / `hex` | guest RAM and scratchpad (read also host data) |
| `screenshot` | `path` | displayed frame (native resolution) as a PPM |
| `present_dump` | `path`, `count` | the next presented window image (as seen, after scaling and post-processing) as a PPM; with `count`, that many consecutive images to `<path>.0000.ppm`, ... (written a few presents later on the game thread). While a burst is armed, a paused game does not redraw, so a burst holds stepped presents only |
| `gpu_dump` | `path` | the whole scaled hardware VRAM target as a PPM |
| `gpu_draw` | `packet` (GP0 words, hex), `texture` (hex texels) with `texture_x/y/w/h`, `area_x/y/w/h`, `offset_x/y` | tests: load texels, set the drawing area, draw the commands on both rasterizers (`flipped_sprite_smoke.py`) |
| `graphics` | `scale`, `filter`, `aspect`, `widescreen` (with `widescreen_cover`, `widescreen_layout`, `widescreen_scene`, `widescreen_trace` N), `perspective`, `show_depth`, `native_room`, `gameplay_walls` (`original`/`smart`), `gameplay_walls_ghost`, `ui_upscale`, `ui_subpixel`, `ui_text` (`original`/`font`), `ui_text_layout` (`line`/`glyph`), `ui_lines` (`joined`/`legacy`), `ui_outline_boost`, `ui_bubbles` (`original`/`vector`), `ui_bubble_outline`, `lighting_preset` and the lighting quality keys, ..., `aa` (`off`/`fxaa`/`smaa`/`msaa2`/`msaa4`/`msaa8`/`ssaa`[factor], joined with `+`), `aa_downsample` | get/set renderer options; the reply adds `lighting_caps` (backend, ray tracing, GPU class), `lighting_autodetect` and `lighting_unavailable` (why a choice cannot run here) |
| `ui_upscale` | `verify`, `reset`, `redraw` (path), `mask` (path), `poke` (`[x, y, w, h, value]`), `regions` (N) | the 2D magnification cache: backend, factor, hits/misses, CPU ms per frame, display area; `verify` checks the atlas against the CPU MMPX (`mismatches`, `foreign`: texels not in their source); `redraw` draws the newest frame again as recorded with the current options and dumps the scaled target (compare options on the same frame), `mask` writes where magnified 2D was drawn since the last mask (1024 x 512 PGM), `poke` rewrites a VRAM rectangle as LoadImage would, `regions` lists the cached regions drawn lately (tests) |
| `button_prompts` | `button_prompts` (mode), `device` (`playstation`/`xbox`/`nintendo`/`keyboard`, as a device used last would set it) with `variant` (3/4/5 PlayStation, 360 Xbox), `bind` (`{"bind_key_circle": "c", "bind_pad_circle": "south"}`), `icon` (a button name) with `size`, `clear_shown`, `style`, `scale`, `dpad` (`cross`/`dim`/`lit`), `cross_actions` (0-15: the shortcut cross's usable arms, bits up, left, right, down, whatever the game reports; -1 the game's), `mask` | the mode, the family drawn, the device, `generation`, the glyph each PS1 button shows, the bindings, `shown` (the icon each prompt on screen was drawn with since `clear_shown`, `text` and `sprite`, the shortcut cross's `dpad` and its lit `dpad_arms`), `style`, `scale`, `dpad_mode`, `cross_actions`, regions drawn from icons (`regions`) and fallen back (`failed`); `icon` adds the native menus' image (`pc_prompt_glyph`: size, uv) |
| `ui_bubbles` | `path` (N) | vector talk bubbles: frames reported, verified, fallen back (`mismatched`), drawn; line segments the hardware path drew (`lines_drawn`); native pixels per output pixel; the recent bubbles (shape, flip, tail, outline bounds drawn and the game's whole-pixel points, thickness); `path`: bubble N's outline points |
| `ui_text` | `ui_text` (`original`/`font`), `layout` (`line`/`glyph`), `lines` (path: the newest frame's laid-out lines as JSON), `style` with `font`, `stretch`, `slant`, `embolden`, `aspect`; `render` ([tpage, clut, u0, v0, u1, v1]) with `factor`, `path` | the text fonts: option, each style (font, placement, `loaded`), fonts loaded, glyph fields made, glyph regions drawn from fonts (`regions`, of them `labels`) and fallen back (`failed`), the labels drawn (`labels_drawn`); a `style` with values changes it as the `ui_text_<style>_*` settings do; `render` draws that region from the font into `path` (little-endian PS1 colours) and adds its size |
| `redraw` | | the newest frame drawn again as recorded with the current options (MSAA, ...) and presented until the game draws another: pause first, then compare options on one frame (`aa_smoke.py`) |
| `settings` | `action` (`save`, `load`) | the `ROOD_SETTINGS` file (a loaded preset applies at once) |
| `perf` | `reset` | GPU time per presented frame since the last reset (`ROOD_PERF=1`; Vulkan timestamps, GL timer queries; Vulkan adds `sections`: ms per frame of `draw`, `present`, `aa`, `downsample`, the GI passes); GL adds `presents`, `gpu_wait_ms` (CPU waits for frames in flight), `gi_traced`/`gi_reused`, `gi_gpu_ms`, `gi_budget_ms`, `gi_interval` (path tracer cadence) |
| `lighting_autodetect` | `ray_query`, `integrated` | the first-run preset for those capabilities |
| `render_stats` | | native lists/instances/triangles and room triangles drawn from packets in the last frame |
| `editor_ui` | `visible`, `help`, `tab`, `details`, `lighting_menu`, `rooms` | open or close the editor and its Keyboard shortcuts window (`rooms`: the Room Browser); `tab` `outliner` or `place`; `details` a Details tab (`lighting`, `inspector`, `atmosphere`, `post`, `graphics`, `diagnostics`); `lighting_menu` opens the toolbar's quality dropdown |
| `editor_overlay` | `points`, `edges`, `update` | overlay state (points and edges shown, hidden-line test, cost) and N shown points/edges with window pixels |
| `editor_snap` | `x`, `y` (or `u`, `v`), `modes`, `apply`, `highlight`, `clear` | the snap target a light drag would find there, with the lit neighbours and their weights; `apply` moves the selected light |
| `actors` | `action` (`list`, `add`, `set`, `remove`, `select`, `sample`, `suggest`, `light_fixture`, `attach`, `fixture_triangles`, `save`, `save_prefab`, `apply_prefab`, `from_lights`), `prefab`, `position` or `x`/`y` (`u`/`v`), `modes`, `params`, `reset`, `index`, `time`, `animate`, `glows` | light actors in the room (see `pc/src/dev_rpc.c`); `time` fixes the animation clock (null: live) |
| `glows` | `action` (`list`, `select`, `set`, `reset`, `solo`), `id`, `enabled`, `intensity`, `tint`, `size`, `reach`, `smoothing`, `fragments` | the room's glows (`pc/src/glows.c`): id, key, name, category (fixture, magic, effect), casts_shadows, smoothed and raw position/colour, presence, traced; the tracers' light `energy` (and `raw_energy` unsmoothed), per category (`categories`), `casts` (bit i: traced emitter i may cast shadows); select/set/reset/solo take fixtures and magic glows only; `smoothing` sets every kind's; `fragments: true` adds this update's glowing triangles and their glows |
| `lights` | `action` (`suggest`, `save`, `reload`, `add` {`position`}), `select`, `baked` | the room's lights (zone, room, `dirty`, `loads`: layer loads so far, status); `reload` reads every lighting layer of the current room from disk at once (unsaved edits are dropped) instead of waiting for the editor's file poll (twice a second of wall-clock time, which fast stepping outpaces): scripts write a room file, call `reload`, then count frames. The poll keeps the files' times as read, so it does not load them again, and the renderers' history resets (path tracer, NRD, probes) follow a hash of the lights, so a changed light resets them once |
| `prefabs` | `reload` | the prefab library (built-in and user), parameters and problems |
| `editor_place` | `item` / `prefab` / `light`, `phase` (`begin`, `move`, `drop`, `cancel`, `double_click`, `state`), `x`/`y` or `u`/`v` | a drag from Place Actors, as the mouse would do it |
| `environment` | `sun` {`enabled`, `azimuth`, `elevation`, `color`, `intensity`, `angle`, `shadows`}, `sky` {`enabled`, `zenith`, `horizon`, `ground`, `intensity`, `sun_tint`, `ambient`}, `remove` (`sun`, `sky`, `all`), `global` | the room's sun and sky (`defined`: the room has the item; setting a field of one it lacks adds it), `active`, `global` (settings key `environment`), the renderers' packed record (`gpu`) and its `hash`; an edit marks the room dirty (save with `lighting` `save`) and is one undo transaction |
| `environment` (fog) | `haze` {`density`, `anisotropy`, `glow`, `max_opacity`, `max_light`, `shafts`, `max_forward`} (room settings), `floor_grid`, `fog` {`enabled`, `density`, `height`, `offset`, `noise`, `scale`, `wind`, `color`, `fill`, `sky`}, `remove_fog`, `volumes` [{`id`, `remove`, `enabled`, `shape`, `name`, `pos`, `size`, `yaw`, `density`, `edge`, `color`, `fill`, `noise`}], `fog_time` (seconds, -1 live), `fog_global` | the same reply plus `haze`, `fog` (with the room's main `floor` and its `floor_grid` {`n`, `corner`, `cell`, `heights`}), `bounds`, `volumes`, `fog_global`, `fog_time` |
| `lighting` | `action` (`layers`, `save`, `revert` {`id`}, `promote` {`ids`}, `reload` (as `lights` `reload`), `packs` {`packs`, `active`}, `discard_dangling`, `poll` (the live reload's check, now)) | the room's lighting layers (files, entries, version), the effective items with their layer badges, deleted ids, problems (dangling overrides), the user layer as it would be saved |
| `fixtures` | `action` (`list`, `set` {`type`, `params`, `offset`, `fixture_radius`, `glow`, `enabled`, `review`}, `save`, `revert`, `regenerate` {`scope`}), `type`, `all` | the fixture catalog's types in the room (or one, or all), the room's typed actors and glows (`muted` 4: suppressed by the type); type edits apply live to every instance (`pc/docs/Fixture_Catalog.md`) |
| `rooms` | | every zone and room read from the disc (zone, room, location, scene, entries, name, area), the current room, whether warped, the warp's phase |
| `room_goto` | `zone`, `room`, `entry`, `enemies`, `scripts`, `block_saves`, or `action` (`return`, `clear`, `cancel`, `status`) | ask the game to load a room (taken in the field loop); phase 0 idle, 1 requested, 2 loading, 3 arrived, 4 done |
| `ending` | | developer jump: the field's next frame plays the ending (ENDING.PRG: credits, score and rank) and returns to the title, which offers the clear-data save; battle build |
| `editor_undo` | `action` (`list`, `undo`, `redo`, `begin`, `end`, `jump`), `steps`, `name`, `done` | the undo history (`done`, names, `undo`/`redo` names, `open`); each editing request (`actors`, `glows`, `lights`, `environment`, `editor_place`, `room_groups`, `uv_effects`, `editor_snap` with `apply`) is one transaction, `begin`/`end` hold one open across requests (a drag) |
| `editor_input` | `keys` (`delete`, `backspace`, `escape`, `enter`, `tab`, `f2`, a letter or digit), `text`, `ctrl`, `shift`, `focus` (`search`, `none`) | keys and text through SDL's event queue (the keyboard's path); replies `visible`, `want_text_input`, `item_active`, `gizmo` |
| `editor_view` | `focus` | the cutaway's focus (`[x, y, z]` fixes it, null: automatic), whether it is `hidden` and how far the cut has faded in (`amount`) |
| `quit` | | exit |

Button names: `up down left right cross circle square triangle l1 r1 l2 r2 l3 r3
start select`.

`pc/tools/rood_rpc.py` is the client: a Python API (`Game.launch()`, `run`,
`press`, `state`, `screenshot`, `read_symbol`), a CLI (`launch`, `call`,
`symbol`) and `--rpc`, a stdin/stdout JSON-RPC bridge that adds `symbol` and
`read_symbol` (resolved with `nm`, since the build is not position independent).
`Game.launch()` runs headless unless `window=True` (see "Running the tests"
for the EGL and Xvfb modes), and uses a scratch memory card unless `card=` is
given; `fast=True` drops the 59.94 Hz pacing, which RPC stepping does not
need. `run(n)` waits as long as the game keeps advancing (a loaded machine
path tracing on OpenGL can take seconds per vsync) and fails when no vsync
passes for `Game.STALL` seconds (a hang). `reload_lights()` is RPC `lights`
`reload`: tests that write room lighting files call it right after writing. `rood_rpc.py headless` prints the mode and GPU this machine gets.
`pc/tests/rpc_smoke.py` covers it.

Crashes print `[crash]` and a `[stack]` line; `kill -USR1 <pid>` prints the
stack of a running game. `pc/tools/where.sh resolve` turns those lines into
function names. `ROOD_SNAP_EVERY=N` saves every Nth displayed frame.

## Contributing lighting

Lighting contributions arrive as pull requests that change only
`lighting/` (format: [lighting/README.md](../lighting/README.md)):

1. Light rooms in the editor (`pc/run-game.sh`, Escape; Ctrl+R jumps to any
   room). Save area keeps your work in `pc/user/lighting` (untracked).
2. When a room is ready, **File > Promote room to pack** (or Promote
   selection) moves it into `lighting/default/rooms/zoneZZ_roomRR.txt`.
   Override generated items by id rather than copying them; never edit
   `generated/` files.
3. Run `python3 pc/tools/lighting_lint.py` (syntax, known keys and prefab
   parameters, duplicate ids, dangling overrides, rooms that exist; `--fix`
   canonicalizes number formatting) and commit only `lighting/`: no game
   data, screenshots of game imagery, `pc/user` or `pc/lights` files.
4. One room per file keeps conflicts rare; ids are unique per item, so two
   people adding lights to one room merge cleanly (entries are sorted by id).
   Describe in the pull request which rooms changed and with which lighting
   quality preset you judged them.

Prefabs used by contributed actors must be built-in (`pc/assets/prefabs`) or
contributed with them.

## Validation

Every successful boot build compiles and runs the software GTE probe: original
register defaults, identity transform, perspective projection, NCLIP, AVSZ3, and
axis/diagonal vector normalization. The battle build also checks the translated
zero-fill spans and all 65,536 combinations of the actor flag helper.
These focused cases do not establish full GTE fidelity.

### Running the tests

`pc/tests/run.py` runs every suite, several at once, and prints one table:

```sh
python3 pc/tests/run.py              # all suites, -j CPUs/4 (at most 6)
python3 pc/tests/run.py -j 2 input   # suites whose names contain "input"
python3 pc/tests/run.py --list
```

| Suite | Covers |
| --- | --- |
| `ctest` | the CMake probes in `pc/build` (configure it once, below) |
| `rpc_smoke`, `gi_smoke`, `movie_smoke` | RPC control, path tracer, intro movie |
| `room_smoke`, `opening_smoke`, `scene_smoke` | checkpoint frames without a window |
| `window_smoke` | real X11 key and close events (always on Xvfb) |
| `input:*` | each case in `start_button_smoke.sh` (`input_smoke.py`) |
| `editor_cutaway_smoke`, `editor_snap_smoke` | the free camera's view into the save room; the viewport overlays and snapping there (both need `pc/save/card1.mcd`) |
| `golden_frames` | fixed scenes and camera poses against reference frames |
| `camera_stick_smoke` | the right-stick camera in the save room: continuous yaw proportional to the stick, facing masks, L1/R1, zoom limits, no camera behind drawn walls, R3, idle reset, first-person view and targeting sphere, the RT camera (Vulkan); the opening's scripted camera unchanged (needs `pc/save/card1.mcd`) |
| `fixture_lighting_smoke` | generated fixture lights in zone 9: types, lit, shadow exclusion, glow links, suppressed glows, live type edits, catalog save, regenerate (needs `pc/save/card1.mcd`); the catalog's schema, round trip and generator determinism are the CTest `fixture_catalog` |
| `soak`, `soak:save` | `soak.py` for `--soak-frames` vsyncs (default 3000) from New Game, and from a copy of `pc/save/card1.mcd` (`--save`) when it exists; a Game Over is an event (Continue, or New Game from the title), not a finding |

Each suite gets its own process group, output directory
`pc/build/tests/<suite>/` (`output.log`, the game log, frames, scratch memory
cards: `ROOD_OUT`), RPC socket, X display and an empty lights directory
(`ROOD_LIGHTS`), so parallel suites cannot collide (no more "Cannot open X11
display") and your settings, lights and saves never leak in. The table lists
each suite's result, time and log; the exit status is nonzero when any suite
failed (`SKIP` does not count). The runner does not build the game: rebuild
with `pc/tools/build_boot.py --battle` (and without `--battle` for
`window_smoke`'s `rood_boot`) first; it warns about stale binaries.

**Headless rendering** (`ROOD_HEADLESS`, or `run.py --headless`):

- `egl` (the default when it works): SDL's offscreen video driver with an EGL
  context on a GPU device (`EGL_EXT_device_enumeration`/`EGL_EXT_platform_device`),
  no X server. SDL takes the first device that initialises (NVIDIA, then Mesa
  GPUs, then llvmpipe); `ROOD_EGL_DEVICE=N` picks one (`pc/tests/egl_probe.c N`
  prints each device's renderer). Tests run at the real 4x scale with path
  tracing as played. Input goes through the RPC `input` method: `input_smoke.py
  --input rpc` maps its X11 key names to pad buttons.
- `xvfb`: a private Xvfb display with software GL (llvmpipe), for machines
  without a usable EGL device and for X11 input (`send_key.c`, `send_mouse.c`;
  `input_smoke.py --input x11`). `rood_rpc.start_xvfb()` picks a display
  number that no concurrent run can take.

**Golden frames** (`pc/tests/golden_frames.py`): New Game with vsync-exact
RPC stepping reaches the opening card and the first room, which is captured
from the game's camera and from fixed `editor_camera` poses, with the depth
view, 1x nearest scaling and path tracing. Frames are compared with a
tolerance against references in `pc/build/golden/<gpu>/`. The references are
game imagery: they are generated locally and must never be committed.

```sh
python3 pc/tests/golden_frames.py --update   # after an intended visual change
python3 pc/tests/golden_frames.py            # compare; diffs in pc/build/tests/
```

The CMake probes on their own:

```sh
cmake -S pc -B pc/build -G Ninja
cmake --build pc/build
ctest --test-dir pc/build --output-on-failure
cmake -S pc -B pc/build32 -G Ninja -DCMAKE_C_FLAGS=-m32
cmake --build pc/build32
ctest --test-dir pc/build32 --output-on-failure
python3 pc/tests/window_smoke.py
```

For convenience, CMake targets `boot`, `check-boot` (build plus window test), and
`battle-probe` wrap the same commands. They are opt-in and require the disc assets.

The last test requires Xvfb and the X11 development library. It starts an
isolated display, reaches the real title menu, injects Up, verifies that Sound
(item 3) was selected and that Escape does not quit, then closes the window as a
window manager would and expects a clean exit. It does not touch your desktop.
Tests on Xvfb run SDL's X11 backend on their private display
(`SDL_VIDEO_DRIVER=x11`, even from a Wayland session). `pc/tests/send_key.c` sends X11 key events to the window by name, which
SDL reads as ordinary key presses; `pc/tests/send_mouse.c` moves the display's
pointer with XTest, since SDL reads the mouse through XInput2.

The independent memory probe checks 32/64-bit guest addressing, aliases,
scratchpad, rejected MMIO/out-of-range spans, unsigned char and packed OT links.
That small memory API is a portability experiment; the boot diagnostic currently
uses fixed mappings instead, as described below.

## Architecture and limits

- Game C uses a 32-bit ABI, original SDK structures, unsigned char, and original
  packed four-byte GPU tags. This proves a vertical slice, not a final ABI choice.
- Original main/overlay binary **data** is loaded privately into mapped RAM at
  `0x80000000`. Scratchpad is mapped at `0x1f800000`. These mappings are writable
  but non-executable: MIPS instructions are not run by the host.
- Ordering-table links are 24-bit guest addresses (every packet lives in guest
  RAM). The 32-bit build also decodes native links below 16 MB; the 64-bit build
  (`build_boot.py --arch 64`, `pc/build/boot64`) links only guest memory, keeps
  native code and data below 4 GB (non-PIE) and runs the game on a stack below
  2 GB, so any address the game stores in a 4-byte field fits. Native definitions override imports; unresolved original
  data symbols are mapped using reference ELF symbol tables. Full relocation of
  pointers stored in original data and coherent native/guest globals across
  overlays remain important unfinished work.
- The CPU renderer supports VRAM transfers/copies, CLUT and direct-color textures,
  sprites/tiles, flat/Gouraud textured triangles and quads, blend modes, texture
  windows, masks, and draw-area/offset packets. It is not a full PS1 GPU:
  polygon interpolation/dithering are not hardware-exact, and unsupported packet
  types still stop explicitly.
- PsyCross's MIT software GTE subset is vendored with attribution under
  `third_party/psycross/`. Its normal extended GPU structures are deliberately
  not used because this game's code writes packed packet words directly.
- File reads are synchronous against the user's image; streaming and XA audio
  run in time from the CD drive (see above). Debug font output
  and absent hardware have explicitly documented diagnostic behavior. These
  shortcuts are isolated behind `ROOD_PC_BOOT_DIAGNOSTIC` or the host SDK layer.
- Overlay C keeps its statics in host memory, but on the PS1 they live in the
  overlay image and restart whenever the file is reloaded (MAINMENU.PRG at every
  menu open). The build puts each overlay's `.data`/`.bss` in its own sections;
  `pc/src/overlay_statics.c` snapshots them at startup and restores them when
  the disc read that loads the file completes. `ROOD_OVERLAY_STATICS=0` turns
  this off for comparison.
- Unsupported function imports log `[UNIMPLEMENTED] name` and exit 2. This keeps
  missing behavior visible. Successful title boot must not be described as
  complete engine support.

## Battle probe and next work

```sh
python3 pc/tools/build_boot.py --battle
pc/build/boot/rood_battle_probe --skip-movies --fast
```

This optional executable also links the available BATTLE and INITBTL C sources.
It follows the title's idle transition, loads BATTLE, INITBTL, and SYSTEM.DAT,
and loads Ashley, equipment, ZONE020.ZND, and MAP213.MPD. It now reaches the
battle frame loop after actor setup and collision/navigation initialization.
Normal boot now passes actor/navigation frame preparation and reaches entry-script
execution and shared effect setup. It renders the original VKP Headquarters
opening card and caption. Continuing stops at scene transform `func_80098648`.
No room is playable yet.
Actor route paths still have unported callees; passing the current inactive-route
case does not establish complete AI support. VSync query mode now matches the
original API; elapsed scanlines are estimated from monotonic host time, not PS1
hardware precision. Animated-texture MoveImage uses the tested VRAM-copy decoder.

An explicit diagnostic can render the loaded mesh before that actor update:

```sh
pc/build/boot/rood_battle_probe --skip-movies --fast --room-checkpoint
```

This follows the title's idle/demo route into ZONE020 / MAP213. It deliberately
submits loaded geometry even while the entry script keeps the room hidden,
without setting the room active or advancing the script. It exits after writing
`pc/build/boot/room-checkpoint.ppm`. The current preview contains textured room
geometry but is partial and very dark; it is not a completed scene or gameplay.
Normal execution does not use this bypass. The same packet decoder is covered by
32/64-bit `gpu_probe` tests; projection and room packet generation have a separate
32-bit GTE probe.

Host-only actor preparation/commands, scene submission, and object shadows reuse
retained near-matching C candidates, with provenance in their source files.
Their rare branches are not yet validated. Two room-effect callback tables now
use typed native wrappers; the builder verifies their original targets against
the private overlay. Other embedded guest callbacks remain to be relocated.

Room submission translates group matrices, packed vertices, culling, depth
ordering, texture LOD and adaptive polygon subdivision (recursive midpoint
triangles/quads plus the original's degenerate edge triangles, in submission order).
Navigation also traps an edge case that depends on an original stack address.

Dear ImGui is planned for a developer overlay after window/backend integration:
controller state, frame timing, and renderer diagnostics. Keep original game UI
and suppress game input while the overlay captures it.

Next priorities: follow the opening through its scene transitions and reach
controllable gameplay; finish remaining actor polygon routes; implement
missing GPU commands; validate a complete, controllable
scene. Then continue audio/save support.

The original PS1 path is preserved with conditional host guards. Run the normal
`make commit-check -j4` before accepting source changes. Baseline verified on
2026-10-06: all binaries match, 1,887,248 / 1,936,592 matched code bytes (97.45%).
This percentage describes the matching decompilation, not PC-port completeness.

Regenerate `ASSEMBLY_INVENTORY.md` with `python3 pc/tools/inventory.py`. Its count
is raw source macro occurrences, not a preprocessed count of remaining functions.

### Entry-script and effect dispatch

The native build generates bounded callback tables from the private reference
BATTLE overlay, validates the targets against existing C handler signatures, and
uses typed adapters for handlers with fewer arguments. This covers the 256 event
opcodes, 49 shared effect commands, and the built-in trail renderer. The original
alternate event skip table is already native and retains its distinct handlers.
The EFFPURGE setup C now loads entry effect 229's texture and parameter files.
Effect-specific PLG callbacks remain an explicit trap: their guest addresses are
shared across different plugins and cannot be resolved by address alone.

The particle packet routine now uses portable GTE calls and an explicit native
argument, adapting the retained `gte-macros/s5.c` candidate. Its packet layout,
projection, clipping, UVs, blend flag, sprite commands, and color curves have
synthetic tests; this is not a matching-source or all-effects correctness claim.
The four GIM renderer callbacks are native and their original targets are checked.
Sound-bank loading, music start, and sound-effect volume commands follow the
existing explicit no-audio diagnostic policy. `RotMatrix_gte` preserves the original GPF stages;
rotation-order fixtures pass, but exhaustive trig-table equivalence is not claimed.
Run `python3 pc/tests/event_callbacks_test.py` for callback generation/call tests.

### Original opening-script checkpoint

```sh
python3 pc/tools/build_boot.py --battle
pc/build/boot/rood_battle_probe --skip-movies --fast --opening-checkpoint
python3 pc/tests/opening_smoke.py
```

This follows the title's idle/demo route and captures
`pc/build/boot/opening-checkpoint.ppm` after the original GIM 26 background reaches
full brightness and 60 normal battle frames allow the caption to appear. It
changes no script, room visibility, camera, or game timing state to get there.
The capture is the **VKP Headquarters — Valnain, 0:24 A.M.** opening card, with
background, caption, and shared effect rendering. It is a scripted 2D opening,
not a complete or controllable 3D room. Movies and audio remain disabled.
The smoke test requires the background and bright caption region; visual review
also confirmed the actual text. `cmake --build pc/build/probes --target check-opening`
is the equivalent build-and-test target. Without the checkpoint flag, the next
native loop now continues into the later VKP dialogue scene (see below).

### Actor geometry checkpoint

The native renderer now translates the far-clip-scaled scene transform, grouped
bone vertex projection, weapon FT3/FT4 packets (including split quads), and the
terrain-conforming actor shadow. Synthetic fixtures cover matrix/projection
rounding, packet UVs and blend flags, culling, ordering-table links, both split
quad diagonals, and shadow slope/fade/ring behavior. The builder runs these tests.
These are behavioral translations of the original assembly, not matching C.

The PC actor texture-cache scan skips null slots; the original PS1 path is
unchanged. Stop-sound commands follow the existing no-audio diagnostic policy.
The normal opening runs beyond these routines, but complete visible character
rendering remains unvalidated and other actor packet paths remain fail-fast.

### Sustained native scene checkpoint

```sh
python3 pc/tools/build_boot.py --battle
python3 pc/tests/scene_smoke.py
# Or choose another observational frame count:
pc/build/boot/rood_battle_probe --skip-movies --fast --battle-frame-checkpoint 4000
```

The original title idle/demo route now runs 4,000 battle frames and renders the
VKP interior, changing camera shots, animated actors and dialogue. The 4,000-frame
capture shows the woman discussing the manor map. No room visibility, script,
camera, or timing state is forced by this checkpoint. The image is saved as
`pc/build/boot/battle-checkpoint.ppm`; `check-scene` is the CMake equivalent.
The runtime logs each 300 completed battle frames and reports the frame of a trap.
This is sustained cutscene rendering, **not yet controllable gameplay** or a
pixel-exact emulator comparison. Movies and all sound remain disabled.

Native additions include GTE animation blending (including its original retained
IR3 edge case), fixed-width glyph quads, and flat/Gouraud lines and polylines.
Tests cover blend wrap/rounding and fast paths, font atlas/CLUT/layout/advances,
and line colors, clipping, draw offsets and command termination. The initial line
rasterizer does not claim hardware-exact dithering or edge rules. Sound-parameter
commands consistently use the explicit diagnostic no-audio policy.

### New Game input checkpoint

```sh
python3 pc/tests/new_game_smoke.py
python3 pc/tests/new_game_smoke.py --frames 5000
pc/build/boot/rood_battle_probe --window --skip-movies
```

The test opens an isolated Xvfb window and presses Enter on the real title menu.
It requires the original menu to report start mode 0 (New Game), then observes
300 completed battle frames by default. Exploratory 5,000- and 10,000-frame runs also passed, but wait at the first
dialogue prompt without further input; these are stability checks, not equivalent
amounts of story progression.
This follows ZONE001 rather than the title's ZONE020 idle cutscene. The capture is
`pc/build/boot/new-game-checkpoint.ppm`; the transcript is `new-game-test.log` in
the same ignored build directory. `check-new-game` runs the default check.
The opening remains a cutscene; player control and later transitions are not
claimed to work yet.

New Game exposed a zero-particle emission that read an uninitialized node pointer.
The PC guard retains setup side effects while avoiding that unused read. Native
room-light interpolation now handles triangle colors, signed bounds and fallback
colors; its wrapper explicitly forwards the output pointer that the PS1 ABI kept
in a register. Focused interpolation tests and the real input path cover these
fixes. Music volume commands still use the explicit no-audio diagnostic policy.

The per-vertex-colored character GT3/GT4 packet path is also native. Its focused
fixtures cover normal and special triangles/quads, vertex colors, UVs, blend and
depth flags, culling and OT links. Broader live-scene coverage and the blended
actor/weapon variants remain outstanding.


### Dialogue and scene-transition work

```sh
pc/run-game.sh
# Automated real-input exercise (later unported paths may still trap):
python3 pc/tests/new_game_smoke.py --frames 4000 --advance-dialogue
```

The launcher builds the battle-capable diagnostic (`--no-build` skips that)
and opens the original title, on the NVIDIA GPU when there is one (PRIME
offload; `ROOD_GPU=integrated` keeps the default GPU) with path-traced
lighting on (`ROOD_GI=0` turns it off).
Enter selects New Game; `z` is Circle/confirm and `x` is Cross. The launcher
plays the intro movie (pass `--skip-movies` to skip it). The smoke-test option sends ordinary confirm input after each
300-frame marker, allowing dialogue to advance without rewriting script state.

SCREFF2 now has overlay-specific native function names, its original C tile and
border effects, and a native translation of the radial polygon distortion.
Focused tests cover packet layouts, radial scale recurrence and OT restoration;
visual equivalence to a PS1 remains unverified. Scene unload keeps its sound-data
cleanup while omitting device commands under the explicit no-audio diagnostic.
Actor disposition packing has a native translation with exhaustive round-trip
coverage against the existing option unpacker.

Actor unload also translates reference removal, occupancy clearing, effect stop
callbacks, actor counters and heap release. A focused fixture checks these state
changes independently. The dialogue-driven route completes cleanup near frame 2,745 and loads ZONE002
room and actor assets. Its next music entry follows the same logged no-audio
policy. Rendering the complete second scene and player control remain unverified.
The PC actor-removal loop skips empty slots: GDB confirmed the original loop
dereferenced a null slot while removing actor 2. The PS1 build is unchanged.


Input-driven checkpoint: `--frames 6000 --advance-dialogue` **passes**. It
renders the ZONE002 gateway close-up through adaptive room subdivision (frame
2,883 onward) and continues to the next courtyard dialogue scene at frame 6,000
(actors, room and speech bubble). This is still cutscene progress, not verified
player control. The second-zone room and actor loads, first-room teardown, and
subsequent music restart are reached through the original scripts and ordinary
confirm input.

Room subdivision (`pc/src/room_geometry.c`) translates `BATTLE/2EA3C.s`
`func_80097AEC`/`CF0`/`F70`: 0x14-byte vertex records, byte UV/colour averages,
arithmetic-shift XYZ midpoints, GTE re-projection, signed 16-bit leaf depths,
the triangle-code conversion (`& 0xF7000000`) and the exact recursive order,
including degenerate depth-zero edge triangles. `gte_probe` checks a level-1
triangle's packets, UV/clut/tpage words and ordering-table links.

The enemy-AI candidate evaluation that starts after the gateway scene is
hand-written assembly. Native translations with focused fixtures now cover
`func_800E685C`, `func_800E4DF8`, `func_800E4F14`, `func_800E4CF4` (7C4F4/7E05C),
`func_800DC810`, `func_800DC484`/`48C`, `func_800DC4F0` (73C84), `func_800DC424`
(73C24), `func_800DC3E8`/`344`/`3CC` (73B0C, including the K&R candidate
quicksort from 7E05C), `func_800DC2E0` (73AE0), `func_800D9DD8` (70D4C) and the
MVMVA weighted sum `func_800DC284` (7399C, `pc/src/battle_gte.c`). Remaining
routines in those files (`72B70.s`, `func_800DC30C`, `func_800DC574`,
`func_800DC784`, `func_800DC19C`/`210`, the rest of `7E05C.s`)
still trap explicitly when reached. The per-frame AI driver `func_800E511C`
(fixed table `D_800F16BC`, called directly, scratchpad stack switch omitted),
`func_800D820C`/`826C`/`8280`, `func_800E4358`, `func_800E4624`,
`func_800E4478`/`44E8` (floor and line of sight) and `func_800E437C` (facing
cosine) followed. `--frames 7200 --advance-dialogue` passes (Ashley: "Here we
go..."); `--frames 9000` stops at frame 7,286 on `func_800DB820` (`72B70.s`).
Two more no-audio guards skip sound-effect
playback (`func_800461CC`) and the sound stop command (`func_80045DC0`).


A shorter `--frames 2850 --advance-dialogue` run **passes** and captures the
ZONE002 exterior: rooftops, lit gateway and rain. The inspected enlarged image
is retained locally as `pc/build/boot/new-game-zone2-2850.png` (ignored game data).
This is a visible second-scene checkpoint, not proof of controllable gameplay.

Subdivision investigation notes: `func_80097F70` averages signed 16-bit XYZ with
arithmetic shifts (negative odd sums round down), and averages unsigned byte UV
and RGB channels independently. `func_80097AEC` makes four recursive triangle
calls followed by three depth-zero triangle calls; preserve that submission order
rather than assuming a generic four-triangle tessellator. Leaf packets use signed
16-bit depth samples. These details need independent fixtures before replacing
the current trap.
