/* Weapon slots: a quick weapon (and shield) swap on L2 + R2, ported from
 * eremtsov's Vagrant Story QoL mod (CC0; https://github.com/eremtsov/rood-reverse,
 * branch qol-mod-classic, src/mod/wswap.c). Only this feature of the mod is
 * ported, as its "classic buttons" variant has it: every other control stays
 * the game's own. Reimplemented here for the native build; the behaviour,
 * the overlay layout and the save format are the mod's.
 *
 * Compiled with the game headers (battle builds). Off unless the option
 * pc_qol_weapon_slots (settings key qol_weapon_slots, RPC weapon_slots
 * {enabled}, editor Graphics > Gameplay) is set; while it is off nothing here
 * touches the game. The src hooks (ROOD_PC):
 *   vs_battle_exec (BATTLE 146C.c)  reads the pad through
 *                                   pc_weapon_slots_process_pad
 *   func_800C9FE8 (BATTLE 5BF94.c)  the L2 shortcut overlay asks
 *                                   pc_weapon_slots_label for each label
 *   _copyContainer (MENU7 260.c)    remaps 3 gem bytes, not 4 (see below)
 *
 * Controls (field, menu closed):
 *   L2 + R2 (held)        the L2 overlay shows the weapon of each of the 8 slots
 *   ... + slot button     equips that slot's weapon and shield (Circle,
 *                         Triangle, Square, Cross, D-pad Up, Down, Right, Left =
 *                         slots 1..8; the D-pad slots react only to the real
 *                         D-pad, not to the left stick the game ORs into it)
 *   ... + Select          assign mode on/off: the next slot button puts the
 *                         equipped weapon (with the equipped shield when it is
 *                         one-handed) into that slot; a weapon in another slot
 *                         moves, its own slot clears
 *   ... + Start           the equipped shield becomes the shield of every
 *                         occupied one-handed slot (none equipped: they lose it)
 *   R2 (held) + Select    the equipped weapon into the first free slot, or out
 *                         of its slot
 *
 * A swap with the weapon drawn plays the game's sheathe animation
 * (func_800734C0), waits until the actor is idle (func_8009E4B0), swaps while
 * the world is held (field mode 0) and draws again. The swap is the equip
 * menu's sequence (MENU3 _equipWeaponMenu, shield flags, model loads) as a
 * per-frame state machine; the new weapon's name shows in the information box
 * for about 2.5 s.
 *
 * Save format (the mod's, so memory cards are interchangeable with the mod
 * and with the unmodded game):
 *   vs_main_inventory.weapons[i].gems[3]  the weapon's slot, 1..8, 0 = none
 *       (no grip has a 4th gem slot: every gem loop of the game stops at 3 or
 *       at gemSlots <= 3, so the unmodded game leaves 0 there)
 *   vs_main_inventory.shields[i].gems[3]  bit N set = this shield belongs to
 *       slot N + 1 (one shield per slot; one shield may serve several slots)
 * Version 2 of the mod marked any number of favourites with 1; migrate() gives
 * those weapons distinct slots (and clears values above 8). The Container's
 * save (MENU7 _copyContainer) remapped all 4 gem bytes of weapons and
 * shields as gem indices, which scrambled these bytes; it remaps 3 now. */
#include "common.h"
#include "../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../src/BATTLE/BATTLE.PRG/2842C.h"
#include "../../src/BATTLE/BATTLE.PRG/30DB0.h"
#include "../../src/BATTLE/BATTLE.PRG/3A1A0.h"
#include "../../src/BATTLE/BATTLE.PRG/44F14.h"
#include "../../src/BATTLE/BATTLE.PRG/4A0A8.h"
#include "../../src/BATTLE/BATTLE.PRG/573B8.h"
#include "../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../src/SLUS_010.40/main.h"
#include <libetc.h>

extern int sprintf(char*, const char*, ...);

extern u_int D_800F196C; /* field mode: 0 held, 1 field, 6 L2 shortcuts, 10 (un)sheathing */
extern u_char D_800EBCE8; /* L2 overlay slide: 10 hidden, 0 fully in */
int func_8009E4B0(int); /* the actor is busy (an animation runs) */
void func_800734C0(void); /* draw/sheathe toggle (the field's button) */
void func_80073554(void); /* leave the L2 shortcut mode (6) for the field (1) */
int func_800C9EB8(int); /* L2 overlay: the label's sprite (0 = not drawn) */
void func_800C9F98(int xy, int sprite); /* L2 overlay: a label's bullet */

