/* Typed draw items (draw_items.h). The frame's list is saved state: a save state
 * taken after a frame is built and before it is drawn (the game waits for the
 * vsync between them) keeps it. */
#include <stdint.h>
#include "draw_items.h"
#include "frame_cost.h"
#include "snapshot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Items one frame can hold. The credits' busiest frame has about 600; a battle frame's effects have more. */
#define ITEMS 8192
#define MARKER 0x00495449u /* "ITI": a GP0 no-op word, as render.c's markers are */

typedef struct {
    int count, full;
    pc_item item[ITEMS];
} frame_list;
static frame_list pc_item_frame PC_SAVED_STATE = { 0 };

/* The last finished frame's items (kept at pc_frame_begin; host scratch, not state): the game waits for the vsync after
 * a frame's packets are written and before the next frame's items start, so a reader of the guest's packets then (the
 * dev RPC) finds their items here, the list at that moment being the new frame's. */
static frame_list* last_frame;
static int viewing_last;
static const frame_list* view(void) { return viewing_last && last_frame ? last_frame : &pc_item_frame; }
void pc_frame_view_last(int on) { viewing_last = on; }

/* The frame's drawing bookkeeping, one object (pc_frame_book): the markers placed this frame (their packet, the entry of an ordering table they
 * are placed at, the run), for the anchors that name packet memory and the order the walk of a table draws in; the ordering tables made
 * this frame (ClearOTag, ClearOTagR: a placed run's slot is its entry's place in the table it lies in, and the table's direction says
 * which slot the walk reaches first); a table drawn where an entry of another is (pc_frame_table_after: the text boxes' page of the table).
 * Saved state, as the items are: a save state resumed mid-frame has the frame's items back, and the order is read from this. It is the frame's
 * output, not the game's state: the state parity gates compare a game run and a run of the classic layer, which build the same
 * picture with other bookkeeping, so state_diff.c leaves this object out of what it compares. */
#define TABLES 8
static struct {
    struct {
        const void* packet;
        const uint32_t* entry; /* the table entry it is placed at */
        int first, count;
        int tail; /* appended at the end of the slot's chain (pc_frame_move), not at its head */
    } markers[ITEMS];
    int marker_count;
    struct {
        const uint32_t* base;
        int n, reverse;
    } tables[TABLES];
    int table_next;
    struct {
        const uint32_t *at, *head;
    } splice_of[4];
} book PC_SAVED_STATE;
#define markers book.markers
#define marker_count book.marker_count
#define tables book.tables
#define table_next book.table_next
#define splice_of book.splice_of
void pc_frame_book(void** start, size_t* size)
{
    *start = &book, *size = sizeof book;
}
void pc_frame_splice_table(const void* at, const void* head)
{
    for (int i = 0; i < 4; i++)
        if (!splice_of[i].at || splice_of[i].at == at) {
            splice_of[i].at = at, splice_of[i].head = head;
            return;
        }
}
void pc_frame_table(const void* base, int n, int reverse)
{
    const uint32_t* end = (const uint32_t*)base + n;
    int at = -1;
    /* (a table made over a range that overlaps another's, as the room teardown clears a sub-range of the frame's table, replaces it:
     * what was linked in the old one is gone with its links, and a lookup by head finds the one table) */
    for (int i = 0; i < TABLES; i++) {
        if (!tables[i].base || tables[i].base >= end || tables[i].base + tables[i].n <= (const uint32_t*)base)
            continue;
        for (int m = 0; m < marker_count; m++)
            if (markers[m].entry >= tables[i].base && markers[m].entry < tables[i].base + tables[i].n)
                markers[m].entry = NULL;
        if (at < 0)
            at = i;
        else
            tables[i].base = NULL;
    }
    if (at < 0)
        for (int i = 0; i < TABLES && at < 0; i++)
            if (!tables[i].base)
                at = i;
    if (at < 0)
        at = table_next++ % TABLES;
    tables[at].base = base, tables[at].n = n, tables[at].reverse = reverse;
}
/* Whether a run still has an item that draws (the others were suppressed: kind none). */
int pc_frame_run_live(int first, int count)
{
    for (int i = first; i < first + count && i < pc_item_frame.count; i++)
        if (pc_item_frame.item[i].kind != PC_ITEM_NONE)
            return 1;
    return 0;
}
/* The runs placed in the table `head` (a table drawn: pc_frame_table_draw's argument) lies in, in the order they draw: the slot the table's
 * drawing reaches first (a reverse table: the last), and at one slot the run placed last first (it was at the slot's head), those appended
 * at the end of a slot after them, and a spliced table's runs where the drawing reaches the entry it is drawn after (pc_frame_table_after),
 * before the runs placed there. The slots before the head are not reached; suppressed runs (kind none) draw nothing and are left out.
 * Returns how many runs, or -1 when no table holds `head`; pc_frame_table_run reads them. */
static int order_marker[ITEMS];
static const uint32_t* order_at[ITEMS]; /* (a spliced table's run: the entry it is drawn after) */
typedef struct {
    long key;
    int marker;
    const uint32_t* at;
} keyed;
static int keyed_cmp(const void* a, const void* b)
{
    const keyed *x = a, *y = b;
    return x->key < y->key ? -1 : x->key > y->key;
}
int pc_frame_table_order(const void* head)
{
    const uint32_t* h = head;
    int t = -1, n = 0;
    for (int i = 0; i < TABLES; i++)
        if (tables[i].base && h >= tables[i].base && h < tables[i].base + tables[i].n)
            t = i;
    if (t < 0)
        return -1;
    static keyed run[ITEMS];
    for (int i = 0; i < marker_count && n < ITEMS; i++) {
        const uint32_t *e = markers[i].entry, *at = NULL;
        if (!e || !pc_frame_run_live(markers[i].first, markers[i].count))
            continue;
        long slot, sub;
        if (e >= tables[t].base && e < tables[t].base + tables[t].n) {
            slot = e - tables[t].base;
            sub = markers[i].tail ? (1L << 19) + ITEMS + i : (1L << 19) + (ITEMS - 1 - i);
        } else { /* a spliced table's run: at the entry the table is drawn after */
            slot = -1;
            for (int sp = 0; sp < 4 && slot < 0; sp++) {
                int bt = -1;
                for (int b = 0; b < TABLES; b++)
                    if (tables[b].base && b != t && splice_of[sp].head >= tables[b].base && splice_of[sp].head < tables[b].base + tables[b].n && e >= tables[b].base && e < tables[b].base + tables[b].n)
                        bt = b;
                if (bt >= 0 && splice_of[sp].at >= tables[t].base && splice_of[sp].at < tables[t].base + tables[t].n) {
                    slot = splice_of[sp].at - tables[t].base;
                    at = splice_of[sp].at;
                    sub = (long)(tables[bt].reverse ? tables[bt].n - 1 - (e - tables[bt].base) : e - tables[bt].base) * ITEMS + (ITEMS - 1 - i);
                }
            }
            if (slot < 0)
                continue;
        }
        long from = h - tables[t].base;
        if (tables[t].reverse ? slot > from : slot < from) /* (drawing starts at `head`: the slots before it are not reached) */
            continue;
        run[n].key = ((tables[t].reverse ? -slot : slot) << 20) + sub, run[n].marker = i, run[n].at = at;
        n++;
    }
    qsort(run, (size_t)n, sizeof run[0], keyed_cmp);
    for (int i = 0; i < n; i++)
        order_marker[i] = run[i].marker, order_at[i] = run[i].at;
    return n;
}
/* Run `k` of the last pc_frame_table_order: its marker packet, the entry it is placed at and (a spliced table's run) the entry of the
 * table drawn that its table is drawn after, else NULL. */
