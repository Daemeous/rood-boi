#ifndef DEV_LAYOUT_H
#define DEV_LAYOUT_H
/* The layout table in the game: the symbols, types and field offsets of this build (memory lane S2).
 *
 * pc/tools/layout_table.py writes <executable>.layout from the executable's DWARF when the build links; the first
 * RPC `field` or `symbol_info` loads it. Tools and tests name state ("vs_battle_actors[3]->unk0.facing") and the
 * game resolves the name, so a step that moves or reshapes state changes the table and nothing that asks by name.
 * The path grammar is documented in layout_table.py: SYMBOL, then .member, ->member and [index]; a pointer is
 * followed by either member form. */
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uintptr_t address;
    unsigned size;
    int type;       /* the table's type id, or 0 for a symbol without type information */
    int bitsize;    /* a bit field: its width (else 0) and its shift within the byte at address */
    int bitshift;
    size_t limit;   /* the bytes from address to the end of the symbol (a pointer's target: unknown, ~0) */
} dev_layout_ref;

/* 1 when the table is loaded (loading it on the first call); else 0 and *error says why. */
int dev_layout_load(const char** error);
/* Resolves `path`. `readable(address, length)` says whether the game may read that range (a pointer on the path is
 * read to follow it). Returns 1, or 0 with `error` filled. */
int dev_layout_resolve(const char* path, int (*readable)(uintptr_t, uintptr_t), dev_layout_ref* out, char* error,
    size_t error_size);
/* The type's kind letter ('i' signed, 'n' unsigned, 'f' float, 'b' bool, 'p' pointer, 'P' guest pointer, 's' struct,
 * 'u' union, 'a' array, 'v' void), its name (may be NULL), and the element or target. */
char dev_layout_kind(int type);
const char* dev_layout_type_name(int type);
int dev_layout_type_target(int type, unsigned* count);  /* array: element and count; pointers: the target */
/* The members of a struct or union type: index 0.. -> name (NULL: anonymous), offset, type, bits. 0 at the end. */
int dev_layout_member(int type, int index, const char** name, unsigned* offset, int* member_type, int* bitsize,
    int* bitoffset);
unsigned dev_layout_type_size(int type);
/* A value of the scalar at `ref` as a JSON number or string written to out (the bytes at ref must be readable). */
int dev_layout_format_value(const dev_layout_ref* ref, char* out, size_t size);
/* Writes integer `value` into the scalar at ref (bit fields keep their neighbours). 0 when it is not one. */
int dev_layout_store_value(const dev_layout_ref* ref, long long value);
/* Calls back for every symbol whose name starts with `prefix` (sorted by name). */
void dev_layout_each_symbol(const char* prefix, void (*each)(void* context, const char* name, uintptr_t address,
    unsigned size, int type), void* context);
#endif