int pc_weapon_slots_process_pad(void);
int pc_weapon_slots_label(int id);
void pc_weapon_slots_json(char* out, unsigned size);

extern int pc_qol_weapon_slots; /* project_settings.cpp */

#define MSG_TICKS 150 /* how long the weapon name stays up (game ticks) */
#define SHEATHE_TIMEOUT 90
#define FIELD_MENU_CLOSED(m) (((m) & 0x3F) == 0x3F)

/* Per overlay label id (0..3 the button group, 4..7 the D-pad group): text x,
 * y, bullet x, y, width budget in characters (6 px each), slot. Positions with
 * the overlay fully slid in; the button group moves with +step, the D-pad
 * group with -step. Odd ids are right-aligned (the text ends at x). */
static const u_char labels[8][6] = {
    { 242, 99, 237, 101, 12, 2 }, { 214, 108, 213, 115, 17, 3 },
    { 252, 111, 247, 113, 10, 1 }, { 223, 126, 223, 127, 19, 4 },
    { 94, 99, 89, 101, 21, 5 }, { 71, 112, 70, 115, 11, 8 },
    { 103, 117, 98, 113, 17, 7 }, { 81, 124, 80, 127, 12, 6 },
};
/* Slot 1..8 -> bit of the button words: Circle, Triangle, Square, Cross, Up,
 * Down, Right, Left. */
static const u_char slot_bits[8] = { 5, 4, 7, 6, 12, 14, 13, 15 };
/* Small-font palettes: weapon / equipped, the same in assign mode, the hint,
 * the assign prompt (4 white, 5 yellow, 6 orange, 7 dim). */
static const u_char palettes[6] = { 4, 5, 6, 5, 7, 6 };
/* Game text encoding (A..Z = 10..35, ':' 0x9E, ' ' 0x8F, end 0xE7):
 * "SELECT: ASSIGN  START: SHIELD", then "ASSIGN: PRESS A SLOT". */
static const u_char hints[2][32] = {
    { 28, 14, 21, 14, 12, 29, 0x9E, 0x8F, 10, 28, 28, 18, 16, 23, 0x8F, 0x8F, 28, 29, 10, 27, 29, 0x9E, 0x8F, 28,
        17, 18, 14, 21, 13, 0xE7 },
    { 10, 28, 28, 18, 16, 23, 0x9E, 0x8F, 25, 27, 14, 28, 28, 0x8F, 10, 0x8F, 28, 21, 24, 29, 0xE7 },
};

static struct {
    int loading; /* swap state machine: 0 idle, 1 waiting for the load queue, 2 sheathing */
    int redraw; /* draw the weapon again when the swap is done */
    int wait; /* frames left for the sheathe animation */
    int msg; /* ticks left for the weapon name box, 0 = not ours */
    vs_main_inventoryWeapon* target; /* weapon being swapped to */
    int wmode; /* the L2 overlay shows weapons instead of shortcuts */
    int assign; /* the next slot button assigns the equipped weapon */
    int raw; /* physical D-pad bits (12..15) of the previous frame */
    int swaps, assigns; /* counters (RPC) */
} s;

#define SHIELD_SLOTS(shield) ((shield)->gems[3])
#define SLOT_BIT(n) ((1 << (n)) >> 1) /* slot 1..8 -> bit 0..7, 0 -> 0 */

/* Two-handed blade categories (vs_mainMenu_weaponHands). */
static int two_handed_category(int category) { return (0x1D4 >> (category - 1)) & 1; }

static int two_handed(vs_main_inventoryWeapon* w)
{
    return two_handed_category((u_char)vs_main_inventory.blades[w->blade - 1].category);
}

/* The weapon in slot n (1..8), or NULL. */
static vs_main_inventoryWeapon* in_slot(int n)
{
    int i;
    for (i = 0; i < 8; ++i) {
        vs_main_inventoryWeapon* w = &vs_main_inventory.weapons[i];
        if (w->blade && w->gems[3] == n)
            return w;
    }
    return NULL;
}

/* Index of the equipped weapon, or -1. */
static int equipped_weapon(void)
{
    int i, found = -1;
    for (i = 0; i < 8; ++i) {
        if (vs_main_inventory.weapons[i].isEquipped && vs_main_inventory.weapons[i].blade)
            found = i;
    }
    return found;
}

