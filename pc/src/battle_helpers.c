/* Native equivalents of hand-written game assembly, added only when verified. */
/* pc/src/game/BATTLE/BATTLE.PRG/7E05C.s: func_800E6EAC returns immediately.
 * Its delay slot only writes caller-saved $t0 (the following function's lui). */
#include "psx_types.h" /* psx_load/psx_store: 4-byte guest pointers */
#include "game/BATTLE/BATTLE.PRG/ai_state.h"
#include "draw_items.h"
#include "scratch.h"
extern void pc_unimplemented(const char*);
void vs_core_memoryClearAlignedWords(void* destination, unsigned bytes);


/* 7E028.s: merge the two actor flags and derive their priority state.
 * Explicit offsets preserve the assembly ABI until this structure is shared. */
void func_800E6828(void* actor);


extern unsigned char* vs_battle_aiActorAiStates[];
extern unsigned char D_80069C0C[];
/* 7E05C.s: store an actor's two stat bytes and their maximum; empty actor
 * slots are ignored. The original's return value is not meaningful. */
void vs_battle_aiSetActorVerticalMovementSteps(int id, int first, int second);

/* 7E05C.s: propagate reciprocal actor relations through the linked actor list.
 * The reciprocal field is read before writing the outgoing field, including
 * the self-relation case; preserving that order preserves recursion behavior. */
void vs_battle_aiInitializeActorAiRelations(void* actor);


extern void vs_battle_aiTryBeginAshleyAlliance(void*, int);
void func_800E72D0(void);


int vs_field_sampleCollisionHeightCode(unsigned x, unsigned z, unsigned layer);


/* The sprite entry points of 573B8.c are items (game/spriteItem.h). */
/* The packet cursor, and the table's default slot, as the items' markers use them. */
static unsigned* cursor_get(void) { return vs_scratch.unk0; }
static void cursor_set(unsigned* cursor) { vs_scratch.unk0 = cursor; }
/* The free packet space the items' markers are made at (effect_native.c's fallback reads where its items left it). */
unsigned* vs_battle_itemCursor(void) { return cursor_get(); }

/* The sprite as an item (game/spriteItem.h): the packet's fields, the marker where it linked. */
int vs_battle_spriteItem(unsigned* ot, int brightness, int xy, int wh, int mode, int uv_clut)
{
    unsigned* cursor = cursor_get();
    int first = pc_frame_count();
    unsigned grey = (unsigned)brightness & 255;
    pc_frame_sprite(0, (short)xy, (short)(xy >> 16), (short)wh, (short)(wh >> 16), uv_clut & 255, uv_clut >> 8 & 255,
        (int)(grey * 0x010101u), uv_clut >> 16 & 0xffff, mode ? mode & 0x3fff : PC_ITEM_INHERIT,
        (brightness >> 7) & 2 ? PC_ITEM_SEMI : 0);
    cursor_set(pc_frame_place(first, pc_frame_count() - first, ot ? ot : vs_scratch.unk8, cursor));
    return first;
}
/* vs_battle_addTile as an item: a flat tile (code 0x60, 0x62: wh its size) or a flat line (0x40, 0x42: wh its end), placed
 * where the packet linked; 0 when the words are anything else (a draw mode and window pair: they stay the packet). */
