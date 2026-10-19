/* Fast travel between Circles of Magic (pc/docs/Fast_Travel.md): the game's
 * own Teleportation (MENU0 _teleportMenu, BATTLE func_800735F8 case 10,
 * func_8007C694, the door's room load), offered from the first Circle once the
 * prologue is over (setting qol_fast_travel) instead of only once the game has
 * set vs_main_stateFlags.unkB5. When the game itself has set it, everything is
 * the game's.
 *
 * fast_travel_rules.c: the rules, plain C with no game headers
 * (pc/tests/fast_travel_probe.c). fast_travel.c (battle builds) fills
 * pc_ft_state from the game and is what the src hooks call (MENU0 15AC.c
 * includes this header under ROOD_PC). Nothing writes unkB5, menuFlags,
 * savePointStates or magicUsed. */
#ifndef ROOD_FAST_TRAVEL_H
#define ROOD_FAST_TRAVEL_H
#ifdef __cplusplus
extern "C" {
#endif

#define PC_FT_POINTS 48       /* savePointStates[48], MENU0's tables */
#define PC_FT_USED 35         /* the entries with a location */
#define PC_SHORTCUT_TRAVEL 9  /* vs_battle_shortcutInvoked of the Travel button (the game's run 1..8) */
#define PC_FT_MAGIC_MASK 0xB7 /* D_800F4EA0 bits that grey Magic and every destination (MENU0) */
#define PC_FT_NO_MP 0x20      /* ... MP 0 (func_800CABE0) */
#define PC_FT_HERE 15         /* _getTeleportCost of the Circle Ashley stands on */
#define PC_FT_COST_MAX 4095   /* the packed cost's 12 bits (executeAbility.fields.unk0_19) */

enum { PC_FT_COST_FREE, PC_FT_COST_FLAT, PC_FT_COST_SCALED, PC_FT_COST_GAME, PC_FT_COST_MODES };
enum { PC_FT_OFF, PC_FT_PORT, PC_FT_GAME }; /* whose unlock is in effect */

extern const int pc_ft_save_point_data[PC_FT_POINTS];   /* MENU0 84.c _savePointData */
extern const char pc_ft_save_point_costs[PC_FT_POINTS]; /* _getTeleportCost's savePointCosts */
extern const char* const pc_ft_cost_names[PC_FT_COST_MODES]; /* "free", "flat", "scaled", "game" */

/* Settings (project_settings.cpp): qol_fast_travel 0/1 (1), qol_fast_travel_cost
 * PC_FT_COST_* (free), _flat MP (15), _scale (0.5); the prompt: on (1), where
 * (an anchor and offsets in fractions of the picture), how big (1), and
 * qol_fast_travel_prompt_hold: vsyncs it stays after the field's last poll
 * offered travel (8: a field frame is 2 or 3; a slow room's more). */
extern int pc_qol_fast_travel, pc_qol_fast_travel_cost, pc_qol_fast_travel_flat, pc_qol_fast_travel_prompt,
    pc_qol_fast_travel_prompt_anchor, pc_qol_fast_travel_prompt_hold;
extern float pc_qol_fast_travel_scale, pc_qol_fast_travel_prompt_offset_x, pc_qol_fast_travel_prompt_offset_y,
    pc_qol_fast_travel_prompt_scale;
/* The prompt's look (project_settings.cpp: qol_fast_travel_prompt_backdrop,
 * _size, _stroke, _stretch, _pad_x, _pad_y, _border, _color, _stroke_color),
 * and the button prompts' icon / gap / tracking the menus' hints share
 * (ui_prompt_icon, ui_prompt_gap, ui_prompt_tracking). */
extern int pc_qol_fast_travel_prompt_backdrop;
extern float pc_qol_fast_travel_prompt_size, pc_qol_fast_travel_prompt_stroke, pc_qol_fast_travel_prompt_stretch,
    pc_qol_fast_travel_prompt_pad_x, pc_qol_fast_travel_prompt_pad_y, pc_qol_fast_travel_prompt_border;
extern float pc_qol_fast_travel_prompt_color[4], pc_qol_fast_travel_prompt_stroke_color[4];
extern float pc_ui_prompt_icon, pc_ui_prompt_gap, pc_ui_prompt_tracking;

typedef struct {
    int enabled, cost, flat;
    float scale;
} pc_ft_settings;

typedef struct {
    int game;     /* vs_main_stateFlags.unkB5: 0 not learned, 1 learned, 2 the spirit paling */
    int intro;    /* introState (3 and up: the prologue is over) */
    int warped;   /* the Room Browser: warped, or a warp under way */
    int no_save;  /* unkA7: the game refuses to save here now */
    int circle;   /* func_8008A4FC(): the Circle under Ashley (point + 1), 0 none */
    int here;     /* this room's point in MENU0's table (_getTeleportCost's), PC_FT_POINTS none */
    int event;    /* an event script, a script room change, a screen transition or game over */
    int busy;     /* Ashley acts (unk954) or falls (status 0x4000), or L2 / R2 held */
    unsigned mask; /* func_800CABE0(0) now */
    int mp;
    unsigned char states[PC_FT_POINTS]; /* savePointStates: 0 not found, 1 found, 2 the spirit paling */
} pc_ft_state;

int pc_ft_here(int location);             /* this room's point (MENU0's search: PC_FT_POINTS when none) */
int pc_ft_raw_cost(int here, int target); /* _getTeleportCost */
int pc_ft_whose(const pc_ft_settings* c, const pc_ft_state* s);
int pc_ft_unlock(const pc_ft_settings* c, const pc_ft_state* s); /* what MENU0 reads for unkB5 */
int pc_ft_cost(const pc_ft_settings* c, const pc_ft_state* s, int raw);
unsigned pc_ft_mask(const pc_ft_settings* c, const pc_ft_state* s, unsigned bits);
unsigned pc_ft_menu_bits(const pc_ft_settings* c, const pc_ft_state* s); /* menuFlags bits to show (bit 0 Magic) */
/* NULL, or why the port's unlock refuses to leave from here (R1, R3, R7). */
const char* pc_ft_blocked(const pc_ft_settings* c, const pc_ft_state* s);
/* NULL, or why destination `point` is refused (R4, the mask, MP). */
const char* pc_ft_refused(const pc_ft_settings* c, const pc_ft_state* s, int point);
/* NULL when the Travel button opens the destinations now, or why not. */
const char* pc_ft_shortcut(const pc_ft_settings* c, const pc_ft_state* s);
int pc_ft_counts_as_magic(const pc_ft_state* s); /* case 10 sets magicUsed */
int pc_ft_packed(int point, int cost);           /* MENU0's hand-off (D_800F4E98.executeAbility) */

/* The RPC report (fast_travel.c fills it, pc_ft_json writes it). */
typedef struct {
    int zone, room;   /* the destination's room (the executable's location table) */
    int circle_id;    /* its Circle object's id (section 9, type 16), 0 none */
    int cell[3];      /* its x, z, level */
    int ok;           /* id == point + 1 and the cell is the arrival cell, level 0 */
} pc_ft_audit_point;
typedef struct {
    int valid, from, to, location, whose, counted;
    int arrived, circle, room_ok, mp_before, mp_after;
} pc_ft_trip;
typedef struct {
    pc_ft_settings settings;
    pc_ft_state state;
    int prompt, prompt_on;          /* the prompt would show now; qol_fast_travel_prompt */
    unsigned presses, opened, ignored, prompt_drawn;
    float prompt_box[4];            /* the prompt as last drawn (window pixels x0, y0, x1, y1) */
    float picture[4];               /* ... the game picture it was placed in (x, y, w, h) */
    int prompt_font;                /* ... its verb's font: 1 the HUD labels' (0: missing, the menus' label font) */
    int prompt_backdrop;            /* ... on a plate (qol_fast_travel_prompt_backdrop) */
    float prompt_stroke;            /* ... its outline's width (window pixels; 0: none) */
    int prompt_hold;                /* qol_fast_travel_prompt_hold */
    int prompt_anchor;              /* qol_fast_travel_prompt_anchor, _offset_x / _y, _scale */
    float prompt_offset[2], prompt_scale;
    pc_ft_trip last;
    int audited;
    pc_ft_audit_point audit[PC_FT_USED];
} pc_ft_report;
/* The report as JSON into out (its length, or -1 when it does not fit). */
int pc_ft_json(const pc_ft_report* r, char* out, int size);
/* The [travel] log lines (stderr). */
void pc_ft_log_trip(const pc_ft_trip* t);
void pc_ft_log_arrival(const pc_ft_trip* t);

/* fast_travel.c (battle builds): the src hooks and the field poll. */
int pc_fast_travel_unlock(void);             /* MENU0, for vs_main_stateFlags.unkB5 */
int pc_fast_travel_cost(int raw);            /* MENU0: what destination of raw cost `raw` costs */
unsigned pc_fast_travel_mask(unsigned bits); /* MENU0 / BATTLE: the menu mask bits that grey Magic */
unsigned pc_fast_travel_menu_bits(void);     /* BATTLE func_800C930C: commands shown, never stored */
int pc_fast_travel_blocked(void);            /* MENU0: the Teleportation row greyed by the port's rules */
const char* pc_fast_travel_why(void);        /* ... why (the modern Magick page) */
int pc_fast_travel_begin(int packed);        /* BATTLE func_800735F8 case 10: 1 = set magicUsed */
int pc_fast_travel_poll(void);               /* room_warp.c's field poll: 1 = the Travel button opened the list */
void pc_fast_travel_room_loaded(void);       /* room_warp.c: a room's sections are loaded */
void pc_fast_travel_reset(void); /* boot_sdk.c: the game's state is replaced (no room read until one loads) */
void pc_fast_travel_press(void);             /* RPC fast_travel {press}: the Travel button, once */
int pc_fast_travel_prompt(void);             /* the prompt shows now (fast_travel_prompt.cpp) */
void pc_fast_travel_report(pc_ft_report* r, int audit); /* RPC fast_travel */

#ifdef __cplusplus
}
#endif
#endif
