/* The PS1 data model for the SDK headers in the 64-bit build.
 *
 * The PS1 SDK (include/psx) declares its 32-bit words as long and u_long:
 * packet tags and OT entries, VECTOR and MATRIX, DR_ENV, CdlFILE, the SPU and
 * kernel records. LP64 makes long 8 bytes, which moves every one of those
 * layouts and every game struct that embeds them. So the 64-bit build
 * (ROOD_PC_LP64) reads the SDK headers with long spelled int: pc/include
 * comes first on the port's include path, and each wrapper there (libgpu.h,
 * libgte.h, sys/types.h, ...) brackets its include/psx header with this file
 * and psx_sdk_long_end.h. u_long and ulong become unsigned int, VECTOR.vx an
 * int, and SDK prototypes take int* where they took long*.
 *
 * Only the SDK's own declarations change. Game code's long (locals, casts and
 * struct members in src/) keeps the host meaning, so a (u_long)pointer cast
 * now truncates and -Werror=pointer-to-int-cast reports it. The PS1 build
 * never sees pc/include and the 32-bit PC build leaves long alone (it is 32
 * bits there already), so neither changes. push/pop keeps nested wrappers
 * balanced. pc/src/psx_sdk_layout.c pins the resulting layouts. A newly used
 * SDK header with long in it needs a wrapper here too. */
#if defined(ROOD_PC) && defined(ROOD_PC_LP64)
#pragma push_macro("long")
#undef long
#define long int
#endif