int vs_battle_tileItem(unsigned* ot, unsigned rgb0, unsigned xy, unsigned wh)
{
    unsigned code = rgb0 >> 24;
    unsigned* cursor = cursor_get();
    int first = pc_frame_count();
    if ((code & 0xfc) == 0x60)
        pc_frame_tile(0, (short)xy, (short)(xy >> 16), (short)wh, (short)(wh >> 16), (int)(rgb0 & 0xffffff), PC_ITEM_INHERIT,
            code & 2 ? PC_ITEM_SEMI : 0);
    else if (code == 0x40 || code == 0x42) {
        pc_item* it = pc_frame_add(PC_ITEM_LINE, 0);
        if (it) {
            it->mode = PC_ITEM_INHERIT, it->flags = code & 2 ? PC_ITEM_SEMI : 0;
            it->x[0] = (short)xy, it->y[0] = (short)(xy >> 16), it->x[1] = (short)wh, it->y[1] = (short)(wh >> 16);
            it->rgb[0][0] = (unsigned char)rgb0, it->rgb[0][1] = (unsigned char)(rgb0 >> 8), it->rgb[0][2] = (unsigned char)(rgb0 >> 16);
        }
    } else
        return 0;
    cursor_set(pc_frame_place(first, pc_frame_count() - first, ot, cursor));
    return 1;
}
/* A flat tile (`wh` its size, the colour 0xBBGGRR) or a flat line (`wh` its end), semi-transparent when `semi`, placed at `ot`. */
void vs_battle_tileFlatItem(unsigned* ot, int semi, int rgb, int xy, int wh)
{
    vs_battle_tileItem(ot, ((unsigned)rgb & 0xffffff) | (semi ? 0x62000000u : 0x60000000u), (unsigned)xy, (unsigned)wh);
}
void vs_battle_lineFlatItem(unsigned* ot, int semi, int rgb, int xy0, int xy1)
{
    vs_battle_tileItem(ot, ((unsigned)rgb & 0xffffff) | (semi ? 0x42000000u : 0x40000000u), (unsigned)xy0, (unsigned)xy1);
}
const void* vs_battle_spriteCommand(int item) { return pc_frame_command(item); }
/* The sprite's colour (0xBBGGRR, each channel cut to a byte) and whether it is semi-transparent. */
int vs_battle_spriteColor(int item, int rgb, int semi)
{
    pc_item* it = pc_frame_item(item);
    if (it)
        it->rgb[0][0] = (unsigned char)rgb, it->rgb[0][1] = (unsigned char)(rgb >> 8), it->rgb[0][2] = (unsigned char)(rgb >> 16),
        it->flags = (unsigned short)(semi ? it->flags | PC_ITEM_SEMI : it->flags & ~PC_ITEM_SEMI);
    return item;
}

/* vs_battle_insertTpage's E1 word as an item: the draw mode for what draws after it, placed where its packet linked. */
void vs_battle_modeItem(unsigned* ot, int e1)
{
    unsigned* cursor = cursor_get();
    int first = pc_frame_count();
    pc_item* it = pc_frame_add(PC_ITEM_MODE_SET, 0);
    if (it)
        it->mode = (unsigned short)(e1 & 0x3fff);
    cursor_set(pc_frame_place(first, pc_frame_count() - first, ot, cursor));
}

/* The items the battle drawing needs where it linked a primitive at the table entry `ot` (one marker each). */
static int place(unsigned* ot, unsigned* cursor, int first)
{
    cursor_set(pc_frame_place(first, pc_frame_count() - first, ot, cursor));
    return first;
}
/* A flat triangle or quad (`verts`), `rgb` 0xBBGGRR, corners as x low, y high words. */
void vs_battle_flatItem(unsigned* ot, int verts, int semi, int rgb, const int* xy)
{
    unsigned* cursor = cursor_get();
    int first = pc_frame_count();
    pc_item* it = pc_frame_add(PC_ITEM_POLY, 0);
    if (it) {
        it->verts = (unsigned char)verts, it->flags = semi ? PC_ITEM_SEMI : 0, it->mode = PC_ITEM_INHERIT;
        it->rgb[0][0] = (unsigned char)rgb, it->rgb[0][1] = (unsigned char)(rgb >> 8), it->rgb[0][2] = (unsigned char)(rgb >> 16);
        for (int i = 0; i < verts; i++)
            it->x[i] = (short)xy[i], it->y[i] = (short)(xy[i] >> 16);
    }
    place(ot, cursor, first);
}
/* A shaded triangle or quad: a colour per corner. */
void vs_battle_shadedItem(unsigned* ot, int verts, int semi, const int* rgb, const int* xy)
{
    unsigned* cursor = cursor_get();
    int first = pc_frame_count();
    pc_item* it = pc_frame_add(PC_ITEM_POLY, 0);
    if (it) {
        it->verts = (unsigned char)verts, it->flags = PC_ITEM_SHADED | (semi ? PC_ITEM_SEMI : 0), it->mode = PC_ITEM_INHERIT;
        for (int i = 0; i < verts; i++) {
            it->x[i] = (short)xy[i], it->y[i] = (short)(xy[i] >> 16);
            it->rgb[i][0] = (unsigned char)rgb[i], it->rgb[i][1] = (unsigned char)(rgb[i] >> 8), it->rgb[i][2] = (unsigned char)(rgb[i] >> 16);
        }
    }
    place(ot, cursor, first);
}
/* A textured quad (FT4): the page word and CLUT it names, `rgb` the colour its texels are scaled by (a raw one is drawn as the texels
 * are), corners as x low, y high words and texels as u low, v high. */
