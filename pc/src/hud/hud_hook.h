/* HUD elements for the Lua visible layer (pc/src/hud/hud.h; plan
 * pc/docs/plans/2026-10-18-lua-visible-layer.md), the game side's hook,
 * under ROOD_PC only. PC_HUD wraps the call that draws one of the game's HUD
 * elements: the packets the statement allocates from the scratchpad's packet
 * pointer (0x1F800000) and the game's record go to pc_hud_element
 * (hud_bridge.c), which copies the element's fields and, when a plugin's
 * claim on its kind is honoured, turns those packets into no-ops (the
 * plugin draws it instead). Without a claim nothing changes. */
#ifndef ROOD_HUD_HOOK_H
#define ROOD_HUD_HOOK_H
#ifdef ROOD_PC
enum { PC_HUD_STATUS = 1 }; /* hud.h's PC_HUD_* kinds */
void pc_hud_element(int kind, void* record, void* first, void* end) __attribute__((weak));
#define PC_HUD(kind, record, statement) \
    do { \
        void* pc_hud_first = psx_load(void*, (u_int*)0x1F800000); \
        statement; \
        if (pc_hud_element) \
            pc_hud_element(kind, record, pc_hud_first, psx_load(void*, (u_int*)0x1F800000)); \
    } while (0)
#endif
#endif
