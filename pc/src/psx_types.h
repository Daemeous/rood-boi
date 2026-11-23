/* Layout helpers and the guest memory's extents for the 64-bit (LP64) port.
 *
 * Structs that overlay PS1 data (disc files, save data) keep the PS1 layout; a pointer in a struct the game builds itself is a native
 * pointer (the 4-byte guest pointer words and their psx_ptr/psx_get/psx_set accessors are gone: no struct holds one). PSX_SIZE and
 * PSX_OFFSET give a literal the original wrote its native value, and PSX_ASSERT_SIZE/OFFSET fail the build, not memory, where a layout
 * is load-bearing:
 *
 *   PSX_ASSERT_SIZE(Node, 8);
 *   PSX_ASSERT_OFFSET(Node, x, 4);
 *   vs_main_bzero(state, PSX_SIZE(State, 0xC4));        // runtime struct
 *   p = (u8*)actor + PSX_OFFSET(Actor, position, 0x34); // byte offsets
 *
 * The header includes nothing, because src/ is preprocessed with -nostdinc against the PS1 SDK headers.
 */
#ifndef ROOD_PSX_TYPES_H
#define ROOD_PSX_TYPES_H

/* The arenas (memory lane S4, S6): the game heap and the port arena live in their own mapping, at an address we
 * choose and keep the same in every process (so snapshots and rewind restore them). Pointers into them are ordinary 64-bit
 * pointers; no 32-bit word names them (the sound driver's words are pointer-sized since T18). ROOD_ARENA_SIZE is what is mapped (pages nobody touches cost nothing). */
#define ROOD_ARENA_BASE 0x100000000ull
#define ROOD_ARENA_SIZE 0x200000
/* The game heap's arena: its used size is the original's (VS_MAIN_HEAP_SIZE); the snapshot saves this much of it. */
#define ROOD_HEAP_BASE ROOD_ARENA_BASE
#define ROOD_HEAP_SIZE 0xF2000
/* The port arena (memory lane S6): the decoded file objects live here, never in the game heap, so decoding does not
 * change the game's allocation pattern. game/common/port_arena.zig is the allocator (header, roots, size-class free
 * lists). The snapshot saves all of it as the "port" range, so its size is a deliberate setting (a snapshot only loads
 * into a build with the same range). */
#define ROOD_PORT_BASE (ROOD_ARENA_BASE + 0x100000)
#define ROOD_PORT_SIZE 0x40000
/* Whether [address, address + size) lies in the game's arenas (heap and port), as a host bridge needs. */
static inline int rood_guest_memory(__UINTPTR_TYPE__ address, __UINTPTR_TYPE__ size)
{
    __UINTPTR_TYPE__ in_arena = address - ROOD_ARENA_BASE;
    return in_arena < ROOD_ARENA_SIZE && size <= ROOD_ARENA_SIZE - in_arena;
}

/* Where the PC C reads (and clears) fields through what the original left NULL
 * (the PS1's physical address 0): 64 KiB of zeroes, defined in boot_runtime.c,
 * the same page the Zig side's psx.lowRam hands out. */
extern unsigned char pc_null_object[0x10000];

/* The SDK's long: 32 bits on the PS1. The 64-bit build reads the SDK headers
 * with long spelled int (pc/include/psx_sdk_long_begin.h), so port code that
 * defines or calls an SDK function spells the type psx_long to match the
 * prototype on every build. */
#if defined(ROOD_PC_LP64) /* only the PC passes it */
typedef int psx_long;
typedef unsigned int psx_ulong;
#else
typedef long psx_long;
typedef unsigned long psx_ulong;
#endif

#ifdef ROOD_PC

#ifdef __cplusplus
#define PSX_STATIC_ASSERT_ static_assert
#else
#define PSX_STATIC_ASSERT_ _Static_assert
#endif

/* Load-bearing PS1 layouts (data overlays): fail the build, not memory. */
#define PSX_ASSERT_SIZE(T, n)                                                  \
    PSX_STATIC_ASSERT_(sizeof(T) == (n), "PS1 layout: sizeof(" #T ") != " #n)
#define PSX_ASSERT_OFFSET(T, field, n)                                         \
    PSX_STATIC_ASSERT_(__builtin_offsetof(T, field) == (n),                    \
                       "PS1 layout: offsetof(" #T ", " #field ") != " #n)

/* A size or offset the original code wrote as a literal. Native layouts use
 * the real value; wherever pointers are 4 bytes (the 32-bit build) the
 * literal is also checked, so a wrong T or field fails to compile. The
 * result is an int, like the literal, so signed arithmetic is unchanged. */
#define PSX_CHECK_ILP32_(actual, n)                                            \
    (0 * sizeof(char[(sizeof(void*) != 4 || (actual) == (n)) ? 1 : -1]))
#define PSX_SIZE(T, n) ((int)(sizeof(T) + PSX_CHECK_ILP32_(sizeof(T), n)))
#define PSX_OFFSET(T, field, n)                                                \
    ((int)(__builtin_offsetof(T, field)                                        \
           + PSX_CHECK_ILP32_(__builtin_offsetof(T, field), n)))

#else /* PS1: the original literal, so matched objects do not change. */

/* A declaration, so the trailing semicolon stays valid at file scope. */
#define PSX_ASSERT_SIZE(T, n) extern int psx_assert_unused_
#define PSX_ASSERT_OFFSET(T, field, n) extern int psx_assert_unused_
#define PSX_SIZE(T, n) (n)
#define PSX_OFFSET(T, field, n) (n)

#endif

#endif

/* An index the data supplies for a table declared with the extent the data needs: 1 when 0 <= index < length, else it is logged once per table
 * (boot_runtime.c, with where and two numbers of context) and the caller refuses the access: no silent clamp to a wrong entry. */
int pc_table_index_ok(const char* table, long index, long length, const char* where, unsigned long first, unsigned long second);