int vs_battle_ft4Item(unsigned* ot, int raw, int semi, int tpage, int clut, int rgb, const int* xy, const int* uv)
{
    unsigned* cursor = cursor_get();
    int first = pc_frame_count();
    pc_item* it = pc_frame_add(PC_ITEM_POLY, 0);
    if (it) {
        it->verts = 4, it->flags = (unsigned short)(PC_ITEM_TEXTURED | (raw ? PC_ITEM_RAW : 0) | (semi ? PC_ITEM_SEMI : 0));
        it->mode = (unsigned short)tpage, it->clut = (unsigned short)clut;
        it->rgb[0][0] = (unsigned char)rgb, it->rgb[0][1] = (unsigned char)(rgb >> 8), it->rgb[0][2] = (unsigned char)(rgb >> 16);
        for (int i = 0; i < 4; i++) {
            it->x[i] = (short)xy[i], it->y[i] = (short)(xy[i] >> 16);
            it->u[i] = (unsigned char)uv[i], it->v[i] = (unsigned char)(uv[i] >> 8);
        }
    }
    return place(ot, cursor, first);
}
/* A textured Gouraud quad (GT4), opaque or semi-transparent: the page word and CLUT it names, `rgb`, `xy` and `uv` per corner (colours
 * 0xBBGGRR, places x low and y high, texels u low and v high). */
int vs_battle_gt4PlainItem(unsigned* ot, int semi, int tpage, int clut, const int* rgb, const int* xy, const int* uv)
{
    unsigned* cursor = cursor_get();
    int first = pc_frame_count();
    pc_item* it = pc_frame_add(PC_ITEM_POLY, 0);
    if (it) {
        it->verts = 4, it->flags = (unsigned short)(PC_ITEM_TEXTURED | PC_ITEM_SHADED | (semi ? PC_ITEM_SEMI : 0));
        it->mode = (unsigned short)tpage, it->clut = (unsigned short)clut;
        for (int i = 0; i < 4; i++) {
            it->x[i] = (short)xy[i], it->y[i] = (short)(xy[i] >> 16);
            it->u[i] = (unsigned char)uv[i], it->v[i] = (unsigned char)(uv[i] >> 8);
            it->rgb[i][0] = (unsigned char)rgb[i], it->rgb[i][1] = (unsigned char)(rgb[i] >> 8), it->rgb[i][2] = (unsigned char)(rgb[i] >> 16);
        }
    }
    return place(ot, cursor, first);
}
/* A shaded quad (POLY_G4) drawn in the E1 word `mode`, the E1 word `after` left as the draw mode (colours 0xBBGGRR, corners x low, y high). */
void vs_battle_g4AfterItem(unsigned* ot, int mode, int after, const int* rgb, const int* xy)
{
    unsigned* cursor = cursor_get();
    int first = pc_frame_count();
    pc_item* it = pc_frame_add(PC_ITEM_POLY, 0);
    if (it) {
        it->verts = 4, it->flags = PC_ITEM_SHADED | PC_ITEM_AFTER;
        it->mode = (unsigned short)(mode & 0x3fff), it->after = (unsigned short)(after & 0x3fff);
        for (int i = 0; i < 4; i++) {
            it->x[i] = (short)xy[i], it->y[i] = (short)(xy[i] >> 16);
            it->rgb[i][0] = (unsigned char)rgb[i], it->rgb[i][1] = (unsigned char)(rgb[i] >> 8), it->rgb[i][2] = (unsigned char)(rgb[i] >> 16);
        }
    }
    place(ot, cursor, first);
}
/* A flat line drawn in the E1 word `mode` (its draw mode set before it, as the packet that held both did). */
int vs_battle_lineModeItem(unsigned* ot, int semi, int rgb, int xy0, int xy1, int mode)
{
    unsigned* cursor = cursor_get();
    int first = pc_frame_count();
    pc_item* it = pc_frame_add(PC_ITEM_LINE, 0);
    if (it) {
        it->flags = semi ? PC_ITEM_SEMI : 0, it->mode = (unsigned short)(mode & 0x3fff);
        it->x[0] = (short)xy0, it->y[0] = (short)(xy0 >> 16), it->x[1] = (short)xy1, it->y[1] = (short)(xy1 >> 16);
        it->rgb[0][0] = (unsigned char)rgb, it->rgb[0][1] = (unsigned char)(rgb >> 8), it->rgb[0][2] = (unsigned char)(rgb >> 16);
    }
    return place(ot, cursor, first);
}
/* A shaded line (LINE_G2) between two points, semi-transparent when `semi`; returns the item. */
int vs_battle_lineItem(unsigned* ot, int semi, int rgb0, int xy0, int rgb1, int xy1)
{
    unsigned* cursor = cursor_get();
    int first = pc_frame_count();
    pc_item* it = pc_frame_add(PC_ITEM_LINE, 0);
    if (it) {
        it->flags = PC_ITEM_SHADED | (semi ? PC_ITEM_SEMI : 0), it->mode = PC_ITEM_INHERIT;
        it->x[0] = (short)xy0, it->y[0] = (short)(xy0 >> 16), it->x[1] = (short)xy1, it->y[1] = (short)(xy1 >> 16);
        it->rgb[0][0] = (unsigned char)rgb0, it->rgb[0][1] = (unsigned char)(rgb0 >> 8), it->rgb[0][2] = (unsigned char)(rgb0 >> 16);
        it->rgb[1][0] = (unsigned char)rgb1, it->rgb[1][1] = (unsigned char)(rgb1 >> 8), it->rgb[1][2] = (unsigned char)(rgb1 >> 16);
    }
    return place(ot, cursor, first);
}
/* An effect trail's segment: a textured Gouraud quad (GT4) at slot `depth` of the table scratch.unk4, bracketed by the default draw
 * mode as the effect plugins' packets were (effectItem.h), semi-transparent when `semi`. `rgb`, `xy` and `uv` per corner as the
 * others (rgb 0xBBGGRR; xy x low, y high; uv u low, v high), `tpage` its page word and `clut` its CLUT. */
