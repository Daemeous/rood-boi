# PsyCross software SPU subset
Source: the software SPU in Chris Hardin's PsyCross fork,
https://github.com/SlickAmogus/PsyCross (`src/audio`, October 2026), a fork of
https://github.com/OpenDriver2/PsyCross.
License: MIT; see LICENSE (Copyright REDRIVER2 Project).

Only the software SPU core and its reverb/resampler dependencies are used
(src/audio/PsyX_SPUCore, PsyX_SPUReverb, PsyX_IdealReverb, PsyX_ReferenceReverb,
PsyX_ReferenceResampler) plus include/psx/{types,libspu}.h.
include/PsyX/PsyX_public.h is a rood-boi stub holding only the sound-override hook
declaration. rood-boi drives the core from pc/src/audio_spu.cpp (register-level
adapter for the game's sound driver) and runs it in its Exact renderer mode.

## rood-boi changes: clean-room reverb and noise

The reverb unit and the noise generator were rewritten for rood-boi, clean-room,
from the public hardware documentation only (psx-spx / nocash PSX specifications,
"Sound Processing Unit": "SPU Reverb Registers", "SPU Reverb Formula", "SPU Reverb
Examples", "Reverb Buffer Resampling", "SPU Noise Generator"). No emulator source
was consulted. These files replace the earlier versions, whose comments credited
code from emulators under licences incompatible with this directory's MIT licence:

- `PsyX_SPUReverb.{h,cpp}`: integer reverb unit (work-area ring addressing,
  same/different-side reflection, comb taps, two all-pass filters, 39-tap FIR
  decimation/interpolation between 44100 and 22050 Hz, master-enable write gate).
  The register sets are the documented libspu presets; their work-area sizes
  match the libspu table in the game's own executable.
- `PsyX_IdealReverb.{h,cpp}`: the same network in double precision (Ideal
  renderer); it now shares the register types and presets with PsyX_SPUReverb.
- `PsyX_SPUCore.cpp` `StepSharedNoise()`: the documented noise timer and
  parity-feedback shift register.

Behaviour is equivalent to the replaced code: on the same input, the integer
reverb's output differs by 58-68 dB below the signal for eight of the nine
presets and 39 dB for Half Echo (rounding and saturation order); the double
precision one is identical after rounding to 16 bits. In-game recordings (title,
New Game, opening with the Hall and Studio Large modes) match in level and per-band
energy within the run-to-run variation (< 0.2 dB). The input to the reverb is now
saturated to 16 bits instead of wrapped.

The remaining files (SPU core, ADPCM, Gaussian interpolation table, ADSR,
reference reverb and resampler) are as in the fork apart from comment edits.

## rood-boi changes: save states

`SPUCore::SaveState` / `LoadState` (end of `PsyX_SPUCore.cpp`) write and read
the core's Exact-renderer state as bytes for the port's save states
(`pc/src/snapshot.c`, through `pc/src/audio_spu.cpp`). The Ideal/Reference
renderers' resampler and reverb histories are not saved (LoadState resets them).

## rood-boi changes: SPU address interrupt

`SPUCore::SetIrq` / `TakeIrq` (PsyX_SPUCore.cpp, "SetIrq") model the SPU
interrupt from the psx-spx description: while enabled, a voice decoding the
ADPCM block that holds the IRQ address raises a flag that stays set until the
interrupt is disabled. Only voice reads raise it (not transfers, reverb or
capture-buffer writes). The game's sound driver needs it to stream effect
sounds through a double buffer; `pc/src/audio_libspu.c` runs the callback.
The enable, address and flags are part of the save state.