void pc_frame_table_run(int k, const void** packet, const void** entry, const void** at)
{
    *packet = markers[order_marker[k]].packet, *entry = markers[order_marker[k]].entry, *at = order_at[k];
}
/* The tripwire for a packet written into a table by hand: the frame's tables are made empty (pc_frame_table_clear writes no link) and
 * nothing is linked into them, so at the draw a slot with a link in it, or a marker whose tag got one, is a screen that still builds a
 * packet and links it. It would draw nothing now: logged (once per process; pc/tests/run.py and pc/tools/build_compare.py fail a suite or a
 * lane whose game log holds the line), the first slot's word with it. Returns how many linked words were found. */
int pc_frame_table_check(const void* head)
{
    static int told;
    const uint32_t* h = head;
    int found = 0;
    for (int t = 0; t < TABLES; t++) {
        if (!tables[t].base || h < tables[t].base || h >= tables[t].base + tables[t].n)
            continue;
        for (int i = 0; i < tables[t].n; i++)
            if (tables[t].base[i] & 0xffffff) {
                if (!found && !told++)
                    fprintf(stderr, "[ot] a raw packet is linked in a table: slot %d holds %08x\n", i, tables[t].base[i]);
                found++;
            }
    }
    for (int i = 0; i < marker_count; i++)
        if (markers[i].entry && (*(const uint32_t*)markers[i].packet & 0xffffff)) {
            if (!found && !told++)
                fprintf(stderr, "[ot] a raw packet is linked in a table: after a marker (%08x)\n", *(const uint32_t*)markers[i].packet);
            found++;
        }
    return found;
}

/* Developer coverage: the call sites that placed an item, by return address (dev_rpc.c effect_sites). */
#define SITES 4096
static int sites_on, sites_count, peak;
static const void* sites[SITES];

/* boot_runtime.c: one lowered packet drawn as a member of an ordering table. */
extern void pc_draw_item_packet(void* packet);
extern void pc_hwc_frame_room(void); /* hw_cmd.c */
extern void pc_hwc_begin_ot(void); /* hw_cmd.c */
extern void pc_hwc_end_ot(void);
extern void pc_gte_new_generation(void);

void pc_frame_begin(void)
{
    if (pc_item_frame.count > peak)
        peak = pc_item_frame.count;
    if (!last_frame)
        last_frame = calloc(1, sizeof *last_frame);
    if (last_frame) {
        last_frame->count = pc_item_frame.count;
        memcpy(last_frame->item, pc_item_frame.item, (size_t)pc_item_frame.count * sizeof pc_item_frame.item[0]);
    }
    pc_item_frame.count = 0;
    pc_item_frame.full = 0;
    marker_count = 0;
    memset(splice_of, 0, sizeof splice_of);
}

static pc_item spare; /* where a full frame's item goes: never drawn */

pc_item* pc_frame_add(int kind, int depth)
{
    if (pc_item_frame.count == ITEMS) {
        if (!pc_item_frame.full++)
            fprintf(stderr, "[items] a frame holds %d items: the rest are left out\n", ITEMS);
        return NULL;
    }
    pc_item* it = &pc_item_frame.item[pc_item_frame.count++];
    memset(it, 0, sizeof *it);
    it->kind = (uint8_t)kind;
    it->depth = depth;
    return it;
}

void pc_frame_tile(int depth, int x, int y, int w, int h, int rgb, int mode, int flags)
{
    pc_item* it = pc_frame_add(PC_ITEM_TILE, depth);
    if (!it)
        return;
    it->flags = (uint8_t)flags;
    it->mode = (uint16_t)mode;
    it->x[0] = (int16_t)x, it->y[0] = (int16_t)y, it->w = (int16_t)w, it->h = (int16_t)h;
    it->rgb[0][0] = (uint8_t)rgb, it->rgb[0][1] = (uint8_t)(rgb >> 8), it->rgb[0][2] = (uint8_t)(rgb >> 16);
}

void pc_frame_sprite(int depth, int x, int y, int w, int h, int u, int v, int rgb, int clut, int mode, int flags)
{
    pc_item* it = pc_frame_add(PC_ITEM_SPRITE, depth);
    if (!it)
        return;
    it->flags = (uint8_t)flags;
    it->mode = (uint16_t)mode;
    it->clut = (uint16_t)clut;
    it->x[0] = (int16_t)x, it->y[0] = (int16_t)y, it->w = (int16_t)w, it->h = (int16_t)h;
    it->u[0] = (uint8_t)u, it->v[0] = (uint8_t)v;
    it->rgb[0][0] = (uint8_t)rgb, it->rgb[0][1] = (uint8_t)(rgb >> 8), it->rgb[0][2] = (uint8_t)(rgb >> 16);
}

int pc_frame_count(void) { return pc_item_frame.count; }

/* The classic layer's site range (draw_items.h). */
static int site_first, site_end;
void pc_frame_site(int first, int end) { site_first = first, site_end = end; }
void pc_frame_site_range(int* first, int* end) { *first = site_first, *end = site_end; }

int pc_frame_suppress(int first, int end)
{
    int n = 0;
    for (int i = first < 0 ? 0 : first; i < end && i < pc_item_frame.count; i++)
        if (pc_item_frame.item[i].kind != PC_ITEM_NONE)
            pc_item_frame.item[i].kind = PC_ITEM_NONE, n++;
    return n;
}

void pc_frame_rewind(int count)
{
    if (count >= 0 && count < pc_item_frame.count)
        pc_item_frame.count = count, pc_item_frame.full = 0;
    /* (the markers of the dropped items go too: their packets lie in memory the game uses again, and a reader that looks a range
     * of packets up by address would find them) */
    int kept = 0;
    for (int i = 0; i < marker_count; i++) {
        if (markers[i].first >= pc_item_frame.count)
            continue;
        if (markers[i].first + markers[i].count > pc_item_frame.count)
            markers[i].count = pc_item_frame.count - markers[i].first;
        markers[kept++] = markers[i];
    }
    marker_count = kept;
}