void vs_battle_gt4Item(int depth, int semi, int tpage, int clut, const int* rgb, const int* xy, const int* uv)
{
    unsigned* cursor = cursor_get();
    pc_item* it = pc_frame_poly_at(depth, PC_ITEM_POLY, 4, PC_ITEM_TEXTURED | PC_ITEM_SHADED | PC_ITEM_BRACKET | (semi ? PC_ITEM_SEMI : 0),
        vs_scratch.unk4, &cursor);
    cursor_set(cursor);
    if (!it)
        return;
    it->mode = (unsigned short)tpage, it->clut = (unsigned short)clut;
    for (int i = 0; i < 4; i++) {
        it->x[i] = (short)xy[i], it->y[i] = (short)(xy[i] >> 16);
        it->u[i] = (unsigned char)uv[i], it->v[i] = (unsigned char)(uv[i] >> 8);
        it->rgb[i][0] = (unsigned char)rgb[i], it->rgb[i][1] = (unsigned char)(rgb[i] >> 8), it->rgb[i][2] = (unsigned char)(rgb[i] >> 16);
    }
}
/* The item (from vs_battle_lineItem, vs_battle_ft4Item) left out of the hardware picture, still in the game's own. */
void vs_battle_itemHardwareHidden(int item)
{
    pc_item* it = pc_frame_item(item);
    if (it)
        it->flags |= PC_ITEM_HW_HIDDEN;
}
/* The item bracketed as the effects' packets were: the default draw mode set before it, the dithering one after (PC_ITEM_BRACKET). */
void vs_battle_itemBracket(int item)
{
    pc_item* it = pc_frame_item(item);
    if (it)
        it->flags |= PC_ITEM_BRACKET;
}
/* The textured polygon item drawn after the draw mode `e1` (GP0 E1): the packet that began with that word (PC_ITEM_BEFORE). */
void vs_battle_itemBefore(int item, int e1)
{
    pc_item* it = pc_frame_item(item);
    if (it)
        it->flags |= PC_ITEM_BEFORE, it->before = (unsigned short)(e1 & 0x3fff);
}
/* The item left with the draw mode `e1` (GP0 E1) set after it (PC_ITEM_AFTER). */
void vs_battle_itemAfter(int item, int e1)
{
    pc_item* it = pc_frame_item(item);
    if (it)
        it->flags |= PC_ITEM_AFTER, it->after = (unsigned short)(e1 & 0x3fff);
}
/* SetDrawMode's DR_MODE: the draw mode (E1 bits) and, when `window` is not -1, the texture window (E2's 20 bits). */
void vs_battle_drawModeItem(unsigned* ot, int e1, int window)
{
    unsigned* cursor = cursor_get();
    int first = pc_frame_count();
    pc_item* it = pc_frame_add(PC_ITEM_MODE_SET, 0);
    if (it) {
        it->mode = e1 == -1 ? PC_ITEM_INHERIT : (unsigned short)(e1 & 0x3fff); /* (-1: the texture window alone, no draw mode word) */
        if (window != -1)
            it->flags |= PC_ITEM_WINDOW, it->tw = (unsigned)window & 0xfffffu;
    }
    place(ot, cursor, first);
}
/* The drawing area (x, y, w, h) and, when `offset`, the draw offset (ox, oy): GP0 E3, E4 and E5. */
void vs_battle_areaItem(unsigned* ot, int x, int y, int w, int h, int offset, int ox, int oy)
{
    unsigned* cursor = cursor_get();
    int first = pc_frame_count();
    pc_item* it = pc_frame_add(PC_ITEM_AREA, 0);
    if (it) {
        it->x[0] = (short)x, it->y[0] = (short)y, it->w = (short)w, it->h = (short)h;
        if (offset)
            it->flags |= PC_ITEM_OFFSET, it->x[1] = (short)ox, it->y[1] = (short)oy;
    }
    place(ot, cursor, first);
}
/* DR_STP: the mask bit set when drawing (E6). */
void vs_battle_maskItem(unsigned* ot, int on)
{
    unsigned* cursor = cursor_get();
    int first = pc_frame_count();
    pc_item* it = pc_frame_add(PC_ITEM_MASK, 0);
    if (it)
        it->x[0] = (short)(on != 0);
    place(ot, cursor, first);
}

