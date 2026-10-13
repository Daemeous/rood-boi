# Third-party code and licences

The project's own code (the decompilation inherited from
[rood-reverse](https://github.com/ser-pounce/rood-reverse) and the native PC
port under `pc/`) is dedicated to the public domain under **CC0 1.0 Universal**
(see [`LICENSE`](LICENSE)). The CC0 dedication does **not** cover the
third-party code listed below, which keeps its own licence; each component's
licence file sits in its directory.

This repository contains no game data. *Vagrant Story* is owned by Square Enix;
you must own the game and supply your own disc image (see [`README.md`](README.md)).

## Vendored in `pc/third_party/`

| Component | Version | Licence | Notes |
|---|---|---|---|
| [SDL 3](https://github.com/libsdl-org/SDL) (`SDL3/`) | 3.4.18 | zlib (`LICENSE.txt`) | Trimmed release tarball, no source changes (`UPSTREAM.txt`). Its bundled `src/hidapi` is tri-licensed and used under its BSD licence (`src/hidapi/LICENSE-bsd.txt`). Bundled Khronos headers (EGL/GLES/Vulkan) and Wayland protocol XML keep the licences stated in their files. |
| [Dear ImGui](https://github.com/ocornut/imgui), docking branch (`imgui/`) | 3bae66c7 | MIT (`LICENSE.txt`) | Includes the stb_rectpack/stb_textedit/stb_truetype headers (MIT or public domain, see their headers); `pc/src/ui_text.c` also compiles stb_truetype to rasterize the `ui_text` fonts. |
| [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) (`ImGuizmo/`) | 18cef5e0 | MIT (`LICENSE`) | |
| [glslang](https://github.com/KhronosGroup/glslang) (`glslang/`) | 16.6.0 | BSD-3-Clause, BSD-2-Clause, MIT, Apache-2.0 and others per file (`LICENSE.txt`, `LICENSES/`) | Built only as the shader-compiler tool; nothing of it is linked into the game. The Bison-generated parser (`glslang/MachineIndependent/glslang_tab.cpp`) is GPL-3.0-or-later **with the Bison special exception** (`LICENSES/Bison-exception-2.2.txt`), which permits distributing it under glslang's own terms. |
| [SMAA](https://github.com/iryoku/smaa) (`smaa/`) | 71c806a8 | MIT, Copyright (C) 2013 Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro, Diego Gutierrez (`LICENSE.txt`) | The reference `SMAA.hlsl`, unchanged, used through its GLSL macros by the anti-aliasing passes (`pc/shaders/vulkan/smaa_*.frag`, embedded for OpenGL), and its precomputed area/search lookup textures `Textures/AreaTex.h`, `SearchTex.h` (`pc/src/antialias.c`); `UPSTREAM.txt`. |
| [PsyCross](https://github.com/OpenDriver2/PsyCross), geometry subset (`psycross/`) | e56e4cde | MIT, Copyright REDRIVER2 Project (`LICENSE`) | Software GTE and SDK geometry wrappers; changes listed in `ROOD-NOTES.md`. |
| PsyCross software SPU (`psycross-audio/`) | from [SlickAmogus/PsyCross](https://github.com/SlickAmogus/PsyCross), a PsyCross fork | MIT, Copyright REDRIVER2 Project (`LICENSE`) | SPU core by Chris Hardin. The reverb unit (`PsyX_SPUReverb`, `PsyX_IdealReverb`) and the noise generator were rewritten for rood-boi clean-room from the public psx-spx / nocash hardware documentation and are released under the same MIT licence; see `ROOD-NOTES.md`. |

### Fonts (`pc/third_party/fonts/`)

Used by the optional `ui_text font` setting (`pc/src/ui_text.c`, README "2D /
UI"), which draws the game's text with them. Each is the unmodified TrueType
file from the [Google Fonts repository](https://github.com/google/fonts)
(`ofl/<family>/` or `apache/<family>/`) and keeps its own licence, which sits
beside it: the **SIL Open Font License 1.1** (`OFL.txt`, with the copyright
notice; the fonts may be bundled and redistributed with this software but
not sold by themselves, and Reserved Font Names apply to modified versions)
or the **Apache License 2.0** (`LICENSE.txt`). None of them is derived from
the game.

| Font | File | Licence | Copyright |
|---|---|---|---|
| [Permanent Marker](https://github.com/google/fonts/tree/main/apache/permanentmarker) | `PermanentMarker/PermanentMarker-Regular.ttf` | Apache 2.0 (`LICENSE.txt`) | Font Diner, Inc. (2010) |
| [Asap Condensed](https://github.com/Omnibus-Type/Asap) Bold | `AsapCondensed/AsapCondensed-Bold.ttf` | OFL 1.1 | The Asap Project Authors |
| [Chakra Petch](https://github.com/m4rc1e/Chakra-Petch) Bold | `ChakraPetch/ChakraPetch-Bold.ttf` | OFL 1.1 | The Chakra Petch Project Authors |
| [Alegreya](https://github.com/huertatipografica/Alegreya) (variable, default instance) | `Alegreya/Alegreya[wght].ttf` | OFL 1.1 | The Alegreya Project Authors |

**Kenney Input Prompts** (`KenneyInputPrompts/`), version 1.5A by
[Kenney](https://kenney.nl/assets/input-prompts), **CC0 1.0** (`License.txt`):
the icon fonts of the PlayStation, Xbox, Nintendo Switch and keyboard sets
(`kenney_input_<set>.ttf`, unmodified; the keyboard file renamed from
`kenney_input_keyboard_&_mouse.ttf`) with their glyph name maps
(`kenney_input_<set>_map.txt`, line endings made LF). Used by the
`button_prompts` setting (`pc/src/button_prompts.c`), which draws the PS1
buttons the game shows as the bound key or gamepad button. Not derived from
the game.

## Ported into the project's sources

- **Weapon slots** (`pc/src/weapon_slots.c` and the `ROOD_PC` hooks it lists):
  ported from eremtsov's Vagrant Story QoL mod
  (https://github.com/eremtsov/rood-reverse, branch `qol-mod-classic`,
  `src/mod/wswap.c` and the MENU7 Container fix), reimplemented for the
  native build with the same behaviour and save format. Released by its
  author under CC0 1.0, like rood-reverse. Only the weapon slots are ported.
- **MMPX** (`pc/src/ui_upscale.c` `pc_ui_mmpx2x`, `pc/shaders/vulkan/ui_mmpx.comp`):
  ported from the reference implementation of Morgan McGuire and Mara Gagiu,
  "MMPX Style-Preserving Pixel Art Magnification", JCGT 10(2), 2021
  (supplement at https://casual-effects.com/research/McGuire2021PixelArt/).
  MIT licence, Copyright 2020 Morgan McGuire & Mara Gagiu: permission is
  hereby granted, free of charge, to any person obtaining a copy of this
  software and associated documentation files (the "Software"), to deal in
  the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions: the above
  copyright notice and this permission notice shall be included in all copies
  or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS",
  WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
  TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
  FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
  THE USE OR OTHER DEALINGS IN THE SOFTWARE. These ported parts stay under that
  MIT licence, not CC0.

## Downloaded at build time, not redistributed

`pc/tools/nrd.py` (driving `pc/tools/nrd/CMakeLists.txt`) downloads these into
the ignored `pc/build/nrd` directory when the 64-bit build is made with NRD.
None of their files are committed to this repository, and they remain under
their own licences:

- [NVIDIA NRD](https://github.com/NVIDIA-RTX/NRD) (Real-time Denoisers),
  pinned commit `7033ffbc`, under the NVIDIA RTX SDKs licence.
- Dependencies NRD's own build fetches: NVIDIA ShaderMake and MathLib, and the
  DirectX Shader Compiler (DXC), each under its own licence. DXC only compiles
  NRD's shaders and is deleted after the build.

Building without NRD (`build_boot.py --nrd off`, or any 32-bit build) downloads
nothing.

The original-game matching build (`make`) likewise downloads its toolchain
(decompals old-gcc, mkpsxiso, objdiff and the Python packages in
`requirements.txt`) into ignored directories; see the upstream rood-reverse
documentation.

## Inherited from rood-reverse

- `include/psx/*.h`: PsyQ SDK library headers, as published by the upstream
  decompilation (needed to rebuild the original binaries).
- `assets/*.yaml`: menu and item string tables extracted by the upstream
  decompilation's asset pipeline.
- The decompiled C and assembly under `src/` (this fork adds further
  decompiled functions and hand-written-assembly `.s` files in the same
  decompilation conventions).
