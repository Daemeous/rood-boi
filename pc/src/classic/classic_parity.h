/* The classic look drawn in Lua: packet capture and the parity gate (plan
 * pc/docs/plans/2026-10-20-classic-ui-in-lua.md, §3). Pure C: no game
 * headers, so the walker and the comparison are probed on synthetic packets
 * (pc/tests/classic_parity_probe.c).
 *
 *   capture   a site's packets (the PC_CLASSIC hook's range, classic_hook.h)
 *             walked in ordering-table order from its slots' heads, each
 *             kept as its payload words (the link dropped) and the
 *             widescreen anchor its range has: RPC packets
 *   parity    with the parity mode on (RPC classic {parity}, or
 *             ROOD_CLASSIC_PARITY=1; dev mode only) the game's own drawing
 *             stays live and the Lua classic draw of the same element runs
 *             into a shadow slot whose packets are never drawn; both are
 *             normalised and compared, and each kind's counters (frames,
 *             mismatches by field, the first difference) are kept: RPC
 *             classic {parity}
 *
 * Normalised, a packet is its words after the tag (code, colours, vertices,
 * texels, CLUT and page, sizes, draw-mode settings: no addresses) and its
 * anchor, in drawing order. Zero-length packets (the text printers' tails,
 * packets a claim turned into no-ops) are left out. Text needs nothing more:
 * a glyph is a sprite whose texels and CLUT are its code, before ui_text,
 * which restyles at raster time and never changes a packet. */