/* A talk bubble's textured face as an item (vs_menu_drawTextBoxFrame): a flat-coloured triangle or quad (`verts`) with its texels and the page
 * 0x17 and CLUT 0x373E it names, placed at the table entry `ot` where its packet linked. `xy` and `uv` (u low, v high) per corner. */
void vs_battle_bubbleFace(unsigned* ot, int verts, int rgb, const int* xy, const int* uv)
{
    unsigned* cursor = cursor_get();
    int first = pc_frame_count();
    pc_item* it = pc_frame_add(PC_ITEM_POLY, 0);
    if (it) {
        it->verts = (unsigned char)verts, it->flags = PC_ITEM_TEXTURED, it->mode = 0x17, it->clut = 0x373E;
        it->rgb[0][0] = (unsigned char)rgb, it->rgb[0][1] = (unsigned char)(rgb >> 8), it->rgb[0][2] = (unsigned char)(rgb >> 16);
        for (int i = 0; i < verts; i++) {
            it->x[i] = (short)xy[i], it->y[i] = (short)(xy[i] >> 16);
            it->u[i] = (unsigned char)uv[i], it->v[i] = (unsigned char)(uv[i] >> 8);
        }
    }
    cursor_set(pc_frame_place(first, pc_frame_count() - first, ot, cursor));
}

/* The face's shadow: a black polygon and its outline (the lines the polyline drew: corner 0, 1, 1 (a triangle) or 3, 2, 0), in
 * whatever draw mode the GPU is in. `xy` already offset. */
void vs_battle_bubbleShadow(unsigned* ot, int verts, const int* xy)
{
    unsigned* cursor = cursor_get();
    int first = pc_frame_count();
    int ring[5] = { 0, 1, verts == 4 ? 3 : 1, 2, 0 };
    /* (drawn in reverse of the order added: the outline, then the polygon) */
    for (int k = 3; k >= 0; k--) {
        pc_item* line = pc_frame_add(PC_ITEM_LINE, 0);
        if (line) {
            line->mode = PC_ITEM_INHERIT;
            line->x[0] = (short)xy[ring[k]], line->y[0] = (short)(xy[ring[k]] >> 16);
            line->x[1] = (short)xy[ring[k + 1]], line->y[1] = (short)(xy[ring[k + 1]] >> 16);
        }
    }
    pc_item* poly = pc_frame_add(PC_ITEM_POLY, 0);
    if (poly) {
        poly->verts = (unsigned char)verts, poly->mode = PC_ITEM_INHERIT;
        for (int i = 0; i < verts; i++)
            poly->x[i] = (short)xy[i], poly->y[i] = (short)(xy[i] >> 16);
    }
    cursor_set(pc_frame_place(first, pc_frame_count() - first, ot, cursor));
}

