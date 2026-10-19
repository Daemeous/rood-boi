/* HUD elements for the Lua visible layer (pc/src/hud/hud.h; plan
 * pc/docs/plans/2026-10-18-lua-visible-layer.md), the game side's hook,
 * under ROOD_PC only. PC_HUD wraps the call that draws one of the game's HUD
 * elements at (x, y): the packets the statement allocates from the
 * scratchpad's packet pointer (0x1F800000), the place and the game's record
 * go to pc_hud_element (hud_bridge.c), which copies the element's fields and,
 * when a plugin's claim on its kind is honoured, turns those packets into
 * no-ops (the plugin draws it instead). Without a claim nothing changes. */
#ifndef ROOD_HUD_HOOK_H
#define ROOD_HUD_HOOK_H
#ifdef ROOD_PC
enum { PC_HUD_STATUS = 1, PC_HUD_CHAIN, PC_HUD_BUFF, PC_HUD_DAMAGE }; /* hud.h's PC_HUD_* kinds */
void pc_hud_element(int kind, void* record, int x, int y, void* first, void* end) __attribute__((weak));
#define PC_HUD(kind, record, x, y, statement) \
    do { \
        void* pc_hud_first = psx_load(void*, (u_int*)0x1F800000); \
        statement; \
        if (pc_hud_element) \
            pc_hud_element(kind, record, x, y, pc_hud_first, psx_load(void*, (u_int*)0x1F800000)); \
    } while (0)
#endif
#endif
