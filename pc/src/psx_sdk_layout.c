/* PS1 SDK layouts the port depends on, checked at compile time.
 *
 * GPU packets and OT entries are built in guest memory and walked by the
 * GPU emulation by byte offset; VECTOR/MATRIX/DRAWENV and the CD, SPU and
 * kernel records are embedded in game structs that overlay PS1 data. All of
 * them must keep the PS1 size and offsets on every PC target, so a header
 * change that moves one fails the build rather than memory.
 *
 * build_boot.py compiles this file with the game's include path and flags
 * for both --arch 32 and --arch 64 (the LP64 build reads the SDK headers
 * with a 32-bit long: pc/include/psx_sdk_long_begin.h); a failure is fatal
 * in both. Types with pointer members (TIM_IMAGE, TMD_PRIM, DIVPOLYGON*,
 * ENCSPUENV, TCBH, ToT, EvCB, DIRENTRY) are left to the phase 1 retypes.
 * libcd.h and libds.h both define StHEADER, so the libds records are
 * checked in a second pass with PSX_SDK_LAYOUT_LIBDS. */
#include <sys/types.h>
#ifdef PSX_SDK_LAYOUT_LIBDS
#include <libds.h>
#else
#include <libgte.h> /* before libgpu.h, which uses SVECTOR */
#include <libapi.h>
#include <libcd.h>
#include <libetc.h>
#include <libgpu.h>
#include <libpress.h>
#include <libsnd.h>
#include <libspu.h>
#endif
#include "pc/src/psx_types.h"

#ifdef PSX_SDK_LAYOUT_LIBDS

PSX_ASSERT_SIZE(DslLOC, 4);
PSX_ASSERT_SIZE(DslFILTER, 4);
PSX_ASSERT_SIZE(DslATV, 4);
PSX_ASSERT_SIZE(DslFILE, 24);
PSX_ASSERT_OFFSET(DslFILE, size, 4);
PSX_ASSERT_OFFSET(DslFILE, name, 8);
PSX_ASSERT_SIZE(StHEADER, 32);

#else

/* SDK words. */
PSX_ASSERT_SIZE(u_long, 4);
PSX_ASSERT_SIZE(u_int, 4);
PSX_ASSERT_SIZE(u_short, 2);

/* Ordering table entries and packet headers: a 24-bit link and a length. */
PSX_ASSERT_SIZE(P_TAG, 8);
PSX_ASSERT_SIZE(P_CODE, 4);

/* Polygons. */
PSX_ASSERT_SIZE(POLY_F3, 20);
PSX_ASSERT_SIZE(POLY_F4, 24);
PSX_ASSERT_SIZE(POLY_FT3, 32);
PSX_ASSERT_SIZE(POLY_FT4, 40);
PSX_ASSERT_OFFSET(POLY_FT4, code, 7);
PSX_ASSERT_OFFSET(POLY_FT4, x0, 8);
PSX_ASSERT_OFFSET(POLY_FT4, u0, 12);
PSX_ASSERT_OFFSET(POLY_FT4, clut, 14);
PSX_ASSERT_OFFSET(POLY_FT4, tpage, 22);
PSX_ASSERT_OFFSET(POLY_FT4, x3, 32);
PSX_ASSERT_SIZE(POLY_G3, 28);
PSX_ASSERT_SIZE(POLY_G4, 36);
PSX_ASSERT_SIZE(POLY_GT3, 40);
PSX_ASSERT_SIZE(POLY_GT4, 52);
PSX_ASSERT_OFFSET(POLY_GT4, clut, 14);
PSX_ASSERT_OFFSET(POLY_GT4, tpage, 26);
PSX_ASSERT_OFFSET(POLY_GT4, r3, 40);
PSX_ASSERT_OFFSET(POLY_GT4, u3, 48);

/* Lines. */
PSX_ASSERT_SIZE(LINE_F2, 16);
PSX_ASSERT_SIZE(LINE_G2, 20);
PSX_ASSERT_SIZE(LINE_F3, 24);
PSX_ASSERT_SIZE(LINE_G3, 32);
PSX_ASSERT_SIZE(LINE_F4, 28);
PSX_ASSERT_SIZE(LINE_G4, 40);