/* Every weapon has its own slot. Version 2 of the mod marked any number of
 * favourites with 1: the first keeps slot 1, the others get the lowest free
 * slots in inventory order. Values above 8 are garbage. Idempotent, runs
 * every field frame. */
static void migrate(void)
{
    vs_main_inventoryWeapon* w = vs_main_inventory.weapons;
    int i, k, g;
    for (i = 0; i < 8; ++i) {
        g = w[i].gems[3];
        if (g > 8) {
            w[i].gems[3] = 0;
        } else if (g && w[i].blade && in_slot(g) != &w[i]) {
            for (k = 1; in_slot(k); ++k) {
            }
            w[i].gems[3] = k;
        }
    }
}

/* Slot n gets the equipped shield (with != 0) or none: its bit is set on the
 * equipped shield and cleared on all others. n == 0 does nothing. */
static void shield_bind(int n, int with)
{
    int i;
    for (i = 0; i < 8; ++i) {
        vs_main_inventoryShield* shield = &vs_main_inventory.shields[i];
        if (with && shield->isEquipped)
            SHIELD_SLOTS(shield) |= SLOT_BIT(n);
        else
            SHIELD_SLOTS(shield) &= ~SLOT_BIT(n);
    }
}

/* Start in the overlay: the equipped shield becomes the shield of every
 * occupied one-handed slot (none equipped: those slots lose theirs). */
static void shield_all(void)
{
    int i;
    for (i = 0; i < 8; ++i) {
        vs_main_inventoryWeapon* w = &vs_main_inventory.weapons[i];
        if (w->blade && !two_handed(w))
            shield_bind(w->gems[3], 1);
    }
    vs_main_playSfxDefault(0x7E, 0x18); /* the workshop's confirm */
}

/* The shield side of a swap to weapon w (two: two-handed): the slot's shield,
 * none for a two-handed weapon or a slot without a (still existing) shield.
 * The equip menu's steps (flags, stats, the model slot). Returns the shield
 * put on (its model is queued), or NULL. */
static vs_main_inventoryShield* shield_swap(vs_main_inventoryWeapon* w, int two)
{
    vs_main_inventoryShield* shields = vs_main_inventory.shields;
    vs_main_inventoryShield* on = NULL;
    vs_main_inventoryShield* want = NULL;
    int i;

    for (i = 0; i < 8; ++i) {
        if (shields[i].isEquipped)
            on = &shields[i];
        if (!two && (SHIELD_SLOTS(&shields[i]) & SLOT_BIT(w->gems[3])) && shields[i].base.id)
            want = &shields[i];
    }
    if (want == on)
        return NULL;
    for (i = 0; i < 8; ++i)
        shields[i].isEquipped = 0;
    if (want)
        want->isEquipped = 1;
    vs_battle_equipShield(want);
    func_8009AA84(1);
    return want;
}

/* The equip menu's sequence (MENU3 _equipWeaponMenu and _loadObjectData) on
 * actor 0; the main loop processes the load queue. */
static void begin_swap(vs_main_inventoryWeapon* w)
{
    vs_main_inventoryBlade* blade = &vs_main_inventory.blades[w->blade - 1];
    vs_battle_objectData* q = vs_battle_objectDataSlots;
    vs_main_inventoryShield* shield;
    int i;
    int category = (u_char)blade->category;
    int wep = (u_char)blade->wepId;
    int material = blade->material;

    for (i = 0; i < 8; ++i)
        vs_main_inventory.weapons[i].isEquipped = 0;
    w->isEquipped = 1;

    /* A two-handed weapon drops the shield, a one-handed one gets the slot's. */
    shield = shield_swap(w, two_handed_category(category));

    vs_battle_equipWeapon(w);

    if (D_800F4538[0]->unk5B1 != category) {
        func_8009CC20(0, 0);
        q->dataType = 7;
        q->index = 0;
        q->modelId = category;
        q->actorId = 0;
        ++q;
    }

    func_8009AA84(0);

    for (;;) { /* the weapon's model, then the shield's (index 1: wepId >= 96) */
        if (wep) {
            i = wep >= 96;
            q->dataType = 3;
            q->index = i;
            q->modelId = wep;
            psx_set(q->unk4, psx_get(vs_battle_characterState->unk48[i]));
            q->actorId = 0;
            q->unk11 = 240 + i;
            q->material = material;
            ++q;
        }
        if (shield == NULL)
            break;
        wep = (u_char)shield->base.wepId;
        material = shield->base.material;
        shield = NULL;
    }

    s.target = w;
    D_800F196C = 0; /* hold the world as the menu does (mode 0 does nothing) */
    s.loading = 1;
    ++s.swaps;
}

