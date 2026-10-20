/* Owned game code (game/, Zig; pc/docs/Ownership.md): the host side.
 * This part: the owned objects' panic handler and the ABI check. */
#include "ownership.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void pc_unimplemented(const char* what) __attribute__((noreturn));
extern const uint32_t rood_game_abi; /* game/core/root.zig */
#define ROOD_GAME_ABI 1
#ifndef _WIN32
extern int pc_shadow_panic(const char*, size_t) __attribute__((weak));
#endif

/* game/common/panic.zig: a safety check failed in owned code (overflow,
 * bounds, unreachable, a null unwrap). */
__attribute__((noreturn)) void pc_zig_panic(const char* msg, size_t len, uintptr_t return_address)
{
#ifndef _WIN32
    if (pc_shadow_panic)
        pc_shadow_panic(msg, len); /* returns only outside the owned shadow callback */
#endif
    fprintf(stderr, "[zig] panic: %.*s (return address %#llx)\n", (int)len, msg,
        (unsigned long long)return_address);
    fflush(stderr);
    pc_unimplemented("owned code panicked (see the [zig] line)");
}

#ifndef ROOD_NO_OWNED /* built without Zig (build_boot.py): no owned objects yet */
__attribute__((constructor)) static void check_abi(void)
{
    if (rood_game_abi != ROOD_GAME_ABI) {
        fprintf(stderr, "[ownership] game_core ABI %u, the host expects %u\n", rood_game_abi, ROOD_GAME_ABI);
        abort();
    }
}
#endif

/* Host state, deliberately outside snapshots. Task 9 supplies routing.c. */
#include <string.h>
#ifdef ROOD_NO_ROUTING
static const unsigned pc_route_count = 0;
static const unsigned char pc_route_subsystems[] = { 0 };
static const unsigned pc_subsystem_count = 0;
static const char* const pc_subsystem_names[] = { NULL };
static const unsigned char pc_subsystem_defaults[] = { 0 };
#else
extern const unsigned pc_route_count;
extern const unsigned char pc_route_subsystems[];
extern const unsigned pc_subsystem_count;
extern const char* const pc_subsystem_names[];
extern const unsigned char pc_subsystem_defaults[];
#endif
#ifndef _WIN32
extern int pc_shadow_available(void) __attribute__((weak));
#endif

unsigned char pc_owner[64];
static const char* const owner_labels[] = { "original", "owned", "shadow" };

int pc_owner_routes(int subsystem)
{
    int count = 0;
    for (unsigned r = 0; r < pc_route_count; r++)
        count += pc_route_subsystems[r] == subsystem;
    return count;
}
int pc_owner_count(void) { return (int)pc_subsystem_count; }
const char* pc_owner_subsystem(int i) { return i >= 0 && i < (int)pc_subsystem_count ? pc_subsystem_names[i] : NULL; }
const char* pc_owner_label(int owner) { return owner >= 0 && owner <= 2 ? owner_labels[owner] : "?"; }
int pc_owner_get(int i) { return i >= 0 && i < (int)pc_subsystem_count ? pc_owner[i] : -1; }
int pc_owner_find(const char* name)
{
    for (unsigned i = 0; name && i < pc_subsystem_count; i++)
        if (!strcmp(pc_subsystem_names[i], name))
            return (int)i;
    return -1;
}
int pc_owner_parse(const char* label)
{
    for (int o = 0; label && o < 3; o++)
        if (!strcmp(owner_labels[o], label))
            return o;
    return -1;
}
/* Called between frames: 0 set, -1 invalid, -2 shadow unavailable. */
int pc_owner_set(int i, int owner)
{
    if (i < 0 || i >= (int)pc_subsystem_count || owner < 0 || owner > 2)
        return -1;
    if (owner == 2) {
#ifdef _WIN32
        return -2;
#else
        if (!(pc_shadow_available && pc_shadow_available()))
            return -2;
#endif
    }
    if (pc_owner[i] != owner)
        fprintf(stderr, "[ownership] %s: %s\n", pc_subsystem_names[i], owner_labels[owner]);
    pc_owner[i] = (unsigned char)owner;
    return 0;
}

/* ROOD_OWNER=battle_action=owned,menus=original, applied over defaults. */
__attribute__((constructor)) static void owner_defaults(void)
{
    if (pc_subsystem_count > sizeof pc_owner)
        abort();
    for (unsigned i = 0; i < pc_subsystem_count; i++)
        pc_owner[i] = pc_subsystem_defaults[i];
    const char* env = getenv("ROOD_OWNER");
    char item[96];
    while (env && *env) {
        size_t len = strcspn(env, ",");
        if (len >= sizeof item) {
            fprintf(stderr, "[ownership] ROOD_OWNER: ignored oversized item\n");
            env += len + (env[len] == ',');
            continue;
        }
        snprintf(item, sizeof item, "%.*s", (int)len, env);
        env += len + (env[len] == ',');
        char* eq = strchr(item, '=');
        int i = eq ? (*eq = 0, pc_owner_find(item)) : -1;
        int owner = eq ? pc_owner_parse(eq + 1) : -1;
        if (pc_owner_set(i, owner))
            fprintf(stderr, "[ownership] ROOD_OWNER: ignored %s\n", item);
    }
}