int pc_frame_is_placed(int index)
{
    return index >= 0 && index < pc_item_frame.count && (pc_item_frame.item[index].flags & PC_ITEM_PLACED) != 0;
}

/* Raw GP0 words as items (draw_items.h): the inverse of `lower`, with the draw mode and texture window set along the way. */
int pc_frame_add_packets(int depth, const unsigned* w, int n, int* first)
{
    int start = pc_item_frame.count, i = 0, mode = -1, window = -1, prims = 0, state_after = 0, last_mode = -1, last_window = -1;
    pc_item* last = NULL;
    *first = start;
    while (i < n) {
        unsigned c = w[i] >> 24;
        pc_item* it;
        if (c == 0xe1) {
            mode = (int)(w[i] & 0x3fff), state_after = 1, i++;
            continue;
        }
        if (c == 0xe2) {
            window = (int)(w[i] & 0xfffff), state_after = 1, i++;
            continue;
        }
        state_after = 0;
        if (c == 0xe3 && i + 1 < n && w[i + 1] >> 24 == 0xe4) {
            int x = (int)(w[i] & 0x3ff), y = (int)(w[i] >> 10 & 0x3ff), x1 = (int)(w[i + 1] & 0x3ff), y1 = (int)(w[i + 1] >> 10 & 0x3ff);
            if (!(it = pc_frame_add(PC_ITEM_AREA, depth)))
                goto fail;
            it->x[0] = (int16_t)x, it->y[0] = (int16_t)y, it->w = (int16_t)(x1 - x + 1), it->h = (int16_t)(y1 - y + 1);
            i += 2, prims++;
            last_mode = mode, last_window = window, last = it;
            continue;
        }
        if (c >= 0x20 && c < 0x40) {
            int quad = (c & 8) != 0, tex = (c & 4) != 0, shade = (c & 0x10) != 0, verts = quad ? 4 : 3, k = i;
            if (i + (shade ? verts : 1) + verts + (tex ? verts : 0) > n || !(it = pc_frame_add(PC_ITEM_POLY, depth)))
                goto fail;
            it->verts = (uint8_t)verts;
            it->flags = (uint8_t)((c & 2 ? PC_ITEM_SEMI : 0) | (c & 1 ? PC_ITEM_RAW : 0) | (tex ? PC_ITEM_TEXTURED : 0)
                | (shade ? PC_ITEM_SHADED : 0));
            for (int v = 0; v < verts; v++) {
                if (v == 0 || shade) {
                    it->rgb[v][0] = (uint8_t)w[k], it->rgb[v][1] = (uint8_t)(w[k] >> 8), it->rgb[v][2] = (uint8_t)(w[k] >> 16);
                    k++;
                }
                it->x[v] = (int16_t)w[k], it->y[v] = (int16_t)(w[k] >> 16);
                k++;
                if (tex) {
                    it->u[v] = (uint8_t)w[k], it->v[v] = (uint8_t)(w[k] >> 8);
                    if (v == 0)
                        it->clut = (uint16_t)(w[k] >> 16);
                    else if (v == 1)
                        it->mode = (uint16_t)(w[k] >> 16);
                    k++;
                }
            }
            if (!tex)
                it->mode = mode < 0 ? PC_ITEM_INHERIT : (uint16_t)mode;
            else if (mode >= 0)
                it->flags |= PC_ITEM_BEFORE, it->before = (uint16_t)mode; /* (the draw mode its dither bits come from) */
            i = k, prims++;
        } else if ((c & 0xfc) == 0x60 || (c & 0xfc) == 0x64) { /* a tile or a sprite, of a size it carries */
            int tex = (c & 4) != 0, need = tex ? 4 : 3;
            if (i + need > n || !(it = pc_frame_add(tex ? PC_ITEM_SPRITE : PC_ITEM_TILE, depth)))
                goto fail;
            it->flags = (uint8_t)((c & 2 ? PC_ITEM_SEMI : 0) | (c & 1 ? PC_ITEM_RAW : 0));
            it->mode = mode < 0 ? PC_ITEM_INHERIT : (uint16_t)mode;
            it->rgb[0][0] = (uint8_t)w[i], it->rgb[0][1] = (uint8_t)(w[i] >> 8), it->rgb[0][2] = (uint8_t)(w[i] >> 16);
            it->x[0] = (int16_t)w[i + 1], it->y[0] = (int16_t)(w[i + 1] >> 16);
            if (tex)
                it->u[0] = (uint8_t)w[i + 2], it->v[0] = (uint8_t)(w[i + 2] >> 8), it->clut = (uint16_t)(w[i + 2] >> 16);
            it->w = (int16_t)w[i + need - 1], it->h = (int16_t)(w[i + need - 1] >> 16);
            i += need, prims++;
        } else if (c == 0x40 || c == 0x42 || c == 0x50 || c == 0x52) { /* a line, flat or shaded */
            int shaded = c >= 0x50, need = shaded ? 4 : 3;
            if (i + need > n || !(it = pc_frame_add(PC_ITEM_LINE, depth)))
                goto fail;
            it->flags = (uint8_t)((c & 2 ? PC_ITEM_SEMI : 0) | (shaded ? PC_ITEM_SHADED : 0));
            it->mode = mode < 0 ? PC_ITEM_INHERIT : (uint16_t)mode;
            it->rgb[0][0] = (uint8_t)w[i], it->rgb[0][1] = (uint8_t)(w[i] >> 8), it->rgb[0][2] = (uint8_t)(w[i] >> 16);
            it->x[0] = (int16_t)w[i + 1], it->y[0] = (int16_t)(w[i + 1] >> 16);
            if (shaded)
                it->rgb[1][0] = (uint8_t)w[i + 2], it->rgb[1][1] = (uint8_t)(w[i + 2] >> 8), it->rgb[1][2] = (uint8_t)(w[i + 2] >> 16);
            it->x[1] = (int16_t)w[i + need - 1], it->y[1] = (int16_t)(w[i + need - 1] >> 16);
            i += need, prims++;
        } else if (c == 0x48 || c == 0x4a) { /* a flat polyline (to the 0x5555 word): a line to each next vertex */
            int j = i + 1, verts;
            while (j < n && (w[j] & 0xf000f000u) != 0x50005000u)
                j++;
            verts = j - i - 1;
            if (j >= n || verts < 2)
                goto fail;
            for (int v = 0; v + 1 < verts; v++) {
                if (!(it = pc_frame_add(PC_ITEM_LINE, depth)))
                    goto fail;
                it->flags = (uint8_t)(c & 2 ? PC_ITEM_SEMI : 0);
                it->mode = mode < 0 ? PC_ITEM_INHERIT : (uint16_t)mode;
                it->rgb[0][0] = (uint8_t)w[i], it->rgb[0][1] = (uint8_t)(w[i] >> 8), it->rgb[0][2] = (uint8_t)(w[i] >> 16);
                it->x[0] = (int16_t)w[i + 1 + v], it->y[0] = (int16_t)(w[i + 1 + v] >> 16);
                it->x[1] = (int16_t)w[i + 2 + v], it->y[1] = (int16_t)(w[i + 2 + v] >> 16);
                if (window >= 0 && v + 2 < verts)
                    it->flags |= PC_ITEM_WINDOW, it->tw = (unsigned)window;
            }
            i = j + 1, prims++;
        } else
            goto fail;
        if (window >= 0)
            it->flags |= PC_ITEM_WINDOW, it->tw = (unsigned)window;
        last_mode = mode, last_window = window, last = it;
    }
    /* (a draw mode set after the last primitive stays with it; a window set after it is an item of its own) */
    if (!prims) { /* (state words alone: a draw mode and a texture window set, as the item that sets them) */
        pc_item* set;
        if ((mode < 0 && window < 0) || !(set = pc_frame_add(PC_ITEM_MODE_SET, depth)))
            goto fail;
        set->mode = mode < 0 ? PC_ITEM_INHERIT : (uint16_t)mode;
        if (window >= 0)
            set->flags |= PC_ITEM_WINDOW, set->tw = (unsigned)window;
        set->group = (unsigned short)(start + 1);
        return 1;
    }
    if (state_after && window != last_window) { /* (a texture window set after the last primitive: an item of its own, with the mode if it changed) */
        pc_item* set = pc_frame_add(PC_ITEM_MODE_SET, depth);
        if (!set)
            goto fail;
        set->mode = mode != last_mode ? (uint16_t)mode : PC_ITEM_INHERIT;
        if (window >= 0)
            set->flags |= PC_ITEM_WINDOW, set->tw = (unsigned)window;
    } else if (state_after && mode != last_mode)
        last->flags |= PC_ITEM_AFTER, last->after = (uint16_t)mode;
    for (int a = start; a < pc_item_frame.count; a++)
        pc_item_frame.item[a].group = (unsigned short)(start + 1);
    for (int a = start, b = pc_item_frame.count - 1; a < b; a++, b--) { /* (items of one depth draw last first) */
        pc_item t = pc_item_frame.item[a];
        pc_item_frame.item[a] = pc_item_frame.item[b];
        pc_item_frame.item[b] = t;
    }
    return pc_item_frame.count - start;
fail:
    pc_frame_rewind(start);
    return 0;
}

