rood-boi for Linux - builder
============================

rood-boi is a native PC version of Vagrant Story (by noprompt, built on the
rood-reverse decompilation), with modern graphics options. This folder builds it
on your own PC, from your own copy of the game. Optionally it also builds the
Callo Merlose companion mod ("Two's A Riot").

Nothing from the game is included or downloaded: the build reads your disc
image, and the result is for you only. Please don't share the built folder.


WHAT YOU NEED
-------------
* A 64-bit x86 Linux with glibc 2.39 or newer (Ubuntu 24.04+, Debian 13,
  Fedora 40+, Arch/CachyOS/Manjaro, SteamOS 3.6+ ...) and about 15 GB free.
* Podman (recommended) or Docker. If neither is installed, the builder tells
  you the command for your distribution, e.g.:
    Ubuntu/Debian:  sudo apt install podman
    Arch/CachyOS:   sudo pacman -S podman
    Fedora:         sudo dnf install podman
  SteamOS already has Podman.
* Your own Vagrant Story (USA), SLUS-01040, as a .bin + .cue, UNMODIFIED
  (no Zenith, Randomizer or other patches). Other formats (.chd, .iso, .pbp)
  and other versions (Europe, Japan) don't work.
* A graphics driver with Vulkan 1.3 (Mesa or NVIDIA's), or OpenGL as a
  fallback.
* An internet connection for the first build (about 1.5 GB).

Nothing is installed on your system except Podman: the build tools live in a
container image.


HOW TO BUILD
------------
1. Extract this archive somewhere with space, e.g. ~/Games/rood-boi-builder.
2. Copy your Vagrant Story .bin and .cue into that folder.
3. Have a look at OPTIONS below: the builder starts by asking which ones
   you want.
4. In a terminal, in that folder, run:
     ./build-rood-boi.sh
   (or right-click it in your file manager and run it in a terminal).
5. Wait. The first build takes about 20-40 minutes; later ones are quicker.

When it's done, run:
* rood-boi/play.sh        - the game
* rood-boi/play-callo.sh  - the game with Merlose (if you chose her)

Escape or F1 in the game opens the editor and settings. Your saves and
settings are in rood-boi/pc/save and rood-boi/pc/user, and are kept when
you build again. On a laptop with an NVIDIA GPU the game uses it
automatically (ROOD_GPU=integrated keeps the default GPU).


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
rood-boi: turn them on in the editor (Escape or F1 in the game). Zenith,
the Randomizer and the rest of the QoL mod are patches for the PlayStation
version and don't work with rood-boi.


UPDATING
--------
Run ./build-rood-boi.sh again: it downloads the latest code and rebuilds.


IF SOMETHING GOES WRONG
-----------------------
* Everything the build prints is also in build.log in this folder.
* "files don't match": your .bin isn't a clean USA image.
* If the game doesn't start, look at rood-boi/rood.log (or rood-callo.log).
* Docker "permission denied": add yourself to the docker group, or use
  Podman instead.
* Problems: https://github.com/Daemeous/rood-boi/issues

Prefer to build by hand? rood-boi's own README has the steps; with the
"callo" branch of https://github.com/Daemeous/rood-boi,
pc/run-game.sh --vulkan --callo builds and plays the game with Merlose.


CLEANING UP
-----------
To free the space the build tools and build files take (several GB), run
./remove-build-tools.sh. The game folder stays.