/* The moments a swap may start: no screen transition, no chain/defense
 * ability timing, the actor idle, nothing loading. */
static int swap_allowed(void)
{
    return ((vs_battle_screenTransitionStep - 1) >= 2U) && (vs_battle_screenTransitionStep != 4)
        && !(func_800A0BE0(0) & 0x6000) && (psx_get(vs_battle_characterState->unk3C)->unk954 == 0)
        && (func_8009E4B0(0) == 0) && (vs_battle_objectDataSlots[0].dataType == 0);
}

/* The sound, then sheathe first when the weapon is drawn, else swap at once. */
static void start_swap(vs_main_inventoryWeapon* w)
{
    vs_battle_playMenuChangeSfx();
    s.redraw = vs_battle_characterState->weaponDrawn & 1;
    if (s.redraw) {
        func_800734C0();
        D_800F196C = 0;
        s.target = w;
        s.wait = SHEATHE_TIMEOUT;
        s.loading = 2;
    } else {
        begin_swap(w);
    }
}

/* The equipped weapon into slot n; the weapon there loses it. Already in slot
 * n: the slot clears. n == 0: its own slot (clear), else the first free one.
 * Returns 1 when something changed. */
static int assign(int n)
{
    vs_main_inventoryWeapon* e;
    vs_main_inventoryWeapon* other;
    int current = equipped_weapon();

    if (current < 0) {
        vs_battle_playInvalidSfx();
        return 0;
    }
    e = &vs_main_inventory.weapons[current];
    if (n == 0) {
        n = e->gems[3];
        if (n == 0) {
            for (n = 1; n < 9 && in_slot(n); ++n) {
            }
            if (n == 9) {
                vs_battle_playInvalidSfx();
                return 0;
            }
        }
    }
    ++s.assigns;
    if (e->gems[3] == n) {
        e->gems[3] = 0;
        shield_bind(n, 0);
        vs_battle_playMenuLeaveSfx();
        return 1;
    }
    other = in_slot(n);
    if (other != NULL)
        other->gems[3] = 0;
    shield_bind(e->gems[3], 0); /* the slot it leaves has no weapon any more */
    shield_bind(n, !two_handed(e));
    e->gems[3] = n;
    vs_battle_playMenuSelectSfx();
    return 1;
}

/* Text in the game's small menu font (font table 0, glyphs in VRAM whenever
 * the field runs), at most max characters of 6 px. flags: bits 0-2 the
 * alignment (pixels per character the start moves left: 0 left, 3 centre, 6
 * right), bits 3.. the palette. Names separate words with `0xFA n` and prose
 * with 0x8F: both are drawn as one 0x8F; trailing blanks are dropped. */
static void draw_small(const u_char* text, int xy, int max, int flags)
{
    const u_char* e = text;
    int n = 0; /* characters that fit */
    int k = 0; /* characters up to the last visible one */
    int x = (short)xy;
    int c;

    while (n < max && (c = *e) != 0xE7) {
        ++n;
        e += 1 + (c == 0xFA);
        if (c != 0xFA && c != 0x8F)
            k = n;
    }
    x -= k * (flags & 7);
    vs_battle_setFontStyle(flags >> 3);
    for (; k != 0; --k) {
        c = *text;
        if (c == 0xFA) {
            c = 0x8F;
            ++text;
        }
        ++text;
        x = vs_battle_printVariableWidthFontChar(
            c, x, (xy >> 16) - 1, (u_long*)((char*)psx_get(vs_scratch.unk8) - 12));
    }
}

/* func_800C9FE8 (the L2 overlay) asks for each label, ids 4..7 then 0..3. Not
 * in weapon mode: the game's answer. In weapon mode the label sprite is not
 * drawn (0): the bullet and the weapon's name are drawn here instead. */