/* ---- runs made without words (draw_items.h): what pc_frame_add_packets makes of the words, item by item ---- */

void pc_frame_run_begin(pc_frame_run* run, int depth)
{
    *run = (pc_frame_run) { .start = pc_item_frame.count, .depth = depth, .mode = -1, .window = -1, .last_mode = -1, .last_window = -1 };
}
void pc_frame_run_state(pc_frame_run* run, int mode, int window)
{
    if (mode >= 0)
        run->mode = mode & 0x3fff;
    if (window >= 0)
        run->window = window & 0xfffff;
    run->state_after = 1;
}
static pc_item* run_item(pc_frame_run* run, int kind, int flags)
{
    pc_item* it = pc_frame_add(kind, run->depth);
    run->state_after = 0;
    if (!it) {
        run->bad = 1;
        return NULL;
    }
    it->flags = (uint16_t)flags;
    return it;
}
static pc_item* run_done(pc_frame_run* run, pc_item* it)
{
    if (run->window >= 0)
        it->flags |= PC_ITEM_WINDOW, it->tw = (unsigned)run->window;
    run->last_mode = run->mode, run->last_window = run->window, run->last = it;
    ++run->prims;
    return it;
}
pc_item* pc_frame_run_poly(pc_frame_run* run, int verts, int flags, int page, int clut)
{
    pc_item* it = run_item(run, PC_ITEM_POLY, flags);
    if (!it)
        return NULL;
    it->verts = (uint8_t)verts;
    if (flags & PC_ITEM_TEXTURED) {
        it->mode = (uint16_t)page, it->clut = (uint16_t)clut;
        if (run->mode >= 0)
            it->flags |= PC_ITEM_BEFORE, it->before = (uint16_t)run->mode;
    } else
        it->mode = run->mode < 0 ? PC_ITEM_INHERIT : (uint16_t)run->mode;
    return run_done(run, it);
}
pc_item* pc_frame_run_tile(pc_frame_run* run, int flags)
{
    pc_item* it = run_item(run, PC_ITEM_TILE, flags);
    if (!it)
        return NULL;
    it->mode = run->mode < 0 ? PC_ITEM_INHERIT : (uint16_t)run->mode;
    return run_done(run, it);
}
pc_item* pc_frame_run_line(pc_frame_run* run, int flags)
{
    pc_item* it = run_item(run, PC_ITEM_LINE, flags);
    if (!it)
        return NULL;
    it->mode = run->mode < 0 ? PC_ITEM_INHERIT : (uint16_t)run->mode;
    return run_done(run, it);
}
int pc_frame_run_end(pc_frame_run* run, int* first)
{
    int start = run->start;
    *first = start;
    if (run->bad)
        goto fail;
    if (!run->prims) { /* (state alone: one item that sets it) */
        pc_item* set;
        if ((run->mode < 0 && run->window < 0) || !(set = pc_frame_add(PC_ITEM_MODE_SET, run->depth)))
            goto fail;
        set->mode = run->mode < 0 ? PC_ITEM_INHERIT : (uint16_t)run->mode;
        if (run->window >= 0)
            set->flags |= PC_ITEM_WINDOW, set->tw = (unsigned)run->window;
        set->group = (unsigned short)(start + 1);
        return 1;
    }
    if (run->state_after && run->window != run->last_window) {
        pc_item* set = pc_frame_add(PC_ITEM_MODE_SET, run->depth);
        if (!set)
            goto fail;
        set->mode = run->mode != run->last_mode ? (uint16_t)run->mode : PC_ITEM_INHERIT;
        if (run->window >= 0)
            set->flags |= PC_ITEM_WINDOW, set->tw = (unsigned)run->window;
    } else if (run->state_after && run->mode != run->last_mode)
        run->last->flags |= PC_ITEM_AFTER, run->last->after = (uint16_t)run->mode;
    for (int a = start; a < pc_item_frame.count; a++)
        pc_item_frame.item[a].group = (unsigned short)(start + 1);
    for (int a = start, b = pc_item_frame.count - 1; a < b; a++, b--) { /* (items of one depth draw last first) */
        pc_item t = pc_item_frame.item[a];
        pc_item_frame.item[a] = pc_item_frame.item[b];
        pc_item_frame.item[b] = t;
    }
    return pc_item_frame.count - start;
fail:
    pc_frame_rewind(start);
    return 0;
}

