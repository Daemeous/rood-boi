# HD texture packs

The native OpenGL and Vulkan renderers can read optional Beetle PSX HW and DuckStation texture packs (PNG or DDS images).
Extract a classic Beetle pack first. Rename its image folder to `texture-replacements` and put it
next to `play.bat` / `play.sh` in your built game folder. It is detected on startup.
Alternatively, set `ROOD_TEXTURE_PACK` to the directory containing the PNGs.
No RetroArch installation or disc patch is needed. Packs remain external and optional; absent or unmatched images use the original textures.

For a **DuckStation pack**, put its extracted `SLUS-01040` folder inside
`texture-replacements` (keep `config.yaml` beside its `replacements` subfolder).
You may also use the existing `textures/SLUS-01040/replacements` layout beside the
launchers, or point `ROOD_TEXTURE_PACK` at the extracted pack root. The scanner
recognizes `texupload-*` and `texpage-*` filenames, including subrectangles and
palette ranges. No manual renaming of individual image files is needed.

For **Beetle page-aligned packs**, put the image folder at
`texture-replacements-pages`, beside `texture-replacements` and the launchers.
`ROOD_TEXTURE_PACK_PAGES` overrides this directory. Keep classic and page-aligned
Beetle files in separate folders: identical filenames mean different VRAM regions.
Both can be installed together; classic upload matches take priority.
Reduced-palette Beetle hashes are detected automatically, with full-palette fallback.

DuckStation YAML `Aliases` entries are read (inline values or literal blocks);
keep renamed images inside the pack's `replacements` folder. Dumping/cache options
are emulator-specific and are not applied to rood-boi. Arbitrary copy-to-write,
upload coalescing and split-write settings are not emulated.

DDS support covers 16/24/32-bit legacy RGB masks, RGBA8/BGRA8, BC1/2/3/4/5 UNORM
and BC7 (legacy DXT1/3/5 or DX10 headers as applicable). Only the base mip is used.
BC6H, signed BC4/5, arrays, cubemaps, volumes and premultiplied DXT2/4 are rejected.
DDS replaces PNG as an image container, not as a different texture naming scheme.
When both exist for the same ID, PNG takes priority. JPEG/WebP images and old
`vram-write-<128-bit hash>` replacements are not currently supported.

To use an HD font pack, select **2D / UI > Text > Original** so the game's glyphs
are sampled from the pack. Selecting a rebuilt font intentionally keeps that font.

Windows example (a `.cmd` file in the game folder):

```bat
@echo off
cd /d "%~dp0"
set "ROOD_TEXTURE_PACK=C:\Games\HD textures\Vagrant Story (USA)-texture-replacements"
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

Replacements live in a VRAM atlas of 8192 x 8192 RGBA layers (256 MiB each),
added as they are needed up to `ROOD_HD_VRAM_MB` (default 2048: eight layers).
When every layer is full, the layer drawn from least recently is reused at a frame
boundary; images on the other layers stay. A layer drawn from in the last two
frames is never cleared, so a scene needing more than the budget shows the excess
with original textures instead of reloading every frame. Decoded images are kept
in a RAM cache (`ROOD_HD_RAM_MB`, default 256; 0 keeps none), so a reused layer
refills without reading the disk. Images are read and decoded on two background
threads: a replacement seen for the first time keeps its original texture until
it arrives, usually the next frame. `ROOD_HD_SYNC_LOAD=1` decodes on the game
thread instead. Missing, unreadable or oversized images use originals. RPC
`hd_textures` reports the layers in use, evictions, the RAM cache and queued loads.
The design follows DuckStation's and Beetle PSX's replacement caches.

## Scope and validation

Supported matching schemes are classic Beetle upload rectangles (full/reduced CLUT),
Beetle page-aligned replacements, and DuckStation `texupload`/`texpage` rectangles
with XXH3 texture/palette hashes and explicit palette ranges. DuckStation page
composition preserves original sampling outside replaced rectangles, including
partial upload invalidation. This is not a complete reimplementation of every
DuckStation texture-cache tracking/configuration option. Native save
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


Extended-format fixtures: `python3 pc/tests/hd_formats_test.py --gpu` (Linux,
system `libxxhash` required for independent fixture generation). They check
DuckStation upload/page rectangles, aliases, partial invalidation, reduced palettes,
DDS decoding and invalid DDS rejection; the GPU probe verifies on/off/on and
uncovered-pixel fallback. Run with `SDL_VIDEO_DRIVER=offscreen GALLIUM_DRIVER=d3d12`
on WSL. `hd_textures_test.py --gpu --dds` repeats the classic sampler tests with DDS.

Real-pack test sources:
- diodiogod's [Vagrant Story HD dialogue font](https://github.com/diodiogod/vagrant-story-hd-dialogue-font), v0.4 beta, DuckStation page rectangles.
- IceBullet's Vagrant Story PS1 HD pack, DuckStation upload rectangles (original download linked in the author's video/community thread).

Format references: [DuckStation's texture replacement documentation](https://github.com/stenzek/duckstation/wiki/Texture-Replacement),
[Beetle page-aligned format](https://github.com/libretro/beetle-psx-libretro/blob/master/PAGE_ALIGN.md),
and [Beetle palette-range hashing](https://github.com/libretro/beetle-psx-libretro/blob/master/HD_TEXTURE_CACHE.md).
