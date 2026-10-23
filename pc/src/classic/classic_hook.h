/* The classic look drawn in Lua (plan pc/docs/plans/2026-10-20-classic-ui-in-lua.md
 * §2): the game side's hook, under ROOD_PC only.
 *
 * PC_CLASSIC wraps the call that draws one of the game's classic elements:
 * the packets the statement allocates from the scratchpad's packet pointer
 * (0x1F800000), its place and the game's record go to pc_classic_site
 * (pc/src/classic/classic_bridge.c), which
 *   - keeps them as the kind's capture (RPC packets, the parity gate),
 *   - and, when a plugin's classic claim on the kind is honoured, turns them
 *     into no-ops and has the claim emit its own in the same ordering-table
 *     slot (emit-only suppression: the statement still runs, so whatever
 *     game state it steps is stepped as before).
 * Without a claim nothing changes: the game's own drawing is the fallback.
 * The kinds are classic_parity.h's PC_CLASSIC_* (the HUD layer's kinds keep
 * their numbers: hud_hook.h's PC_HUD is this hook). */
#ifndef ROOD_CLASSIC_HOOK_H
#define ROOD_CLASSIC_HOOK_H
#ifdef ROOD_PC
#define PC_CLASSIC_SITE_GAUGES 8       /* classic_parity.h PC_CLASSIC_GAUGES: func_800CA2DC's gauges */
#define PC_CLASSIC_SITE_FIGURE 9       /* Ashley's five limb sprites */
/* Snapshot at the status carousel's emit boundary; no game pointers escape. */
typedef struct {
    int previous, current, next, count, selected, offset, slide;
} pc_classic_status_icons;
#define PC_CLASSIC_SITE_STATUS_ICONS 10
typedef struct { int step, slide, pulse; } pc_classic_battle_mode;
#define PC_CLASSIC_SITE_BATTLE_MODE 11
typedef struct { int animation, offset, group, availability; } pc_classic_ability_icons;
#define PC_CLASSIC_SITE_ABILITY_ICONS 12
#define PC_CLASSIC_SITE_STATUS_PANEL 13 /* classic_parity.h PC_CLASSIC_STATUS_PANEL: func_800CA2DC */
void pc_classic_site(int kind, void* record, int x, int y, void* first, void* end) __attribute__((weak));
#define PC_CLASSIC(kind, record, x, y, statement) \
    do { \
        void* pc_classic_first = psx_load(void*, (u_int*)0x1F800000); \
        statement; \
        if (pc_classic_site) \
            pc_classic_site(kind, record, x, y, pc_classic_first, psx_load(void*, (u_int*)0x1F800000)); \
    } while (0)
#endif
#endif