pc_item* pc_frame_poly_at(int depth, int kind, int verts, int flags, unsigned* ot, unsigned** cursor)
{
    int index = pc_item_frame.count;
    pc_item* it = pc_frame_add(kind, depth);
    if (!it) {
        memset(&spare, 0, sizeof spare);
        return &spare;
    }
    it->verts = (uint8_t)verts;
    it->flags = (uint8_t)flags;
    if (ot) /* (a frame of items alone has no table to place it in) */
        *cursor = pc_frame_place(index, 1, ot + depth, *cursor);
    if (sites_on) {
        const void* site = __builtin_return_address(0);
        int i;
        for (i = 0; i < sites_count && sites[i] != site; i++)
            ;
        if (i == sites_count && sites_count < SITES)
            sites[sites_count++] = site;
    }
    return it;
}

/* The sites seen since recording began (return addresses); `on` starts or stops recording, `reset` clears.
 * `peak` is the most items one frame has held. */
int pc_frame_sites(int on, int reset, const void*** list, int* peak_items)
{
    sites_on = on;
    if (reset)
        sites_count = 0, peak = 0;
    *list = sites;
    *peak_items = peak;
    return sites_count;
}

/* ---- the lowering: an item as the GP0 packet the game used to write ---- */

#define STRIDE 20 /* words per item in the packet arena: a mode packet (2) and the primitive (at most 15) */
static uint32_t* arena;

const void* pc_frame_packet(int index)
{
    if (!arena && !(arena = malloc((size_t)ITEMS * STRIDE * sizeof *arena)))
        return NULL;
    return arena + (size_t)index * STRIDE;
}

void pc_frame_group(int first, int count)
{
    for (int i = first; i < first + count && i < pc_item_frame.count; i++) /* (the run's first index: the frame's state only) */
        pc_item_frame.item[i].group = (unsigned short)(first + 1);
}

void pc_frame_mode_item(int depth, int mode)
{
    pc_item* it = pc_frame_add(PC_ITEM_MODE_SET, depth);
    if (it)
        it->mode = (uint16_t)mode;
}

void pc_frame_anchor_markers(const void* start, const void* end, int anchor)
{
    /* (the runs of the markers in the range, joined where they touch: an anchor range is a scarce thing) */
    int lo = -1, hi = -1;
    for (;;) {
        int next = -1;
        for (int i = 0; i < marker_count; i++) {
            if (markers[i].packet < start || markers[i].packet >= end || markers[i].first + markers[i].count <= hi)
                continue;
            if (next < 0 || markers[i].first < markers[next].first)
                next = i;
        }
        if (next < 0)
            break;
        if (lo >= 0 && markers[next].first <= hi) {
            if (markers[next].first + markers[next].count > hi)
                hi = markers[next].first + markers[next].count;
        } else {
            if (lo >= 0)
                pc_frame_anchor(lo, hi, anchor);
            lo = markers[next].first, hi = lo + markers[next].count;
        }
    }
    if (lo >= 0)
        pc_frame_anchor(lo, hi, anchor);
}

int pc_frame_marker_range(const void* start, const void* end, int* first, int* last)
{
    int lo = -1, hi = -1;
    for (int i = 0; i < marker_count; i++)
        if (markers[i].packet >= start && markers[i].packet < end && markers[i].count > 0) {
            if (lo < 0 || markers[i].first < lo)
                lo = markers[i].first;
            if (markers[i].first + markers[i].count > hi)
                hi = markers[i].first + markers[i].count;
        }
    if (lo < 0)
        return 0;
    *first = lo, *last = hi;
    return 1;
}

int pc_frame_marker_span(const void* start, const void* end, const void** lo, const void** hi)
{
    int first = -1, last = -1;
    for (int i = 0; i < marker_count; i++)
        if (markers[i].packet >= start && markers[i].packet < end && markers[i].count > 0) {
            if (first < 0 || markers[i].first < first)
                first = markers[i].first;
            if (markers[i].first + markers[i].count > last)
                last = markers[i].first + markers[i].count;
        }
    *lo = *hi = NULL;
    if (first < 0 || !pc_frame_packet(first))
        return 0;
    *lo = pc_frame_packet(first);
    *hi = (const char*)pc_frame_packet(last - 1) + STRIDE * sizeof *arena;
    return 1;
}

int pc_frame_packet_words(int index, unsigned* out, int max)
{
    unsigned words[3][16];
    int lengths[3], n = 0, packets = pc_frame_lower(index, words, lengths);
    for (int k = 0; k < packets; k++) {
        if (n + 1 + lengths[k] > max)
            break;
        out[n++] = (unsigned)lengths[k] << 24;
        memcpy(out + n, words[k], (size_t)lengths[k] * sizeof words[k][0]);
        n += lengths[k];
    }
    return n;
}

void pc_frame_restyle(int first, int count, int mode, int clut, int semi)
{
    for (int i = first; i < first + count && i < pc_item_frame.count; i++) {
        pc_item* it = &pc_item_frame.item[i];
        if (it->kind != PC_ITEM_SPRITE)
            continue;
        it->mode = (uint16_t)(mode & 0x3fff), it->clut = (uint16_t)clut;
        it->flags = (uint16_t)(semi ? it->flags | PC_ITEM_SEMI : it->flags & ~PC_ITEM_SEMI);
    }
}

pc_item* pc_frame_item(int index)
{
    return index >= 0 && index < pc_item_frame.count ? &pc_item_frame.item[index] : NULL;
}
/* (the primitive's tag is word 2 of an item's 20, its command word 3) */
const void* pc_frame_command(int index)
{
    const uint32_t* at = pc_frame_packet(index);
    return at ? at + 3 : NULL;
}
int pc_frame_command_item(const void* command)
{
    const uint32_t* at = (const uint32_t*)command - 3;
    if (!arena || !command || at < arena || at >= arena + (size_t)pc_item_frame.count * STRIDE || (size_t)(at - arena) % STRIDE)
        return -1;
    return (int)((size_t)(at - arena) / STRIDE);
}

/* The anchor (and opacity) an item draws with are its own fields: a range of items sets them on its items, the innermost range
 * (the fewest items) winning, as the smallest range of packets did. They used to be ranges of the item arena's addresses in
 * the hardware path's tables, which outlived the frame whose items they named. */
extern int pc_hw_item_anchor(int anchor) __attribute__((weak));
void pc_frame_anchor(int first, int end, int anchor)
{
    if (end <= first) /* (an empty range records nothing: not even what a muted one reports) */
        return;
    int a = pc_hw_item_anchor ? pc_hw_item_anchor(anchor) : -1;
    if (a < 0)
        return;
    /* PC_ANCHOR_SLIDE (7, widescreen.h) is the weakest kind: the screen a slide belongs to is anchored as one, after the pieces that
     * name their own place (a range at a screen edge, a picture): those keep theirs, whichever range is the smaller. */
    const int slide = 7;
    for (int i = first; i < end && i < pc_item_frame.count; i++) {
        pc_item* it = &pc_item_frame.item[i];
        int held = it->flags & PC_ITEM_ANCHORED, held_slide = held && it->anchor == slide;
        if (!held || (a == slide ? 0 : held_slide || end - first < it->anchor_span)) {
            it->flags |= PC_ITEM_ANCHORED;
            it->anchor = (unsigned char)a;
            it->anchor_span = end - first;
        }
    }
}

