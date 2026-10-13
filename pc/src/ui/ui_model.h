/* Modern menus: what the screens show and ask for, as plain C (filled and
 * executed by the battle-side bridges; no game headers here). */
#ifndef PC_UI_MODEL_H
#define PC_UI_MODEL_H
#ifdef __cplusplus
extern "C" {
#endif

enum { PC_UI_CLASSES = 6, PC_UI_AFFINITIES = 7, PC_UI_EQUIP_SLOTS = 8, PC_UI_CANDIDATES = 32, PC_UI_QUICK = 8 };
enum { PC_UI_SLOT_WEAPON, PC_UI_SLOT_SHIELD, PC_UI_SLOT_RARM, PC_UI_SLOT_LARM, PC_UI_SLOT_HEAD, PC_UI_SLOT_BODY,
       PC_UI_SLOT_LEGS, PC_UI_SLOT_ACCESSORY };

typedef struct {
    char name[48];      /* UTF-8; "" = nothing */
    char material[24];  /* UTF-8; "" = none */
    int inventory;      /* index into the game's inventory array of that kind, -1 = none */
    int dp, dp_max, pp, pp_max;  /* as the game's menus show them (DP rounded up from hundredths) */
    int str, intel, agl;
    int classes[PC_UI_CLASSES];
    int affinities[PC_UI_AFFINITIES];
    int damage_type;    /* weapons: 0 blunt, 1 edged, 2 piercing; else -1 */
    int two_handed;     /* weapons */
    int quick;          /* weapons: quick-swap slot 1..8, 0 none */
} pc_ui_item;

typedef struct {
    int valid;                                  /* the bridge filled it this session */
    int str, intel, agl;                        /* Ashley's own (the record's, with buffs; no equipment: only the accessory adds to them, see ui_equip_draw.cpp shown()) */
    pc_ui_item equipped[PC_UI_EQUIP_SLOTS];
    /* candidates for one slot (the one the shell asked for: slot field, -1 none) */
    int slot;
    int count;
    pc_ui_item candidates[PC_UI_CANDIDATES];
    /* quick swap: weapon and shield inventory indices per slot (-1 none), and
     * their names ("" none) so the screen can show them without a candidate list */
    int quick_weapon[PC_UI_QUICK], quick_shield[PC_UI_QUICK];
    char quick_weapon_name[PC_UI_QUICK][48], quick_shield_name[PC_UI_QUICK][48];
    int busy;                                   /* no equip now: a model load runs, or the game's menu is still loading */
    int quick_enabled;                          /* the field's quick swap controls are on (settings key qol_weapon_slots) */
} pc_ui_equipment;

enum { PC_UI_REQ_NONE, PC_UI_REQ_EQUIP, PC_UI_REQ_QUICK_SET, PC_UI_REQ_QUICK_CLEAR, PC_UI_REQ_CANDIDATES };
typedef struct {
    int kind;
    int slot;      /* EQUIP/CANDIDATES: PC_UI_SLOT_*; QUICK_*: 0..7 */
    int item;      /* EQUIP: inventory index, -1 = take off; QUICK_SET: weapon inventory index */
    int shield;    /* QUICK_SET: shield inventory index, -1 none */
} pc_ui_request;

/* The Ashley tab (ashley_bridge.c fills it every tick). */
enum { PC_UI_LIMBS = 6, PC_UI_EFFECTS = 32 };
enum { PC_UI_LIMB_CRITICAL, PC_UI_LIMB_DAMAGED, PC_UI_LIMB_WOUNDED, PC_UI_LIMB_GOOD, PC_UI_LIMB_EXCELLENT };
typedef struct {
    char name[32];          /* UTF-8 */
    int hp, hp_max;
    int state;              /* PC_UI_LIMB_* (the game's vs_battle_getLimbStatus) */
} pc_ui_limb;
typedef struct {
    char name[32];          /* UTF-8 */
    char help[160];         /* UTF-8 description */
    int buff;               /* 1 buff, 0 ailment */
    int seconds;            /* remaining, -1 = no timer */
} pc_ui_effect;
typedef struct {
    int valid;
    int hp, hp_max, mp, mp_max, risk;   /* risk 0..100 */
    int str, intel, agl;                /* as the original Status shows them (its EQP column) */
    int limb_count;
    pc_ui_limb limbs[PC_UI_LIMBS];
    int effect_count;
    pc_ui_effect effects[PC_UI_EFFECTS]; /* in the game's display order */
    char area[64], room[96];            /* UTF-8 location ("" unknown) */
    int play_h, play_m, play_s;
    int model;                          /* 0 none, 1 loading, 2 shown */
    float model_rect[4];                /* layout units: where the model is drawn (x, y, w, h) */
    int model_yaw, model_pitch;         /* the view's orbit (4096 = a turn; pitch 0..896, 0 level) */
    int model_blocked;                  /* why no model: PC_UI_MODEL_* */
    int heap_free, heap_need;           /* bytes: the game heap's largest free block, what the clone needs (0 unknown) */
    unsigned tick;                      /* pc_vsync_count at the fill (the drawing trusts `model` only while fresh) */
} pc_ui_ashley;
enum { PC_UI_MODEL_OK, PC_UI_MODEL_NO_HEAP, PC_UI_MODEL_FREE_CAMERA };

#ifdef __cplusplus
}
#endif
#endif
