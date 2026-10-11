# PsyCross geometry subset
Source: https://github.com/OpenDriver2/PsyCross
Commit: e56e4cde1c2b8a15e0d4e38b26cdd9202e0d17e6
License: MIT; see LICENSE. Copyright REDRIVER2 Project.

Only software GTE, SDK geometry wrappers, lookup tables and required headers.
The unused PsyX_render.h include in LIBGTE.C is removed; no other source changes.
Build with USE_PGXP=0 and USE_EXTENDED_PRIM_POINTERS=0. The SDK InitGeom is renamed
at compile time: rood-boi supplies the original game's different register defaults.

CompMatrixLV is also renamed at compile time: the game uses a custom two-argument,
rotation-only in-place entry rather than the SDK three-argument operation.

`include/psx/types.h` makes `u_long`/`ulong` 32-bit under `ROOD_PC_LP64`, matching the
game's SDK headers in the 64-bit build (pc/include/psx_sdk_long_begin.h). PsyCross itself
does not use them. Its `long*` GTE out-parameters (`RotTrans`, `RotTransSV`,
`RotTransPers`/`3`/`4`, `RotAverage4`, `RotAverageNclip4`) are `psx_gte_long*`
(`libgte.h`: `int` under `ROOD_PC_LP64`, else `long`), matching the game's 32-bit
SDK `long`; with host longs `*flag |= _flag` was an 8-byte store into a 4-byte slot.