/* Items [first, end) drawn at `opacity` (16.16: 0x10000 opaque), as the packets of a range were (the last range to name an item wins). */
int pc_frame_drawing_hidden; /* the item being drawn is PC_ITEM_HW_HIDDEN (boot_runtime.c) */
const float* pc_frame_drawing_span; /* the item being drawn's fill shape (boot_runtime.c), NULL none */
int pc_frame_drawing_anchor = -1, pc_frame_drawing_opacity = -1; /* the item being drawn's anchor and opacity (boot_runtime.c), -1 none */
void pc_frame_span(int item, int length, float top_left, float top_right, float bottom_left, float bottom_right)
{
    pc_item* it = item >= 0 && item < pc_item_frame.count ? &pc_item_frame.item[item] : NULL;
    if (!it)
        return;
    it->flags |= PC_ITEM_SPAN;
    it->span[0] = (float)length, it->span[1] = top_left, it->span[2] = top_right, it->span[3] = bottom_left, it->span[4] = bottom_right;
}
void pc_frame_opacity(int first, int end, int opacity)
{
    for (int i = first; i < end && i < pc_item_frame.count; i++) {
        pc_item* it = &pc_item_frame.item[i];
        it->flags |= PC_ITEM_OPACITY;
        it->opacity = (unsigned short)(opacity <= 0 ? 0 : opacity >= 0x10000 ? 256 : opacity >> 8);
    }
}

static uint32_t rgb_word(const pc_item* it, int i)
{
    return (uint32_t)it->rgb[i][0] | (uint32_t)it->rgb[i][1] << 8 | (uint32_t)it->rgb[i][2] << 16;
}
static uint32_t xy_word(const pc_item* it, int i)
{
    return (uint16_t)it->x[i] | (uint32_t)(uint16_t)it->y[i] << 16;
}

/* The words of `it` at `p` (after a tag word); returns how many. */
static unsigned lower(const pc_item* it, uint32_t* p)
{
    unsigned n = 0;
    uint32_t semi = it->flags & PC_ITEM_SEMI ? 2u : 0u, raw = it->flags & PC_ITEM_RAW ? 1u : 0u;
    if (it->flags & PC_ITEM_BRACKET) /* (dither off: a textured polygon's page is in its own words) */
        p[n++] = 0xe1000000u | (it->kind == PC_ITEM_LINE ? it->mode & 0x1ffu : 0u);
    switch (it->kind) {
    case PC_ITEM_MODE_SET:
        if (it->mode != PC_ITEM_INHERIT) /* (INHERIT: a texture window alone) */
            p[n++] = 0xe1000000u | it->mode;
        break;
    case PC_ITEM_NATIVE:
        p[n++] = it->verts ? PC_NATIVE_TRANSLUCENT_MARKER : PC_NATIVE_MARKER;
        p[n++] = it->native_list & 0xffffffu;
        if (it->verts) {
            p[n++] = (uint32_t)it->native_first & 0xffffffu;
            p[n++] = (uint32_t)it->native_count & 0xffffffu;
        }
        break;
    case PC_ITEM_COPY: /* (the words SetDrawMove wrote) */
        p[n++] = 0x01000000u;
        p[n++] = 0x80000000u;
        p[n++] = xy_word(it, 0);
        p[n++] = xy_word(it, 1);
        p[n++] = (uint16_t)it->w | (uint32_t)(uint16_t)it->h << 16;
        break;
    case PC_ITEM_MASK:
        p[n++] = 0xe6000000u | (it->x[0] ? 1u : 0u);
        break;
    case PC_ITEM_AREA:
        p[n++] = 0xe3000000u | ((uint32_t)it->y[0] & 0x3ffu) << 10 | ((uint32_t)it->x[0] & 0x3ffu);
        p[n++] = 0xe4000000u | ((uint32_t)(it->y[0] + it->h - 1) & 0x3ffu) << 10 | ((uint32_t)(it->x[0] + it->w - 1) & 0x3ffu);
        if (it->flags & PC_ITEM_OFFSET)
            p[n++] = 0xe5000000u | ((uint32_t)it->y[1] & 0x7ffu) << 11 | ((uint32_t)it->x[1] & 0x7ffu);
        break;
    case PC_ITEM_LINE:
        if (it->flags & PC_ITEM_SHADED) {
            p[n++] = rgb_word(it, 0) | (0x50u | semi) << 24;
            p[n++] = xy_word(it, 0);
            p[n++] = rgb_word(it, 1);
            p[n++] = xy_word(it, 1);
            break;
        }
        p[n++] = rgb_word(it, 0) | (0x40u | semi) << 24;
        p[n++] = xy_word(it, 0);
        p[n++] = xy_word(it, 1);
        break;
    case PC_ITEM_TILE:
        p[n++] = rgb_word(it, 0) | (0x60u | semi) << 24;
        p[n++] = xy_word(it, 0);
        p[n++] = (uint16_t)it->w | (uint32_t)(uint16_t)it->h << 16;
        break;
    case PC_ITEM_SPRITE:
        p[n++] = rgb_word(it, 0) | (0x64u | semi | raw) << 24;
        p[n++] = xy_word(it, 0);
        p[n++] = it->u[0] | (uint32_t)it->v[0] << 8 | (uint32_t)it->clut << 16;
        p[n++] = (uint16_t)it->w | (uint32_t)(uint16_t)it->h << 16;
        break;
    case PC_ITEM_POLY: {
        int quad = it->verts == 4, textured = (it->flags & PC_ITEM_TEXTURED) != 0, shaded = (it->flags & PC_ITEM_SHADED) != 0;
        uint32_t code = 0x20u | raw | semi | (textured ? 4u : 0u) | (quad ? 8u : 0u) | (shaded ? 0x10u : 0u);
        for (int i = 0; i < (quad ? 4 : 3); i++) {
            if (i == 0)
                p[n++] = rgb_word(it, 0) | code << 24;
            else if (shaded)
                p[n++] = rgb_word(it, i);
            p[n++] = xy_word(it, i);
            if (textured)
                p[n++] = it->u[i] | (uint32_t)it->v[i] << 8 | (i == 0 ? (uint32_t)it->clut << 16 : i == 1 ? (uint32_t)it->mode << 16 : 0u);
        }
        break;
    }
    }
    if (it->flags & PC_ITEM_BRACKET)
        p[n++] = 0xe1000200u;
    if (it->flags & PC_ITEM_AFTER)
        p[n++] = 0xe1000000u | it->after;
    return n;
}

