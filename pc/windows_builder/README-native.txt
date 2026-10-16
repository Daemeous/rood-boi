rood-boi for Windows - builder (no WSL)
=======================================

rood-boi is a native PC version of Vagrant Story (by noprompt, built on the
rood-reverse decompilation by ser-pounce and its other contributors),
with modern graphics options. This folder builds it
on your own PC, from your own copy of the game. Optionally it also builds the
Callo Merlose companion mod ("Two's A Riot").

This builder needs no WSL, no administrator rights and no restart: it builds
with ordinary Windows programs (MSYS2, Python and Windows builds of the
PlayStation compilers), which it downloads into its own folder.

Nothing from the game is included or downloaded: the build reads your disc
image, and the result is for you only. Please don't share the built folder.


WHAT YOU NEED
-------------
* Windows 10 or 11, 64-bit, with about 20 GB free on this drive.
* Your own Vagrant Story (USA), SLUS-01040, as a .bin + .cue, UNMODIFIED
  (no Zenith, Randomizer or other patches). Other formats (.chd, .iso,
  .pbp) and other versions (Europe, Japan) don't work.
* A graphics card with Vulkan 1.3 (most cards from the last ~8 years), or
  OpenGL as a fallback.
* An internet connection for the first build (about 1.5 GB of tools).


HOW TO BUILD
------------
1. Extract this zip: right-click it, choose "Extract All...". Put the
   folder somewhere with space, e.g. C:\Games\rood-boi-builder.
   (If the folder's path has spaces or non-English letters, the build tools
   go in C:\rood-boi-build instead; the builder tells you.)
2. Copy your Vagrant Story .bin and .cue into the extracted folder.
3. Have a look at OPTIONS below: the builder starts by asking which ones
   you want.
4. Double-click "Build rood-boi.bat".
   If Windows says "Windows protected your PC", click "More info", then
   "Run anyway".
5. Wait. The first build takes about 30-60 minutes (most of it setting up
   the tools); later ones are quicker.

When it's done, open the new "rood-boi" folder and double-click:
* play.bat        - the game
* play-callo.bat  - the game with Merlose (if you chose her)

Escape or F1 in the game opens the editor and settings. Your saves and
settings are in rood-boi\pc\save and rood-boi\pc\user, and are kept when
you build again.


HD TEXTURE PACKS (optional)
---------------------------
Both normal and Merlose builds support optional Vagrant Story texture packs
with Vulkan or OpenGL. No packs are included or downloaded automatically.
Without an installed pack, the game uses its original textures. You do not
need RetroArch or DuckStation installed.

First build the game, then extract your separately downloaded pack (for
example with 7-Zip). Put it inside the built rood-boi folder, alongside
play.bat, using the layout for that pack:

* Classic Beetle PSX HW pack: rename the image folder to
  texture-replacements. The CRC-named .png or .dds files go directly in it.
* DuckStation pack: put its SLUS-01040 folder inside texture-replacements.
  Keep SLUS-01040/replacements (the images) and config.yaml together.
  Individual texupload-* and texpage-* image names must remain unchanged.
* Beetle page-aligned pack: rename its image folder to
  texture-replacements-pages. Keep these files separate from classic packs:
  identical filenames refer to different VRAM regions.

The folders are detected at startup and kept when you rebuild. Install one
complete pack at a time when packs replace the same art; classic Beetle
matches take priority over page/DuckStation matches. To keep a pack
elsewhere, set ROOD_TEXTURE_PACK to its extracted root/image folder before
starting; ROOD_TEXTURE_PACK_PAGES selects a separate Beetle page folder.
Reduced-palette Beetle hashes are detected automatically.

To compare with originals: F1 (or Escape), Graphics, "HD texture
replacements". To use an HD font pack, also choose 2D / UI, Text, Original.
Selecting a rebuilt font intentionally keeps that font instead.

