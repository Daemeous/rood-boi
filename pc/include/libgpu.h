/* include/psx/libgpu.h with the PS1's 32-bit long: see psx_sdk_long_begin.h. */
#include "psx_sdk_long_begin.h"
#include_next <libgpu.h>
#include "psx_sdk_long_end.h"

/* Ordering-table links on LP64. Links stay the PS1's 24-bit guest addresses
 * in 32-bit tags: every primitive and OT lives in guest RAM, which is mapped
 * at 0x80000000, so the low 24 bits plus the 0x80 high byte rebuild the
 * pointer (pc_draw_packet in boot_runtime.c). setaddr takes a pointer
 * (checked to be a guest address) or a link word (getaddr, 0xffffffff). */
#if defined(ROOD_PC) && defined(ROOD_PC_LP64)
#include "pc/src/psx_types.h"
#undef setaddr
#undef nextPrim
#define setaddr(p, _addr) (((P_TAG *)(p))->addr = psx_word(_addr))
#define nextPrim(p) psx_to_ptr(void *, (((P_TAG *)(p))->addr) | 0x80000000)
#endif