/* Items [first, first + count) in draw order: larger depth first, then the later submission first. */
static int ordered(int first, int count, uint64_t* keys)
{
    const frame_list* f = view();
    int n = 0;
    for (int i = first; i < first + count && i < f->count; i++)
        if (f->item[i].kind != PC_ITEM_NONE)
            keys[n++] = (uint64_t)((int64_t)INT32_MAX - f->item[i].depth) << 32 | (uint32_t)(0xffffffffu - (uint32_t)i);
    for (int a = 1; a < n; a++) { /* (insertion sort: the list is nearly in order, a few depths) */
        uint64_t k = keys[a];
        int b = a;
        for (; b > 0 && keys[b - 1] > k; b--)
            keys[b] = keys[b - 1];
        keys[b] = k;
    }
    return n;
}

int pc_frame_order(int first, int count, int unplaced, int* out, int max)
{
    const frame_list* f = view();
    uint64_t keys[ITEMS];
    int n = ordered(first, count, keys), m = 0;
    for (int k = 0; k < n && m < max; k++) {
        int index = (int)(0xffffffffu - (uint32_t)keys[k]);
        if (!unplaced || !(f->item[index].flags & PC_ITEM_PLACED))
            out[m++] = index;
    }
    return m;
}

int pc_frame_lower(int index, unsigned words[3][16], int lengths[3])
{
    const frame_list* f = view();
    const pc_item* it;
    uint32_t tmp[16];
    unsigned n, packets = 0;
    if (index < 0 || index >= f->count || f->item[index].kind == PC_ITEM_NONE)
        return 0;
    it = &f->item[index];
    n = lower(it, tmp);
    if (it->flags & PC_ITEM_WINDOW) {
        words[packets][0] = 0xe2000000u | (it->tw & 0xfffffu);
        lengths[packets++] = 1;
    }
    if (it->kind == PC_ITEM_POLY && (it->flags & PC_ITEM_TEXTURED) && (it->flags & PC_ITEM_BEFORE)) {
        words[packets][0] = 0xe1000000u | it->before;
        lengths[packets++] = 1;
    } else if (it->kind != PC_ITEM_AREA && it->kind != PC_ITEM_MODE_SET && it->kind != PC_ITEM_MASK && it->kind != PC_ITEM_COPY && it->kind != PC_ITEM_NATIVE && it->mode != PC_ITEM_INHERIT && !(it->flags & PC_ITEM_BRACKET)
        && !(it->kind == PC_ITEM_POLY && (it->flags & PC_ITEM_TEXTURED))) {
        words[packets][0] = 0xe1000000u | it->mode;
        lengths[packets++] = 1;
    }
    memcpy(words[packets], tmp, n * sizeof tmp[0]);
    lengths[packets++] = (int)n;
    return (int)packets;
}

/* The items from `first` drawn at once, as one packet, by DrawPrim (the classic layer's recorder sees it, as it did
 * the packet a screen built by hand), then given back. */
extern void DrawPrim(void* packet);
static unsigned immediate_packet[1 + 8 * 48];
static const unsigned* immediate_build(int first)
{
    unsigned words[3][16];
    int lengths[3], n = 0, last = 0;
    for (int i = first; i < pc_item_frame.count; i++) {
        int packets = pc_frame_lower(i, words, lengths);
        for (int k = 0; k < packets && n + lengths[k] <= 8 * 48; k++) {
            memcpy(immediate_packet + 1 + n, words[k], (size_t)lengths[k] * sizeof words[k][0]);
            last = n;
            n += lengths[k];
        }
    }
    immediate_packet[0] = (unsigned)n << 24;
    return n ? immediate_packet + 1 + last : NULL;
}
const void* pc_frame_immediate_command(int first) { return immediate_build(first); }
void pc_frame_immediate_words(const unsigned* words, int n)
{
    static unsigned packet[1 + 16];
    if (n > 16)
        n = 16;
    memcpy(packet + 1, words, (size_t)n * sizeof *words);
    packet[0] = (unsigned)n << 24;
    DrawPrim(packet);
}
void pc_frame_immediate_run(int first)
{
    int count = pc_item_frame.count - first, index[ITEMS], n, last = 0, words_n = 0;
    uint64_t keys[ITEMS];
    unsigned words[3][16];
    int lengths[3];
    if (count > 0) {
        n = ordered(first, count, keys);
        for (int k = 0; k < n; k++) {
            int i = (int)(0xffffffffu - (uint32_t)keys[k]), packets = pc_frame_lower(i, words, lengths);
            index[k] = i;
            for (int p = 0; p < packets && words_n + lengths[p] <= 8 * 48; p++) {
                memcpy(immediate_packet + 1 + words_n, words[p], (size_t)lengths[p] * sizeof words[p][0]);
                last = words_n;
                words_n += lengths[p];
            }
        }
        (void)last;
        immediate_packet[0] = (unsigned)words_n << 24;
        if (words_n)
            DrawPrim(immediate_packet);
    }
    pc_frame_rewind(first);
}
void pc_frame_immediate(int first)
{
    if (immediate_build(first))
        DrawPrim(immediate_packet);
    pc_frame_rewind(first);
}

/* Draws a run. `mode` is the draw mode the GPU is in; a packet sets it only when an item needs another
 * (a textured polygon sets its page itself, as it did). */
static void draw_run(int first, int count)
{
    uint64_t keys[ITEMS];
    int n = ordered(first, count, keys), mode = -1;
    unsigned window = ~0u; /* (the texture window the GPU is in, when an item set it) */
    if (!arena && !(arena = malloc((size_t)ITEMS * STRIDE * sizeof *arena)))
        return;
    for (int k = 0; k < n; k++) {
        int index = (int)(0xffffffffu - (uint32_t)keys[k]);
        const pc_item* it = &pc_item_frame.item[index];
        uint32_t* at = arena + (size_t)index * STRIDE;
        int textured_poly = it->kind == PC_ITEM_POLY && (it->flags & PC_ITEM_TEXTURED);
        pc_frame_drawing_anchor = it->flags & PC_ITEM_ANCHORED ? it->anchor : -1;
        pc_frame_drawing_opacity = it->flags & PC_ITEM_OPACITY ? it->opacity : -1;
        pc_frame_drawing_span = it->flags & PC_ITEM_SPAN ? it->span : NULL;
        if ((it->flags & PC_ITEM_WINDOW) && it->tw != window) {
            at[18] = 0x01ffffffu; /* one word: GP0 E2, the texture window */
            at[19] = 0xe2000000u | (it->tw & 0xfffffu);
            window = (int)it->tw;
            pc_draw_item_packet(at + 18);
        }
        if (textured_poly && (it->flags & PC_ITEM_BEFORE) && it->before != mode) {
            at[0] = 0x01ffffffu; /* one word: GP0 E1, set before the polygon (its dither bits stay) */
            at[1] = 0xe1000000u | it->before;
            mode = it->before;
            pc_draw_item_packet(at);
        }
        if (it->kind == PC_ITEM_MODE_SET && it->mode != PC_ITEM_INHERIT)
            mode = it->mode; /* (its packet is the mode) */
        if (it->kind == PC_ITEM_AREA || it->kind == PC_ITEM_MODE_SET || it->kind == PC_ITEM_MASK || it->kind == PC_ITEM_COPY || it->kind == PC_ITEM_NATIVE) {
            /* (a state: no draw mode of its own) */
        } else if (it->flags & PC_ITEM_BRACKET)
            mode = 0x200; /* (its words end with the mode set) */
        else if (!textured_poly && it->mode != PC_ITEM_INHERIT && it->mode != mode) {
            at[0] = 0x01ffffffu; /* one word: GP0 E1, as SetDrawTPage wrote */
            at[1] = 0xe1000000u | it->mode;
            mode = it->mode;
            pc_draw_item_packet(at);
        } else if (textured_poly && mode >= 0)
            mode = (mode & ~0x1ff) | (it->mode & 0x1ff);
        unsigned words = lower(it, at + 3);
        at[2] = words << 24;
        pc_frame_drawing_hidden = (it->flags & PC_ITEM_HW_HIDDEN) != 0; /* (boot_runtime.c draw_packet leaves it out of the hardware picture) */
        pc_draw_item_packet(at + 2);
        pc_frame_drawing_hidden = 0;
        pc_frame_drawing_anchor = pc_frame_drawing_opacity = -1;
        pc_frame_drawing_span = NULL;
        if (it->flags & PC_ITEM_AFTER)
            mode = it->after; /* (left set after it) */
    }
}

