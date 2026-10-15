rood-boi for Windows - builder
==============================

rood-boi is a native PC version of Vagrant Story (by noprompt, built on the
rood-reverse decompilation), with modern graphics options. This folder builds it
on your own PC, from your own copy of the game. Optionally it also builds the
Callo Merlose companion mod ("Two's A Riot").

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
* An internet connection for the first build (about 1 GB of tools).


HOW TO BUILD
------------
1. Extract this zip: right-click it, choose "Extract All...". Put the
   folder somewhere with space, e.g. C:\Games\rood-boi-builder.
2. Copy your Vagrant Story .bin and .cue into the extracted folder.
3. Have a look at OPTIONS below: the builder starts by asking which ones
   you want.
4. Double-click "Build rood-boi.bat".
   If Windows says "Windows protected your PC", click "More info", then
   "Run anyway".
5. The first time, it may need to install WSL (Windows Subsystem for
   Linux), which the build runs in. Windows asks for administrator rights,
   and you need to restart your PC afterwards. Then run
   "Build rood-boi.bat" again.
6. Wait. The first build takes about 20-40 minutes; later ones are quicker.

When it's done, open the new "rood-boi" folder and double-click:
* play.bat        - the game
* play-callo.bat  - the game with Merlose (if you chose her)

Escape or F1 in the game opens the editor and settings. Your saves and
settings are in rood-boi\pc\save and rood-boi\pc\user, and are kept when
you build again.


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
* "Temporary failure resolving" or "can't reach the internet": WSL can't get
  online. Turn off any VPN, allow WSL in your antivirus or firewall, or run
  "wsl --shutdown" in PowerShell and try again. (The builder already tries
  public DNS servers by itself.)
* "files don't match": your .bin isn't a clean USA image.
* If the game doesn't start, look at rood-boi\rood.log.
* "virtualization is disabled": turn it on in your PC's BIOS/UEFI settings
  (often called SVM, AMD-V or VT-x), then try again.
* Problems: https://github.com/Daemeous/rood-boi/issues


CLEANING UP
-----------
The build tools are a private Ubuntu in files\linux (about 7 GB). To free
the space, run "Remove build tools.bat"; the game folder stays. Building
again later sets them up again.