#ifndef PC_CLASSIC_PARITY_H
#define PC_CLASSIC_PARITY_H
#ifdef __cplusplus
extern "C" {
#endif

/* The classic kinds: the HUD layer's (hud.h PC_HUD_*, same numbers) and the
 * sites only the classic layer has. */
enum {
    PC_CLASSIC_NONE = 0,
    PC_CLASSIC_STATUS = 1, /* the timing word (BATTLE.PRG func_800958A4) */
    PC_CLASSIC_CHAIN = 2,  /* 3 CHAINS (func_80095A4C) */
    PC_CLASSIC_BUFF = 3,   /* WEAPON HUMAN +1 (func_8009695C) */
    PC_CLASSIC_DAMAGE = 4, /* a number over an actor (func_80094B0C) */
    PC_CLASSIC_GAUGES = 8, /* the HP / MP / RISK gauges, digits, labels or RISK ZERO inside the panel (hud.h PC_HUD_GAUGES) */
    PC_CLASSIC_FIGURE = 9, /* Ashley's five limb sprites (hud.h PC_HUD_FIGURE) */
    PC_CLASSIC_STATUS_ICONS = 10, /* status carousel and count indicators */
    PC_CLASSIC_BATTLE_MODE = 11, /* sliding BATTLE MODE notice */
    PC_CLASSIC_ABILITY_ICONS = 12, /* ability label groups, cross and face buttons */
    PC_CLASSIC_ACTOR_WORD = 13, /* MISS and the combat labels over the actors (func_8009506C, func_80095258) */
    PC_CLASSIC_TIMER = 14, /* the time attack's timer (5BF94.c _renderTimer) */
    PC_CLASSIC_EMOTE = 15, /* the emote balloons over the actors (2842C.c func_80095C18, func_8009651C) */
    PC_CLASSIC_TEXT_FRAME = 16, /* a text box's frame: the talk bubbles' and the info box's (func_800CD3E4) */
    PC_CLASSIC_TEXT_BAND = 17, /* a text box's band: the name window's (func_800CD158) */
    PC_CLASSIC_BOX_TEXT = 18, /* a text box's text as _printVariableWidthFont sets it (its glyphs) */
    PC_CLASSIC_IMAGE = 19, /* a portrait or still (func_800CCA90) */
    PC_CLASSIC_TARGET_ROW = 20, /* a target row: a part's name and its plate (58578.c func_800C1DC4) */
    PC_CLASSIC_TARGET_LINE = 21, /* a selected row's leader lines (func_800C16FC, func_800C1A40) */
    PC_CLASSIC_TARGET_MARKER = 22, /* the area target's marker (func_800C28AC) */
    PC_CLASSIC_MENU_BACKGROUND = 23, /* MAINMENU's background (58EC.c _renderBackground) */
    PC_CLASSIC_MENU_COMMAND = 24, /* the command title's sprite (_renderMenuCommand) */
    PC_CLASSIC_MENU_ROW = 25, /* a menu row (_renderMenuItems' emit: its window, label, counts, icons) */
    PC_CLASSIC_MENU_CURSOR = 26, /* the pointing hand (vs_mainMenu_renderCursor) */
    PC_CLASSIC_MENU_TABS = 27, /* the tab arrows, L1 / R1 (vs_mainMenu_renderTabNavigation) */
    PC_CLASSIC_OPTION_VALUE = 28, /* a row's value in our Option pages (classic_options.c draw_values) */
    PC_CLASSIC_ITEMS_CATEGORY = 29, /* Items' category icons (MENU3 items0.h _renderItemCategory) */
    PC_CLASSIC_ITEMS_STATUS_BUTTON = 30, /* Items' STATUS strip (MENU3 16C.c: its button, window and word) */
    PC_CLASSIC_EQUIP_STATS = 31, /* the equipment stats panel (MAINMENU C48.c vs_mainMenu_renderEquipStats) */
    PC_CLASSIC_DP_PP_BARS = 32, /* the DP / PP bars (C48.c vs_mainMenu_renderDpPpBars) */
    PC_CLASSIC_GEM_SLOTS = 33, /* a grip's gem slots (MENU3 items1.h _gripStatusView) */
    PC_CLASSIC_STAT_PAGE = 34, /* the class / affinity / type table (C48.c _renderStatPage) */
    PC_CLASSIC_OK_MARK = 35, /* the pulsing OK beside a menu's last choice (MENUC _drawOk, MENUD's top menu) */
    PC_CLASSIC_STORAGE_BUTTONS = 36, /* Storage's button strip (MENUD vs_menuD_exec) */
    PC_CLASSIC_STORAGE_COUNTS = 37, /* Storage's category counts (MENUD func_80106464's callers) */
    PC_CLASSIC_LOOT_CAPACITIES = 38, /* Loot's capacities strip (MENUB _displayInventoryCapacities) */
    PC_CLASSIC_DISCARD_COUNT = 39, /* a misc item's discard amount (items1.h _discardMenu, MENUD's copy) */
    PC_CLASSIC_RENAME_GRID = 40, /* the rename grid (MENU8 _renderRenameMenu) */
    PC_CLASSIC_RENAME_BOX = 41, /* a highlight box on it (MENU8 _highlightSelection) */
    PC_CLASSIC_FILE_MENU = 42, /* MENU7's file menu's frame (_drawFileMenu: drawn at once, PC_CLASSIC_IMMEDIATE) */
    PC_CLASSIC_FILE_ELEMENT = 43, /* a card's or a file's panel in it (_drawFileMenuElement: at once) */
    PC_CLASSIC_MENUF = 44, /* MENUF's screens (3B8.c: their sprite leaves, draw areas and prompt; currentMenu 15) */
    PC_CLASSIC_MENUF_LOAD = 45, /* MENUF's VRAM uploads (its TIMs, the pulsed CLUT row: at once) */
    PC_CLASSIC_STATUS_PANEL = 46, /* the whole status display (func_800CA2DC): captured only */
    PC_CLASSIC_KINDS = 47
};
const char* pc_classic_kind_name(int kind); /* "status", ...; "" for none */
int pc_classic_kind_of(const char* name);   /* 0 unknown */

#define PC_CLASSIC_PACKET_WORDS 16 /* a GP0 packet's payload: at most 15 words (+ the code) */
typedef struct {
    unsigned char words;  /* payload words */
    signed char anchor;   /* widescreen: 0 centred, 1 left edge, 2 right edge, 3 fill, ... (gpu_hw.c) */
    unsigned short at;    /* its place in allocation order inside the range (diagnostics) */
    unsigned w[PC_CLASSIC_PACKET_WORDS];
} pc_classic_packet;

/* How the walker reads guest memory: a link (24 bits) as a pointer to its
 * packet's tag word (NULL: the end), and a packet's anchor. */
typedef struct {
    const unsigned* (*resolve)(unsigned link, void* user);
    int (*anchor)(const unsigned* tag, void* user);
    void* user;
} pc_classic_walker;
/* The game's: links are guest addresses (| 0x80000000; the host maps them
 * there), anchors from gpu_hw.c's pending ranges (pc_hw_anchor_of). */
pc_classic_walker pc_classic_guest(void); /* (made at the call: no host data points at the
                                              * engine library's functions, which reload in place) */

/* The packets of [lo, hi) linked from the slot `head` (a tag word: its link
 * is the first packet), in drawing order: followed while they lie in the
 * range (the site's packets are linked at the slot's head, so the first one
 * outside it ends them). Returns how many went into out (at most max). */
int pc_classic_walk(const pc_classic_walker* walker, const unsigned* head, const void* lo, const void* hi,
    pc_classic_packet* out, int max);
/* The packets of [lo, hi) in allocation order (each packet's length after
 * the next): RPC packets {range} and the hooks' emit-only suppression. */
int pc_classic_scan(const pc_classic_walker* walker, const void* lo, const void* hi, pc_classic_packet* out, int max);
/* The packets of [lo, hi) turned into no-ops (their length 0: the links stay,
 * the GPU skips them), packets that only set draw modes (GP0 E1-E6) kept, so
 * nothing drawn after them changes (a packet of a draw mode and a sprite, as
 * the game's sprites are, draws: it goes); how many. */
int pc_classic_suppress(void* lo, void* hi);
/* The same for the packets of [lo, hi) linked from an ordering-table slot (head), by the links: what the
 * allocation walk cannot reach past a packet shorter than its allocation. */
int pc_classic_suppress_linked(const unsigned* head, void* lo, void* hi);

/* A word's field ("code", "colour", "xy", "uv", "clut", "tpage", "wh",
 * "mode", "word") by the packet's command; fields for waivers. */
enum {
    PC_CLASSIC_FIELD_COUNT, /* the number of packets */
    PC_CLASSIC_FIELD_LENGTH,
    PC_CLASSIC_FIELD_CODE,
    PC_CLASSIC_FIELD_COLOUR,
    PC_CLASSIC_FIELD_XY,
    PC_CLASSIC_FIELD_UV,
    PC_CLASSIC_FIELD_CLUT,
    PC_CLASSIC_FIELD_TPAGE,
    PC_CLASSIC_FIELD_WH,
    PC_CLASSIC_FIELD_MODE,
    PC_CLASSIC_FIELD_ANCHOR,
    PC_CLASSIC_FIELD_WORD,
    PC_CLASSIC_FIELDS
};
const char* pc_classic_field_name(int field);
int pc_classic_field(const pc_classic_packet* p, int word);

typedef struct {
    int index;    /* the first differing packet (in drawing order); -1: none */
    int field;    /* PC_CLASSIC_FIELD_* */
    int word;     /* its word (-1: count, length, anchor) */
    unsigned game, lua;
    unsigned fields; /* every field that differs anywhere (bit per PC_CLASSIC_FIELD_*) */
} pc_classic_diff;
/* 0 when the two lists are the same, else 1 and the first difference. */
int pc_classic_compare(const pc_classic_packet* game, int game_count, const pc_classic_packet* lua, int lua_count,
    pc_classic_diff* diff);

/* JSON of a packet list: [{"code":"0x64","words":[...],"anchor":1,"kind":"sprite","xy":[[x,y],..],
 * "uv":..,"clut":[x,y],"tpage":[x,y,bits],"wh":[w,h],"colour":"#rrggbb","semi":bool,"glyph":"S"}]; the
 * length written (at most size - 1). */
int pc_classic_packets_json(const pc_classic_packet* p, int count, char* out, int size);

/* ---- capture and parity (game thread) ---- */
#define PC_CLASSIC_CAPTURE 512 /* packets kept per capture (the rename grid: about 250; MENUF's congratulations about 270) */
/* Sites capture only once asked (RPC packets turns it on) or in parity. */
int pc_classic_capturing(void);
void pc_classic_capture_on(void);
/* A site drew (the game's packets, live): kept as the kind's last capture. */
void pc_classic_capture_game(int kind, const pc_classic_packet* p, int count, unsigned vsync, const char* text);
/* How many packets the site allocated in all (a walk that reached fewer missed a slot). */
void pc_classic_capture_allocated(int kind, int count);
/* The parity mode: settings-free, dev mode only (ROOD_RPC set or ROOD_DEV=1).
 * pc_classic_parity_on: the mode is on (ROOD_CLASSIC_PARITY=1 at start, RPC). */
int pc_classic_parity_on(void);
void pc_classic_parity_set(int on);
/* The Lua draw of the element just captured, normalised: compared with the
 * game's capture of the same kind (the same frame). */
void pc_classic_parity_compare(int kind, const pc_classic_packet* lua, int count, unsigned vsync);
/* The Lua draw could not run in parity (no classic claim, it failed): counted. */
void pc_classic_parity_skip(int kind, const char* why);
void pc_classic_parity_reset(void);
/* RPC: packets {kind}: the last capture (and in parity the Lua one) as JSON;
 * classic {parity: true|false, reset}: the counters. The length, or < 0
 * with the error in out. */
int pc_classic_packets_rpc(const char* kind, char* out, int size);
int pc_classic_parity_json(char* out, int size);

/* ---- immediate draws (classic_hook.h PC_CLASSIC_IMMEDIATE: DrawPrim / LoadImage at once, no ordering table) ----
 * A range's capture is every packet it draws (its words after the tag, as an ordering-table packet's) and every
 * VRAM transfer, in the order made: an upload as {0xA0000000, y << 16 | x, h << 16 | w, a hash of its pixels}, a
 * read as {0xC0000000, y << 16 | x, h << 16 | w} (GP0's own copy commands), compared as packets are. */
/* A range opens (one at a time); hold: the game's draws and uploads are kept back (not done) until it closes. */
void pc_classic_immediate_open(int hold);
/* boot_sdk.c's DrawPrim, LoadImage, StoreImage: recorded while a range is open; 1 when the call was held back
 * (not to be done now). A read is always done (the game reads it). */
int pc_classic_immediate_draw(const void* packet);
int pc_classic_immediate_load(int x, int y, int w, int h, const void* pixels);
void pc_classic_immediate_store(int x, int y, int w, int h);
/* The range closes: its capture (*out, the count). Then pc_classic_immediate_release: the held draws done now
 * (replay 1: no claim drew it) or dropped (0). */
int pc_classic_immediate_close(const pc_classic_packet** out);
void pc_classic_immediate_release(int replay);
/* Whether the range held everything back (0: it was not asked to, or too much: done at once from there on; a claim
 * may draw instead only when it did). */
int pc_classic_immediate_held(void);
/* The normalised entries (for the Lua side's list): an upload's, a read's. */
void pc_classic_immediate_upload_entry(pc_classic_packet* out, int x, int y, int w, int h, const void* pixels);
void pc_classic_immediate_read_entry(pc_classic_packet* out, int x, int y, int w, int h);
/* The open range keeps its uploads' pixels (MENUF's: a claim uploads them with d.image_load's handle), and the
 * range's upload i (0..: in order; its rectangle, its pixels kept, else NULL), after it closed, until the next opens. */
void pc_classic_immediate_keep(void);
/* The arena a claim's immediate draw is made in (its packets, at once; 32-bit words): classic_parity.c's own. */
#define PC_CLASSIC_IMMEDIATE_ARENA_WORDS (PC_CLASSIC_CAPTURE * 64)
void* pc_classic_immediate_arena(void);
int pc_classic_immediate_uploads(void); /* the uploads the range recorded */
const void* pc_classic_immediate_upload(int i, int* x, int* y, int* w, int* h);
/* MENUF's screens (3B8.c: the congratulations, the time attack's start and end, the cube puzzle's start, end and quit;
 * currentMenu 15), drawn through the ordering table by a few leaves (_renderTextureFadeIn and its kin, the stat
 * wheel's lines and draw areas, the PRESS BUTTON prompt). A frame's calls in the order made, each as the leaf was
 * called (before its own tests): op (PC_CLASSIC_MENUF_*), the _disMap entry it names (tex, and its texels u, v, w, h,
 * tpage, clut as C reads them), the place, and a / b and the two colour words (P_CODE: r, g, b, code) by op:
 *   FADE (_renderTextureFadeIn) a the brightness; FADE_CLUT (_renderTextureFadeInClut) a the brightness, b the CLUT;
 *   TINT (_renderTextureFadeInTint), POP (_renderTexturePopIn) the colours; WIPE (_renderTextureWipe) the colours,
 *   a the wipe's edge; WHEEL (_renderAnimatedStatWheel) the entry's lines; AREA (the stat wheel's DR_AREA) x, y, a w,
 *   b h; PROMPT (_renderPressButtonPrompt) a its slide step, b the blink timer, its words (the game's) in prompt. */
enum {
    PC_CLASSIC_MENUF_FADE = 1,
    PC_CLASSIC_MENUF_FADE_CLUT,
    PC_CLASSIC_MENUF_TINT,
    PC_CLASSIC_MENUF_POP,
    PC_CLASSIC_MENUF_WIPE,
    PC_CLASSIC_MENUF_WHEEL,
    PC_CLASSIC_MENUF_AREA,
    PC_CLASSIC_MENUF_PROMPT
};
typedef struct {
    unsigned char op, tex;
    short u, v, w, h;
    unsigned short tpage, clut;
    short x, y;
    int a, b;
    unsigned colour[2];
} pc_classic_menuf_call;
#define PC_CLASSIC_MENUF_CALLS 128
#define PC_CLASSIC_MENUF_ENDING 16 /* (a screen: ENDING's, past MENUF's submenus 0..6) */
typedef struct {
    int screen, count; /* the submenu (menuFSubmenu; PC_CLASSIC_MENUF_ENDING: ENDING's MISSION COMPLETE screen, the
                        * same leaves: D4.c); count -1: more calls than the record holds */
    unsigned char prompt[2][8];
    pc_classic_menuf_call call[PC_CLASSIC_MENUF_CALLS];
} pc_classic_menuf;
/* MENUF's calls this frame (hud_bridge.c's copy of the game's record), kept out of the save state's sections. */
pc_classic_menuf* pc_classic_menuf_calls(void);
/* The classic draws' own VRAM buffer (d.image_store / d.image_restore): its 1024 pixels and rect (x, y, w, h). */
unsigned short* pc_classic_image_buffer(int** rect);

#ifdef __cplusplus
}
#endif
#endif