void pc_frame_draw(void)
{
    uint64_t cost = pc_cost_begin();
    pc_hwc_frame_room(); /* (the room this frame is drawn in: emissive scopes) */
    pc_hwc_begin_ot();
    draw_run(0, pc_item_frame.count);
    pc_hwc_end_ot();
    pc_gte_new_generation();
    pc_cost_end(PC_COST_DRAW, cost);
}

/* ---- placing a run in an ordering table ---- */

uint32_t* pc_frame_place(int first, int count, uint32_t* ot_entry, uint32_t* packet)
{
    for (int i = first; i < first + count && i < pc_item_frame.count; i++)
        pc_item_frame.item[i].flags |= PC_ITEM_PLACED;
    if (marker_count < ITEMS)
        markers[marker_count].tail = 0, markers[marker_count].packet = packet, markers[marker_count].entry = ot_entry, markers[marker_count].first = first, markers[marker_count++].count = count;
    /* Not linked into the table: the entry is the run's key, where the table's drawing takes it in (pc_frame_table_order), and the readers
     * that followed the chain from an entry ask for the runs placed there (pc_frame_markers_at). */
    packet[0] = 3u << 24;
    packet[1] = MARKER;
    packet[2] = (uint32_t)first & 0xffffff;
    packet[3] = (uint32_t)count & 0xffffff;
    return packet + 4;
}

/* boot_runtime.c's walk: whether `packet` is a marker, and the run it names. */
int pc_frame_marker(const uint32_t* packet, int* first, int* count)
{
    if ((packet[0] >> 24) != 3 || packet[1] != MARKER)
        return 0;
    *first = (int)packet[2];
    *count = (int)packet[3];
    return 1;
}
void pc_frame_draw_run(int first, int count) { draw_run(first, count); }
int pc_frame_run_has_native(int first, int count)
{
    for (int i = first; i < first + count && i < pc_item_frame.count; i++)
        if (pc_item_frame.item[i].kind == PC_ITEM_NATIVE)
            return 1;
    return 0;
}
/* Whether a run has a textured item drawn from a page `wanted` takes (an item that inherits the mode: `current`). */
int pc_frame_run_pages(int first, int count, unsigned current, int (*wanted)(unsigned page))
{
    for (int i = first; i < first + count && i < pc_item_frame.count; i++) {
        const pc_item* it = &pc_item_frame.item[i];
        int textured = it->kind == PC_ITEM_SPRITE || (it->kind == PC_ITEM_POLY && (it->flags & PC_ITEM_TEXTURED));
        if (textured && it->kind != PC_ITEM_NONE && wanted(it->mode == PC_ITEM_INHERIT ? current : it->mode & 0x7ff))
            return 1;
    }
    return 0;
}

extern void* vs_render_legacyCurrentOrderingTable __attribute__((weak));
void pc_frame_move(unsigned* packet, const short* rect, int x, int y)
{
    /* the copy of the rectangle (x, y, w, h: rect[0..3]) to (x, y), an item drawn after what the top slot of the frame's table holds; a copy with
     * no size draws nothing. `packet` is the four words of memory its marker takes (a DR_MOVE's room: the cursor moves past it). */
    if (!rect[2] || !rect[3] || !&vs_render_legacyCurrentOrderingTable)
        return;
    uint32_t* top = (uint32_t*)((char*)vs_render_legacyCurrentOrderingTable + 0x2084);
    int first = pc_item_frame.count;
    pc_item* it = pc_frame_add(PC_ITEM_COPY, 0);
    if (!it)
        return;
    it->mode = PC_ITEM_INHERIT;
    it->x[0] = rect[0], it->y[0] = rect[1];
    it->x[1] = (int16_t)x, it->y[1] = (int16_t)y;
    it->w = rect[2], it->h = rect[3];
    it->flags |= PC_ITEM_PLACED;
    if (marker_count < ITEMS)
        markers[marker_count].packet = packet, markers[marker_count].entry = top, markers[marker_count].first = first,
        markers[marker_count].count = 1, markers[marker_count++].tail = 1;
    packet[0] = 3u << 24;
    packet[1] = MARKER;
    packet[2] = (uint32_t)first & 0xffffff;
    packet[3] = 1;
}

/* The marker packets placed at `entry` that lie in [lo, hi) of packet memory, in the order the table draws them (the head's: the last
 * placed first, then those appended at the end), for the readers that followed the chain from an entry (the classic layer). */
int pc_frame_markers_at(const void* entry, const void* lo, const void* hi, const void** out, int max)
{
    int n = 0;
    for (int tail = 0; tail < 2; tail++)
        for (int k = 0; k < marker_count && n < max; k++) {
            int i = tail ? k : marker_count - 1 - k;
            if (markers[i].entry == (const uint32_t*)entry && markers[i].tail == tail && (const void*)markers[i].packet >= lo
                && (const void*)markers[i].packet < hi)
                out[n++] = markers[i].packet;
        }
    return n;
}
/* The runs placed at the entries [lo, hi] (a table's slots from the one drawn last of them to the one first), for a reader that deforms them all. */
int pc_frame_runs_between(const void* lo, const void* hi, int* firsts, int* counts, int max)
{
    int n = 0;
    for (int i = 0; i < marker_count && n < max; i++)
        if (markers[i].entry && (const void*)markers[i].entry >= lo && (const void*)markers[i].entry <= hi)
            firsts[n] = markers[i].first, counts[n++] = markers[i].count;
    return n;
}
