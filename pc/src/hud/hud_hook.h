/* HUD elements for the Lua visible layer (pc/src/hud/hud.h; plan
 * pc/docs/plans/2026-10-18-lua-visible-layer.md), the game side's hook,
 * under ROOD_PC only. PC_HUD wraps the call that draws one of the game's HUD
 * elements at (x, y): it is the classic layer's hook (pc/src/classic/classic_hook.h,
 * PC_CLASSIC): the packets the statement allocates from the scratchpad's packet
 * pointer (0x1F800000), the place and the game's record go to pc_classic_site, which
 * hands the HUD kinds to pc_hud_element (hud_bridge.c): it copies the element's
 * fields and, when a plugin's claim on its kind is honoured, turns those packets
 * into no-ops (the plugin draws it instead). Without a claim nothing changes. */
#ifndef ROOD_HUD_HOOK_H
#define ROOD_HUD_HOOK_H
#ifdef ROOD_PC
#include "../classic/classic_hook.h"
enum { PC_HUD_STATUS = 1, PC_HUD_CHAIN, PC_HUD_BUFF, PC_HUD_DAMAGE }; /* hud.h's PC_HUD_* kinds */
#define PC_HUD(kind, record, x, y, statement) PC_CLASSIC(kind, record, x, y, statement)
#endif
#endif
