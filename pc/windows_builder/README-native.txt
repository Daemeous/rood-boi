rood-boi for Windows - builder (no WSL)
=======================================

rood-boi is a native PC version of Vagrant Story (by noprompt, built on the
rood-reverse decompilation), with modern graphics options. This folder builds it
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
3. (Optional) Open "rood-boi options.ini" in Notepad to choose whether to
   include Merlose and her extras (see OPTIONS).
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


OPTIONS (rood-boi options.ini)
------------------------------
* merlose: also build the game with Callo Merlose as an AI companion.
* max_stat_items: (Merlose builds) permanent stat-up items always give +4.
* choosable_bonus_wheel: (Merlose builds) choose the bonus wheel reward
  after a boss instead of spinning for it.
Change a value, save, and run "Build rood-boi.bat" again.

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