int pc_weapon_slots_label(int id)
{
    const u_char* t;
    vs_main_inventoryWeapon* w;
    int step;

    if (!s.wmode)
        return func_800C9EB8(id);
    t = labels[id];
    w = in_slot(t[5]);
    step = (u_char)vs_battle_rowAnimationSteps[D_800EBCE8];
    if (id >= 4)
        step = -step;
    func_800C9F98(((t[2] + step) & 0xFFFF) | (t[3] << 16), w ? 0x180 : 0x140);
    if (w) { /* an empty slot shows the bullet only */
        draw_small((const u_char*)w->name, ((t[0] + step) & 0xFFFF) | (t[1] << 16), t[4],
            ((id & 1) * 6) | (palettes[s.assign * 2 + (w->isEquipped & 1)] << 3));
    }
    if (id == 4 && D_800EBCE8 == 0) /* the line under the overlay: what Select does */
        draw_small(hints[s.assign], 160 | (142 << 16), 30, 3 | (palettes[4 + s.assign] << 3));
    return 0;
}

/* L2 + R2 held in the shortcut mode (6): Start writes shields, Select toggles
 * assign mode, a slot button assigns or equips. new_dpad: real D-pad buttons
 * that went down this frame. The game's own shortcut handler sees none of
 * these buttons while L2 + R2 are held. */
static void slot_mode(int new_dpad)
{
    int pressed = vs_main_buttonsPressed.all;
    int events = (pressed & 0xF0) | new_dpad;
    vs_main_inventoryWeapon* w;
    int k;

    vs_main_buttonsPressed.all &= ~(0xF0F0 | PADselect | PADstart);
    vs_main_buttonRepeat &= ~(0xF0F0 | PADselect | PADstart);
    if (pressed & PADstart) { /* one press, one write; swallowed (no first-person view) */
        shield_all();
        return;
    }
    if (pressed & PADselect) {
        s.assign ^= 1;
        if (s.assign)
            vs_battle_playMenuSelectSfx();
        else
            vs_battle_playMenuLeaveSfx();
        return;
    }
    for (k = 0; k < 8; ++k) {
        if ((events >> slot_bits[k]) & 1)
            break;
    }
    if (k == 8)
        return;
    ++k;
    if (s.assign) {
        if (assign(k))
            s.assign = 0;
        return;
    }
    w = in_slot(k);
    if (w == NULL || w->isEquipped || !swap_allowed()) {
        vs_battle_playInvalidSfx();
        return;
    }
    func_80073554(); /* the game's own way out of the shortcut mode */
    start_swap(w);
}

/* The swap state machine, one step per frame; 1 while it holds the frame. */
static int swap_step(void)
{
    D_800F4538_t* a = D_800F4538[0];
    vs_battle_textBox* box;

    if (D_800F196C == 0) {
        func_800B64A8(0, 0, 0); /* stand still, as the game does while (un)sheathing */
    } else if (s.loading == 2) {
        s.loading = 0; /* something else took over the world: give up */
        return 1;
    }
    if (s.loading == 2) {
        if (func_8009E4B0(0) == 0 || --s.wait <= 0)
            begin_swap(s.target);
        return 1;
    }
    if (vs_battle_objectDataSlots[0].dataType != 0)
        return 1;
    /* The queue drained: restart the idle animation on the (maybe new)
     * category's table and refresh the pose and the model flags, as
     * func_800FA2CC does after the menu changed the weapon. */
    a->unk187C = 0;
    func_800A0204(0, 1, 0, 0);
    func_800AFA28(a, &a->unkC54, 1);
    vs_main_memcpy(&a->unk704, &a->unkC54, sizeof a->unk704);
    a->unkC54.unk540 = 0;
    func_8009D934(0, a->unk0.weaponDrawn, 2);
    if (D_800F196C == 0) {
        if (s.redraw)
            func_800734C0(); /* draw the new weapon (mode 10) */
        else
            D_800F196C = 1;
        box = &vs_battle_textBoxes[7];
        if (box->state == 0 || s.msg) {
            vs_battle_initInformationTextBox(informationTextBoxHeaderNone);
            vs_battle_setTextBox(7, s.target->name);
            s.msg = MSG_TICKS;
        }
    }
    s.loading = 0;
    return 0;
}

/* vs_battle_exec's pad read. With the option off: vs_main_processPadState
 * alone (a swap that is under way still finishes). */