/* Sprites and tiles. */
PSX_ASSERT_SIZE(SPRT, 20);
PSX_ASSERT_OFFSET(SPRT, clut, 14);
PSX_ASSERT_OFFSET(SPRT, w, 16);
PSX_ASSERT_SIZE(SPRT_16, 16);
PSX_ASSERT_SIZE(SPRT_8, 16);
PSX_ASSERT_SIZE(TILE, 16);
PSX_ASSERT_OFFSET(TILE, w, 12);
PSX_ASSERT_SIZE(TILE_16, 12);
PSX_ASSERT_SIZE(TILE_8, 12);
PSX_ASSERT_SIZE(TILE_1, 12);

/* Drawing-environment packets. */
PSX_ASSERT_SIZE(DR_MODE, 12);
PSX_ASSERT_SIZE(DR_TWIN, 12);
PSX_ASSERT_SIZE(DR_AREA, 12);
PSX_ASSERT_SIZE(DR_OFFSET, 12);
PSX_ASSERT_SIZE(DR_MOVE, 24);
PSX_ASSERT_SIZE(DR_LOAD, 68);
PSX_ASSERT_OFFSET(DR_LOAD, p, 16);
PSX_ASSERT_SIZE(DR_TPAGE, 8);
PSX_ASSERT_SIZE(DR_STP, 12);
PSX_ASSERT_SIZE(DR_ENV, 64);
PSX_ASSERT_OFFSET(DR_ENV, code, 4);

/* Environments. */
PSX_ASSERT_SIZE(RECT, 8);
PSX_ASSERT_SIZE(RECT32, 16);
PSX_ASSERT_SIZE(DRAWENV, 92);
PSX_ASSERT_OFFSET(DRAWENV, tw, 12);
PSX_ASSERT_OFFSET(DRAWENV, tpage, 20);
PSX_ASSERT_OFFSET(DRAWENV, isbg, 24);
PSX_ASSERT_OFFSET(DRAWENV, dr_env, 28);
PSX_ASSERT_SIZE(DISPENV, 20);
PSX_ASSERT_OFFSET(DISPENV, screen, 8);
PSX_ASSERT_OFFSET(DISPENV, isinter, 16);

/* Geometry. */
PSX_ASSERT_SIZE(MATRIX, 32);
PSX_ASSERT_OFFSET(MATRIX, t, 20);
PSX_ASSERT_SIZE(VECTOR, 16);
PSX_ASSERT_OFFSET(VECTOR, vz, 8);
PSX_ASSERT_SIZE(SVECTOR, 8);
PSX_ASSERT_SIZE(CVECTOR, 4);
PSX_ASSERT_SIZE(DVECTOR, 4);
PSX_ASSERT_SIZE(EVECTOR, 44);
PSX_ASSERT_SIZE(RVECTOR, 24);
PSX_ASSERT_SIZE(SPOL, 16);
PSX_ASSERT_SIZE(POL3, 56);
PSX_ASSERT_SIZE(POL4, 74);

/* CD-ROM. */
PSX_ASSERT_SIZE(CdlLOC, 4);
PSX_ASSERT_SIZE(CdlFILTER, 4);
PSX_ASSERT_SIZE(CdlATV, 4);
PSX_ASSERT_SIZE(CdlFILE, 24);
PSX_ASSERT_OFFSET(CdlFILE, size, 4);
PSX_ASSERT_OFFSET(CdlFILE, name, 8);
PSX_ASSERT_SIZE(StHEADER, 32);

/* SPU and sound. */
PSX_ASSERT_SIZE(SpuVolume, 4);
PSX_ASSERT_SIZE(SpuVoiceAttr, 64);
PSX_ASSERT_SIZE(SpuLVoiceAttr, 68);
PSX_ASSERT_SIZE(SpuReverbAttr, 20);
PSX_ASSERT_SIZE(SpuExtAttr, 12);
PSX_ASSERT_SIZE(SpuCommonAttr, 40);
PSX_ASSERT_SIZE(SpuEnv, 8);
PSX_ASSERT_SIZE(SpuStVoiceAttr, 16);
PSX_ASSERT_SIZE(SpuStEnv, 392);
PSX_ASSERT_SIZE(VabHdr, 32);
PSX_ASSERT_SIZE(ProgAtr, 16);
PSX_ASSERT_SIZE(VagAtr, 32);

/* MDEC and kernel. */
PSX_ASSERT_SIZE(DECDCTENV, 256);
PSX_ASSERT_SIZE(struct EXEC, 60);
PSX_ASSERT_SIZE(struct XF_HDR, 136);
PSX_ASSERT_SIZE(struct TCB, 192);

#endif