Tested DuckStation examples: diodiogod's Vagrant Story HD dialogue font
(v0.4 beta, https://github.com/diodiogod/vagrant-story-hd-dialogue-font) and
IceBullet's Vagrant Story PS1 HD pack. Download packs separately from their
authors; they remain entirely optional components.

Missing replacements keep original textures. Initial image loads/cache
refills can briefly pause. Broad room, animation and palette coverage needs
more playtesting. Native save-state loads need a room reload to restore
texture matching; normal in-game saves are unaffected.

Supported images: PNG and common DDS (RGB/RGBA/BGRA8, BC1/2/3/4/5 UNORM,
BC7; base mip only). Legacy DuckStation vram-write hashes, JPEG/WebP,
arbitrary upload-coalescing/copy-to-write cache options and uncommon DDS
encodings are not supported. DDS and extended Beetle modes were checked
with generated fixtures; the two DuckStation packs above were tested
in-game. More details: pc/docs/HD_Textures.md in the downloaded source.


SAVES AND MEMORY CARDS
----------------------
rood-boi saves to PS1 memory card files, the same kind emulators use:
* Slot 1: rood-boi\pc\save\card1.mcd (made on your first save)
* Slot 2: rood-boi\pc\save\card2.mcd (only if that file exists)

To use another card file: in the game press F1 (or Escape), open the
File menu, then "Memory cards...". Type or paste the file's path for a
slot, click Use, then "Save settings" in that window so it's remembered.
"Default" goes back to pc\save. (Or edit the card1 / card2 lines in
rood-boi\pc\user\settings.cfg.) Switch cards outside the save and load
screens.

Card files it can use:
* .mcd, .mcr, .srm - raw card images (DuckStation, ePSXe, PCSX,
  RetroArch Beetle PSX, MemcardRex): loads and saves.
* .gme (DexDrive) and .mem / .vgs (Connectix VGS): loads and saves, and
  keeps the file in its own format.
* .vmp (PSP / PS Vita): loads only; saving is refused.
Any other file is left alone: the slot shows an empty card and saving
fails, so a wrong path can't damage anything.

Playing your emulator saves: point a slot at the emulator's card, e.g.
DuckStation's
  %LOCALAPPDATA%\DuckStation\memcards\Vagrant Story (USA)_1.mcd
(older DuckStation versions: Documents\DuckStation\memcards). Don't have
the same card file open in rood-boi and an emulator at the same time:
each keeps its own copy and whichever saves last wins. The safe way is to
copy the card into rood-boi\pc\save and point the slot at the copy.

Single-save files (.mcs, .psx, .mcb, .ps1 ...) aren't cards: put them on
a card file first, e.g. with MemcardRex.


OPTIONS (the builder asks)
--------------------------
The builder starts by asking which of these you want:
* Merlose: also build the game with Callo Merlose as an AI companion,
  with its own play-callo file next to the normal one.
* Always +4: (Merlose builds) permanent stat-up items always give +4
  instead of a random +1 to +4.
* Choose the bonus wheel reward: (Merlose builds) after a boss the bonus
  wheel does NOT spin: you pick the reward yourself with Down/Up and
  Circle. If you're not sure, answer N.
* Bigger inventory: (Merlose builds, EXPERIMENTAL, may be unstable) twice
  as many weapons, shields, blades, grips, armour and gems in your bag.
  When you save, the extra items are kept in the last quarter of the
  Container, so the Container holds a quarter less while it's on. Your
  saves stay compatible: build again without it and the extra items are
  in your Container. The new-style PC menus aren't available with it yet.
Your answers are remembered (in "rood-boi options.ini"): the next build
shows them and asks whether to keep them. Answer N there to change them.

The QoL mod's weapon slots (L2 + R2 quick swap) are already part of
rood-boi: turn them on in the editor (Escape or F1 in the game). Zenith, the Randomizer and
the rest of the QoL mod are patches for the PlayStation version and don't
work with rood-boi.


UPDATING
--------
Run "Build rood-boi.bat" again: it downloads the latest code and rebuilds.


IF SOMETHING GOES WRONG
-----------------------
* Everything the build prints is also in build.log in this folder.
* Download errors: check your internet connection; a VPN or antivirus can
  block the downloads. Running the .bat again retries.
* Antivirus software can slow the build down a lot (it checks every file
  the compilers write). Excluding the builder folder in it helps.
* "files don't match": your .bin isn't a clean USA image.
* If the game doesn't start, look at rood-boi\rood.log.
* Problems: https://github.com/Daemeous/rood-boi/issues


CLEANING UP
-----------
The build tools are in files\native (or C:\rood-boi-build), about 10 GB.
To free the space, run "Remove build tools.bat"; the game folder stays.
Building again later sets them up again.


THE BUILD TOOLS
---------------
The builder downloads MSYS2 (https://www.msys2.org), a portable Python
(python.org's NuGet package) and rood-boi's native tools: Windows builds of
the PlayStation compilers (GCC 2.7.2 and 2.8.1, with decompals/old-gcc's
patches) and GNU binutils, from the rood-boi release page, where their
complete source code is published too.


CREDITS
-------
* rood-boi, the native PC port: noprompt (https://github.com/noprompt/rood-boi).
  The Windows port and these builders: Daemeous.
* rood-reverse, the Vagrant Story decompilation rood-boi is built on:
  ser-pounce (https://github.com/ser-pounce), who started it and has done
  the overwhelming majority of the work, and every other contributor.
  https://github.com/ser-pounce/rood-reverse
* rood-boi's own thanks (the QoL mod's weapon slots by eremtsov, Kenney's
  button prompt icons, Andrew Kuhar's UI remaster that inspired the modern
  menus, and more) are on its page:
  https://github.com/noprompt/rood-boi#thank-you
* The native builder uses MSYS2 (https://www.msys2.org), Python
  (python.org) and Windows builds of decompals' old-gcc
  (https://github.com/decompals/old-gcc) and GNU binutils.
* The Merlose mod ("Two's A Riot"): Daemeous.
* Vagrant Story (c) Square Enix. This is a fan project, not affiliated
  with or endorsed by Square Enix.
