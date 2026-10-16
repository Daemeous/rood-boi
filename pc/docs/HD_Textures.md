# HD texture packs

The native hardware renderers can read Beetle PSX HW upload-rectangle PNG texture packs.
Extract the pack first, then set `ROOD_TEXTURE_PACK` to the directory containing the PNGs.
No RetroArch installation or disc patch is needed. Packs remain external to the game.

Windows example (a `.cmd` file in the game folder):

```bat
@echo off
cd /d "%~dp0"
set "ROOD_TEXTURE_PACK=G:\Users\sharo\Downloads\vs-hd-pack\Vagrant Story (USA)-texture-replacements"
rood.exe --window
```

Linux example:

```sh
ROOD_TEXTURE_PACK="/path/to/Vagrant Story (USA)-texture-replacements" ./pc/run-game.sh
```

Graphics > HD texture replacements switches replacements on or off without restarting.
Its settings key is `hd_textures` (0 or 1). RPC `hd_textures` returns index, match,
resident image and error counts; `{ "enabled": false }` switches them off for comparison.
The startup log records the indexed file count and first loaded images.

## Matching and rendering

Names are hexadecimal `uploadCRC-paletteCRC.png`, without mandatory zero padding.
Both CRCs use CRC32 of the original little-endian PS1 words, in row order: the
complete upload rectangle and the active 16- or 256-entry palette. Direct color
uses palette CRC 0. VRAM addresses are not part of the key. Partial overwrites
invalidate only the affected words. Unmasked VRAM copies carry source coordinates;
masked copies and rendered-to regions fall back to originals.

The original VRAM, game logic, readbacks and saves keep their original data.
Replacement pixels are sampled at their actual resolution by the OpenGL and Vulkan
packet and native mesh renderers. PNG alpha uses Beetle's convention: 0 is clear,
255 opaque, intermediate values select PS1 semi-transparency. It is not ordinary
fractional coverage. Native rebuilt font text retains the selected font.

The atlas is bounded to 8192 x 8192 RGBA pixels (256 MiB with a configured pack),
plus tracking tables and temporary uploads. Images load only when their original
texture and palette are used. Missing, unreadable or oversized images use originals.
If the atlas fills, remaining images use originals until the following frame resets
its contents. First use can pause while PNGs decode; asynchronous loading and a
more persistent eviction policy are future improvements.

## Scope and validation

This supports the classic PNG upload-rectangle naming scheme, not DuckStation packs,
DDS files, Beetle page-aligned packs, or reduced-palette hash extensions. Native save
states do not preserve upload provenance: boot normally with a pack enabled; a room
reload restores textures after a state load. Interpolated frame replays use the
current texture provenance (animated texture transitions need further verification).

Run `python3 pc/tests/hd_textures_test.py` after building SDL3. This checks CRCs
against independent Python zlib fixtures, palette modes, PNG loading, partial writes,
copy offsets, wrapping, masked fallback and unchanged original VRAM.

For a real pack comparison on the opening scene (private empty cards):

```sh
ROOD_ARCH=64 GALLIUM_DRIVER=d3d12 ROOD_HEADLESS=egl \
  ROOD_TEXTURE_PACK="/path/to/pack" python3 pc/tests/hd_textures_smoke.py
```

The GPU sampler probe is `SDL_VIDEO_DRIVER=offscreen GALLIUM_DRIVER=d3d12
python3 pc/tests/hd_textures_test.py --gpu`. It checks actual PNG colors and the
on/off/on switch by reading rendered pixels back, independently of the game.
