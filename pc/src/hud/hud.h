/* HUD elements for the Lua visible layer (plan
 * pc/docs/plans/2026-10-18-lua-visible-layer.md). The game decides when and
 * what; a plugin's claim decides how it looks.
 *
 *   game frame: src's PC_HUD hook (hud_hook.h) -> pc_hud_element
 *               (hud_bridge.c, battle builds: the game's record read into
 *               pc_hud_fields) -> pc_hud_record (hud_elements.cpp: a copy in
 *               a fixed pool; 1 when a claim is honoured: the bridge turns
 *               the game's packets into no-ops)
 *   presented frame: dev_overlay.cpp -> pc_hud_frame (lua_api_hud.cpp: each
 *               live element's claim draws into a fixed command buffer) ->
 *               the renderer (hud_draw.cpp, picture space)
 *   game vsync: pc_lua_tick -> held rood.dev.show moments kept, expiry
 *
 * Elements are copies, never pointers into the game. With no claim, a
 * claim that failed, or the classic look (ui_menus 0) without
 * hud_classic_override, nothing is suppressed: the game's own drawing (the
 * fallback). Every call is on the game thread. */
#ifndef ROOD_HUD_H
#define ROOD_HUD_H
#ifdef __cplusplus
extern "C" {
#endif

enum { PC_HUD_NONE = 0, PC_HUD_STATUS = 1, PC_HUD_KINDS };
#define PC_HUD_TEXT 32
#define PC_HUD_POOL 64

/* One element as the game drew it this frame. */
typedef struct {
    int kind;             /* PC_HUD_* */
    const void* key;      /* the game's record: identity only, never read through */
    char text[PC_HUD_TEXT];
    float x, y, w, h;     /* the game's drawing's box in its 320 x 240 screen: x, y its centre */
    unsigned rgb;         /* 0xRRGGBB: the game's colour (its 0x80 = full) */
    int timing;           /* status: 0 too fast, 1 fast, 2 success, 3 slow, 4 too slow */
    int variant;          /* status: which success word (COOL!, GOOD!, ...) */
    int age, life;        /* game frames since it appeared, and when it goes */
    int anchor;           /* where widescreen anchors it: 0 the centre, 1 the left edge, 2 the right */
} pc_hud_fields;

/* ---- hud_elements.cpp ---- */
/* The game drew an element (hud_bridge.c): returns 1 when the game's
 * packets are to be suppressed (a claim on its kind is honoured). */
int pc_hud_record(const pc_hud_fields* f);
/* settings hud_classic_override (project_settings.cpp): with the classic look
 * (ui_menus 0) claims are honoured only with it (0, default: typography only). */
extern int pc_hud_classic_override;
/* Whether claims on `kind` are honoured now (a claim, the look, the overlay drawing). */
int pc_hud_honoured(int kind);
/* Game vsync (pc_lua_tick): held moments kept, gone elements freed. */
void pc_hud_tick(void);

/* ---- hud_bridge.c (battle builds; absent elsewhere: weak) ---- */
/* The field is live (a room loaded, the battle overlay's HUD running). */
int pc_hud_bridge_field(void) __attribute__((weak));
/* The game's own spawner for kind (status: vs_battle_renderBattleAbilityTimingResult). */
int pc_hud_bridge_spawn(int kind, int timing, int variant) __attribute__((weak));
/* Keeps the game's element of kind at `age` (each vsync); 0 when it has none. */
int pc_hud_bridge_hold(int kind, int age) __attribute__((weak));
/* Lets it go: its last frames play out. */
void pc_hud_bridge_release(int kind) __attribute__((weak));

/* ---- lua_api_hud.cpp ---- */
/* Presented frame (dev_overlay.cpp, the ImGui frame open): claims draw the
 * live elements, then the renderer draws their commands. */
void pc_hud_frame(void);
/* RPC hud (dev_rpc.c): the params object's text; JSON in out, or < 0 with the error. */
int pc_hud_rpc(const char* params, char* out, int size);

#ifdef __cplusplus
}
#endif
#endif