short vs_battle_aiPackActorAiPersistenceFlags(int id);


/* 7C4F4.s: decide whether action slot (row, column) is usable and record it in
 * bit 7 of the slot's flags. Type 1 checks a signed 16-bit resource and a
 * blocking flag; type 3 compares cost plus half the high stat against the low
 * stat; type 4 checks a second signed resource. A 0x2000 state allows only
 * types 1 and 2. Rows with a count below 2 are never usable. */
int vs_battle_aiRefreshAiActionUsability(void* state, int row, int column);


extern void vs_battle_aiApplyActorActionReaction(void*, void*);
extern void vs_battle_aiPropagateActorRelationships(void*);
/* 73C84.s: run vs_battle_aiApplyActorActionReaction for every actor, then vs_battle_aiPropagateActorRelationships for every
 * actor (the second pass also receives the context in $a1, which it ignores).
 * The original walks the list as a do-while; an empty list would fault. */
void vs_battle_aiApplyActionRelationshipChanges(void* context);


/* 73B0C.s: copy an actor's stat words (0x18, 0x1c, 0x30, 0x948) and the
 * 0x528-byte block at 0x398 from src to dst, tail-calling vs_main_memcpy. */
void vs_battle_aiCopyPlanningActorStats(void* src, void* dst);


/* 7C4F4.s: derive three condition bytes from the actor's paired 16-bit stats
 * (low = current, high = maximum) and the state threshold at 0x17:
 * 0x12 = low1 * 2 < high1; 0x11 = threshold and (low2 < threshold or
 * low2 * 2 < high2); 0x10 = status flags. The original loads 0x90000 with lui
 * and then overwrites it with ori $zero, so the mask is 0xE2B0. */
void vs_battle_aiRefreshAiResourceRestrictions(void* state);


/* 73C24.s: snapshot each listed actor's id and stat words into a 0x18-byte
 * record indexed by id. Like the original do-while, an empty list faults. */
void vs_battle_aiCaptureActorConditionTable(void* snapshots);

void vs_battle_aiCopyActionCandidate(void* dst, void* src);


/* 73C84.s: bit 15 (vs_battle_aiIsActionHarmfulToActor) or 14 (vs_battle_aiIsActionBeneficialToActor) of the action's
 * target flags. For action 0x60 against an actor of class 2 the original
 * returns the whole shifted halfword xor 1, not just the inverted bit. */
int vs_battle_aiIsActionHarmfulToActor(int action, int id);

int vs_battle_aiIsActionBeneficialToActor(int action, int id);


extern int D_800F58F0;
extern unsigned D_800F58F4;
extern unsigned D_800F58F8; /* a 32-bit guest address */
/* 73C84.s: remember the first candidate for each action slot whose action has
 * flag 0x40, up to 16 entries. The slot bit is set even when the list is full. */
void vs_battle_aiCacheFallbackActionCandidate(void* candidate, void* info);


/* 7C4F4.s: whether a candidate may target actor id. Requires a peer and a
 * relation >= 2; candidate bit 5 adds a relation < 3 check against the action's
 * target mask (unless action flag 0x100); bit 4 rejects relation < 3; a slot
 * flag bit 1 rejects targeting oneself. */
int func_800E4CF4(void* state, void* candidate, int id);


extern unsigned D_800F5904; /* a 32-bit guest address */
/* 73B0C.s: append a scored candidate (up to 16, nonzero weight) to the state's
 * list at 0x2F0, record its pointer at 0x430, and mark (target, action) used. */
void vs_battle_aiAppendActionCandidate(void* state, void* candidate);


/* 7E05C.s vs_core_sortPointersByDescendingKey: K&R quicksort of candidate pointers by their first
 * word (score), descending. The original packs left/right into bytes when
 * saving them, which is lossless for the 16-entry lists it sorts. The list
 * holds 4-byte guest pointers (PS1 layout of the AI record). */
extern int rand(void);
/* 73C84.s: give the first `count` candidates (0x430) one chance each, in
 * order, to take the lead: a candidate with a nonzero byte at bit 5 of word 4
 * wins when rand() & 0x7F is below its weight (bits 19-26 of word 0xC,
 * capped at 0x4C); it swaps with the first entry and the rest are re-sorted
 * (L800E6958, the vs_core_sortPointersByDescendingKey quicksort from index 1). */
