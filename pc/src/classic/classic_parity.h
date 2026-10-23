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
    PC_CLASSIC_STATUS_PANEL = 13, /* the whole status display (func_800CA2DC): captured only */
    PC_CLASSIC_KINDS = 14
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
#define PC_CLASSIC_CAPTURE 160 /* packets kept per capture */
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

#ifdef __cplusplus
}
#endif
#endif