int pc_weapon_slots_process_pad(void)
{
    int ret = vs_main_processPadState();
    int dpad, new_dpad, mode, both;
    vs_battle_textBox* box;

    if (!pc_qol_weapon_slots) {
        /* Never on: nothing here runs. Turned off: what was under way ends. */
        s.wmode = s.assign = 0;
        if (s.msg) {
            box = &vs_battle_textBoxes[7];
            if ((unsigned long)((char*)psx_get(box->string) - (char*)vs_main_inventory.weapons)
                < sizeof vs_main_inventory.weapons)
                vs_battle_dismissTextBox(7);
            s.msg = 0;
        }
        if (s.loading)
            swap_step();
        return ret;
    }

    /* The real D-pad: the game ORs the left stick into the D-pad bits. */
    dpad = ((~vs_main_padBuffer[0][2] & 0xF0) << 8) & vs_main_buttonsState;
    new_dpad = dpad & ~s.raw;
    s.raw = dpad;

    /* The weapon name box: ours while box 7 still shows an inventory name. */
    if (s.msg) {
        box = &vs_battle_textBoxes[7];
        if ((unsigned long)((char*)psx_get(box->string) - (char*)vs_main_inventory.weapons)
            >= sizeof vs_main_inventory.weapons) {
            s.msg = 0; /* the game took the box over */
        } else if (!FIELD_MENU_CLOSED(vs_battle_menuState.currentMenu)
                   || (s.msg -= vs_gametime_tickspeed) <= 0) {
            vs_battle_dismissTextBox(7);
            s.msg = 0;
        }
    }

    if (s.loading && swap_step())
        return ret;

    /* L2 + R2: the overlay shows weapons. The mode stays on until the overlay
     * is hidden, so the game's labels do not flash while it slides out. */
    mode = D_800F196C;
    both = (vs_main_buttonsState & (PADL2 | PADR2)) == (PADL2 | PADR2);
    if (both) {
        if (FIELD_MENU_CLOSED(vs_battle_menuState.currentMenu) && (mode == 1 || mode == 6))
            s.wmode = 1;
    } else {
        s.assign = 0;
        if ((vs_main_buttonsState & PADL2) || D_800EBCE8 == 10)
            s.wmode = 0;
    }

    if (!FIELD_MENU_CLOSED(vs_battle_menuState.currentMenu))
        return ret;
    migrate();
    if (mode == 6) {
        if (both)
            slot_mode(new_dpad);
        return ret;
    }
    /* R2 + Select: the one input outside the overlay that changes slots. */
    if (mode == 1 && (vs_main_buttonsState & PADR2) && (vs_main_buttonsPressed.all & PADselect)) {
        if (equipped_weapon() >= 0)
            assign(0);
        vs_main_buttonsPressed.all &= ~PADselect;
        vs_main_buttonRepeat &= ~PADselect;
    }
    return ret;
}

/* RPC weapon_slots: the option, the state and the slots. */
void pc_weapon_slots_json(char* out, unsigned size)
{
    char* p = out;
    int i;
    if (size < 2048) {
        if (size)
            *out = 0;
        return;
    }
    p += sprintf(p,
        "{\"enabled\":%d,\"overlay\":%d,\"assign\":%d,\"loading\":%d,\"swaps\":%d,\"assigns\":%d,"
        "\"field_mode\":%d,\"equipped\":%d,\"weapons\":[",
        pc_qol_weapon_slots != 0, s.wmode, s.assign, s.loading, s.swaps, s.assigns, (int)D_800F196C,
        equipped_weapon());
    for (i = 0; i < 8; ++i) {
        vs_main_inventoryWeapon* w = &vs_main_inventory.weapons[i];
        p += sprintf(p, "%s{\"blade\":%d,\"equipped\":%d,\"slot\":%d}", i ? "," : "", w->blade,
            w->isEquipped != 0, w->gems[3]);
    }
    p += sprintf(p, "],\"shields\":[");
    for (i = 0; i < 8; ++i) {
        vs_main_inventoryShield* shield = &vs_main_inventory.shields[i];
        p += sprintf(p, "%s{\"id\":%d,\"equipped\":%d,\"slots\":%d}", i ? "," : "", shield->base.id,
            shield->isEquipped != 0, SHIELD_SLOTS(shield));
    }
    p += sprintf(p, "]");
    /* What Ashley's battle record has on (the equip routines' result): the
     * inventory index of the weapon and the shield (0 none), the model's
     * category. */
    if (vs_battle_characterState && psx_get(vs_battle_characterState->unk3C) && D_800F4538[0]) {
        vs_battle_actor2* actor = psx_get(vs_battle_characterState->unk3C);
        p += sprintf(p, ",\"actor_weapon\":%d,\"actor_shield\":%d,\"actor_category\":%d,\"weapon_drawn\":%d",
            actor->weapon.index, actor->shield.index, D_800F4538[0]->unk5B1,
            vs_battle_characterState->weaponDrawn & 1);
    }
    sprintf(p, "}");
}