extern void vs_battle_aiSetTemporaryAwarenessIndicator(void*, int, int);
extern void func_800E48A8(void*, int, int);
/* 73C84.s: mark target `id` (word 0x1A4 + id * 4) as classification 3 and
 * clear bits 8-14. Unless it already was, the actor first reacts: byte 0x18
 * is set (the original's rand() & 0x7F < 0x80 always holds, but rand is
 * still drawn), then vs_battle_aiSetTemporaryAwarenessIndicator(6, 0x28) and func_800E48A8(2, 0x80). */
void vs_battle_aiMakeTargetHostile(void* state, int id);


extern int vs_battle_aiUpdatePatrolCombatBehavior(void*);
extern void vs_battle_aiFindNearestActiveWaypoint(void*, void*);
/* 70D4C.s: choose the movement mode at 0x12E. Actors already running
 * vs_battle_aiUpdatePatrolCombatBehavior, or whose best candidate's flags miss the mask at 0x2D, use
 * mode 1; otherwise mode 2 and a waypoint search into 0x134. */
void vs_battle_aiSelectActionMovementMode(void* state);


extern unsigned D_800F58B8;
extern unsigned D_800F590C;
extern int D_800F58D4;
extern void vs_battle_aiCacheActorAiWorkingState(void); /* pc/src/actor_update.c */
extern void vs_battle_aiUpdatePerceivingActor(void);
extern void vs_battle_aiUpdateActorDecisions(void);
extern void vs_battle_aiAnimateEngagementPhase(void);
extern void vs_battle_aiSaveMovementRandomState(void);
/* 7C7E0.s: per-frame AI driver. The original walks the null-terminated
 * routine table D_800F16BC: vs_battle_aiCacheActorAiWorkingState, 0x800E50C8 (advance to the next
 * active actor), 0x800E5100 (move $sp to the scratchpad at 0x1F800374),
 * vs_battle_aiUpdatePerceivingActor, vs_battle_aiUpdateActorDecisions, vs_battle_aiAnimateEngagementPhase, 0x800E5110 (restore $sp) and
 * vs_battle_aiSaveMovementRandomState. The table is fixed data, so it is called directly here; the
 * stack switch is a speed trick and is not reproduced (the host stack does not
 * overwrite scratchpad below 0x374 as the original's frames would). */
void func_800E511C(void);


extern void vs_battle_setActorMarker(int, int);
/* 6FA0C.s: forward (id, value) to vs_battle_setActorMarker for nonzero actor ids. For id 0
 * the original falls into vs_battle_aiIsEngagementReady's tail and returns 1; otherwise $v0 is
 * whatever vs_battle_setActorMarker leaves (callers ignore it). */
int vs_battle_aiSetActorAwarenessIndicator(int id, int value);

int vs_battle_aiIsEngagementReady(void* state);

/* 6FA6C.s: count down the timer at 0x12A. On reaching zero send 0 (L800D8254),
 * otherwise send the halfword at 0x138, both for the state's actor id. */
void vs_battle_aiUpdateTemporaryAwarenessIndicator(void* state);


/* 7B5DC.s: count down the two per-actor byte timers at 0x155 and 0x13F,
 * stopping at zero. */
void vs_battle_aiTickActorDecisionTimers(void* state);


extern int vs_core_mathSquaredVectorLength(int, int, int);
/* 7BDD4.s: squared distance between two short XYZ positions, with height
 * offsets added to each Y; falls through into vs_core_mathSquaredVectorLength (GTE SQR). */
int vs_core_mathSquaredDistanceWithHeightOffsets(short* a, short* b, int a_height, int b_height);


/* 7B5DC.s: whether height y is above the floor of the navigation cell at
 * (x >> 7, z >> 7) in the grid (64-byte rows) cached at scratchpad 0x3BC. A cell with
 * bit 13 set has floor 0; otherwise floor = sign15(sign((cell & 0x3FF) << 6)
 * - ((cell & 0x1C00) >> 3)). Returns 1 when y < floor. */
int vs_field_isPositionAboveGround(int x, int y, int z);

/* 7B5DC.s: sample 16 points from `from` toward `to` (each Y lowered by its
 * height argument, X read as unsigned 16-bit) in steps of delta >> 4; return 1
 * as soon as a sample has y >= floor (vs_field_isPositionAboveGround returns 0; Y grows down). */
int vs_field_isTerrainSegmentBlocked(short* from, short* to, int from_height, int to_height);

