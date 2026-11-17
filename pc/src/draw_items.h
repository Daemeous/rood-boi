/* Typed draw items: a frame as data (pc/docs/plans/2026-11-08-frames-as-data.md).
 *
 * Game code that used to build GPU packets and link them into an ordering table
 * adds items to the frame instead: a tile, a sprite or a polygon with its place,
 * texels, colour, draw mode and a depth to sort by. Nothing is written to guest
 * memory. The items live in one host list that is saved with the game's state
 * (draw_items.c), so a save state taken between building a frame and drawing it
 * keeps it.
 *
 * Draw order: a larger depth draws first (an ordering-table slot's number); items
 * of one depth draw in the opposite order to their submission, as packets linked
 * at the head of a slot did. pc_frame_draw draws the whole frame. A frame that
 * still has packets (a screen not converted yet) places a run of its items in the
 * ordering table with pc_frame_place: the walk draws them where the marker is.
 *
 * Today the host turns each item into the GP0 words the game used to write and
 * hands them to the packet path (the CPU rasterizer, the hardware path, ui_text,
 * the classic layer see what they saw). That is the transition's lowering; a
 * renderer that takes items directly replaces it. */
#ifndef ROOD_DRAW_ITEMS_H
#define ROOD_DRAW_ITEMS_H
/* (plain C types: the game's own headers, which do not have stdint.h, include it) */

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PC_ITEM_NONE,   /* (a slot that draws nothing) */
    PC_ITEM_TILE,   /* a flat rectangle */
    PC_ITEM_SPRITE, /* a textured rectangle, texels copied 1:1 */
    PC_ITEM_POLY,   /* a triangle or quad: flat or shaded, textured or not */
    PC_ITEM_LINE,   /* a line from (x[0], y[0]) to (x[1], y[1]): flat, or shaded from rgb[0] to rgb[1] */
    PC_ITEM_AREA,   /* the drawing area for the items drawn after it: (x[0], y[0]) and w, h */
    PC_ITEM_MODE_SET, /* the draw mode (GP0 E1: `mode`) for the items drawn after it that carry none (PC_ITEM_INHERIT) */
    PC_ITEM_MASK,   /* the mask bit set when drawing (x[0] 1) or not (0): GP0 E6 */
    PC_ITEM_COPY,   /* a copy within VRAM (DR_MOVE: GP0 80h): the rectangle (x[0], y[0], w, h) to (x[1], y[1]); T5 retires it */
    PC_ITEM_NATIVE  /* a native list's draw (render.c: a model, the room, an effects list): the list, and for a translucent run (verts 1) its entries first, count */
};

enum {
    PC_ITEM_SEMI = 1,     /* blends with what is behind it, by the mode's blend (a textured one: its STP texels) */
    PC_ITEM_RAW = 2,      /* textured: the texels as they are, not scaled by the colour */
    PC_ITEM_TEXTURED = 4, /* polygon: samples a texture (a sprite always does) */
    PC_ITEM_SHADED = 8,   /* polygon: a colour at each corner (else rgb[0] for all) */
    PC_ITEM_BRACKET = 16, /* polygon or line, as the effect plugins drew: with dither off, the draw mode (a line's is
                           * `mode`) set for it alone, and page 0, no blend, dither on left after it */
    PC_ITEM_PLACED = 32,  /* a marker in an ordering table draws it (set by pc_frame_place) */
    PC_ITEM_WINDOW = 64,  /* `tw` is the texture window it draws with (GP0 E2); without the flag the window is left as it is */
    PC_ITEM_AFTER = 128,  /* `after` is the draw mode (GP0 E1) left set after it; without the flag the mode it drew with stays */
    PC_ITEM_HW_HIDDEN = 1024, /* drawn into the game's own picture (VRAM) but left out of the hardware one (a rain drop solid room geometry hides) */
    PC_ITEM_OFFSET = 512, /* drawing area: x[1], y[1] is also the draw offset (GP0 E5) it sets */
    PC_ITEM_SPAN = 8192,     /* `span` is the shape of the scanline fill this line is a row of (pc_frame_span) */
    PC_ITEM_ANCHORED = 2048, /* `anchor` is the widescreen anchor it draws with (pc_frame_anchor) */
    PC_ITEM_OPACITY = 4096,  /* `opacity` (0..256) is how opaque it draws (pc_frame_opacity) */
    PC_ITEM_BEFORE = 256  /* textured polygon: `before` is the draw mode (GP0 E1) set before it, whose dither and display bits it
                           * draws with (its own page word sets the rest) */
};

/* The draw mode a tile, a sprite or an untextured polygon draws with, from a
 * libgpu texture page word (getTPage: page, blend, colour depth): what
 * SetDrawTPage(.., 0, 1, page) set, dither on. A textured polygon carries its
 * own page in `mode` (as its packet did): give it the page word as it was. */
/* A flat item with this mode draws in whatever draw mode the GPU is in (the packet had no draw mode before it). */
#define PC_ITEM_INHERIT 0xffff
#define PC_ITEM_MODE(page) (((page) & 0x9ff) | 0x200)
/* A colour as the items take it: 0xBBGGRR, each channel cut to a byte. */
#define PC_RGB(r, g, b) (((r) & 0xff) | ((g) & 0xff) << 8 | ((b) & 0xff) << 16)
#define PC_GREY(v) (((v) & 0xff) * 0x010101)

/* The words a native item lowers to (after the tag): the walk's native draw recognises them (render.c, boot_runtime.c). */
#define PC_NATIVE_MARKER 0x00524e44u
#define PC_NATIVE_TRANSLUCENT_MARKER 0x00544e44u

typedef struct {
    unsigned char kind;  /* PC_ITEM_* */
    unsigned char verts; /* polygon: 3 or 4 */
    unsigned short flags; /* PC_ITEM_* flags */
    unsigned short group; /* items added together as one call (pc_frame_add_packets) share it; 0: none */
    unsigned short mode; /* see PC_ITEM_MODE */
    unsigned short clut; /* textured: the CLUT word (y << 6 | x >> 4) */
    int depth; /* sort key: larger first (see above) */
    short x[4], y[4]; /* polygon corners; a rectangle's top left is x[0], y[0] */
    short w, h;       /* a rectangle's size */
    unsigned char u[4], v[4]; /* textured: texel at each corner (a sprite's top left is u[0], v[0]) */
    unsigned char rgb[4][3];  /* colour per corner; a flat item's is rgb[0] */
    unsigned tw;              /* PC_ITEM_WINDOW: the window word's 20 bits */
    unsigned native_list;     /* PC_ITEM_NATIVE: the list's id */
    int native_first, native_count; /* PC_ITEM_NATIVE, translucent: its entries */
    unsigned short after;     /* PC_ITEM_AFTER: the draw mode word's bits */
    unsigned short before;    /* PC_ITEM_BEFORE: likewise */
    unsigned char anchor;     /* PC_ITEM_ANCHORED: PC_ANCHOR_* (gpu_hw.c), 0x20 set when exact */
    unsigned short opacity;   /* PC_ITEM_OPACITY */
    int anchor_span;          /* the items in the range that set `anchor` (the innermost range wins) */
    float span[5];            /* PC_ITEM_SPAN: the row's length (x1 - x0) and how its ends join the rows above and below: top left, top right, bottom left, bottom right */
} pc_item;

/* A new frame: the previous frame's items are dropped. */
void pc_frame_begin(void);
/* The next item, zeroed with its kind and depth set; NULL when the frame is full
 * (it is logged once and the item is left out). */
pc_item* pc_frame_add(int kind, int depth);
/* Helpers for the common ones (rgb, mode and clut are cut to their fields). */
void pc_frame_tile(int depth, int x, int y, int w, int h, int rgb, int mode, int flags);
void pc_frame_sprite(int depth, int x, int y, int w, int h, int u, int v, int rgb, int clut, int mode, int flags);
/* The next polygon item of a run of one, placed in the ordering table at slot
 * `depth` of the table at `ot` with a marker packet taken from `*cursor` (the
 * packet cursor, moved past it); a NULL `ot` places nothing (a frame of items alone). When the frame is full a spare item is returned
 * that is never drawn, so the caller can fill it all the same. */
pc_item* pc_frame_poly_at(int depth, int kind, int verts, int flags, unsigned* ot, unsigned** cursor);
/* The number of items so far: the first index of the next run. */
int pc_frame_count(void);
/* The classic layer's protocol (pc/src/classic): a site is a range of items, [first, end) of the frame's
 * list. pc_frame_site sets the range of the site about to be reported (PC_CLASSIC does, around the
 * statement), pc_frame_site_range reads it. */
void pc_frame_site(int first, int end);
void pc_frame_site_range(int* first, int* end);
/* Items [first, end) are no longer drawn (kind none); how many were. Items are mutable until the frame ends. */
int pc_frame_suppress(int first, int end);
/* Drops the items from index `count` on (a draw that was never to show, given back). */
void pc_frame_rewind(int count);
/* Whether item `index` is placed in an ordering table, and its kind. */
int pc_frame_is_placed(int index);
/* Reads (pc_frame_order, pc_frame_lower) see the last finished frame's items while on, not the frame being built: the
 * guest's packets at a vsync are the last frame's (the dev RPC's scan). */
void pc_frame_view_last(int on);
/* Raw GP0 words in draw order (one packet's payload, or several joined) as items at `depth`: polygons, tiles, sprites,
 * flat lines and drawing areas, each with the draw mode (E1) and texture window (E2) the words had set before it.
 * The items are added so that they draw in the order of the words. Returns how many items were added (from index *first); 0 when the words are anything else (a state word after the last primitive that a window changes, other commands): nothing is added then.
 * Words that only set the draw mode and the texture window are one mode item. */
int pc_frame_add_packets(int depth, const unsigned* words, int n, int* first);
/* The same without words: a run of items made in the order they draw, with the draw mode (GP0 E1) and texture window (GP0 E2) that
 * apply to what follows set between them, as the words set them. pc_frame_run_end leaves them as pc_frame_add_packets does
 * (a mode set after the last primitive stays with it, a window set after it is an item of its own). A primitive's `flags` are the
 * PC_ITEM_ ones of its kind (SEMI, RAW, TEXTURED, SHADED); a textured polygon takes its page word and CLUT. The caller fills the
 * item's corners, texels and colours. NULL when the frame is full (the run is then dropped by pc_frame_run_end). */
typedef struct {
    int start, depth, mode, window, last_mode, last_window, state_after, prims, bad;
    pc_item* last;
} pc_frame_run;
void pc_frame_run_begin(pc_frame_run* run, int depth);
void pc_frame_run_state(pc_frame_run* run, int mode, int window); /* -1: left as it is */
pc_item* pc_frame_run_poly(pc_frame_run* run, int verts, int flags, int page, int clut);
pc_item* pc_frame_run_tile(pc_frame_run* run, int flags);
pc_item* pc_frame_run_line(pc_frame_run* run, int flags);
/* Returns how many items the run made (from index *first), 0 when none (nothing is kept then): in the order a frame draws items of
 * one depth (the last made first), so they draw as the run was made. */
int pc_frame_run_end(pc_frame_run* run, int* first);
/* Items [first, first + count) in the order they draw (the larger depth first, then the later submission first),
 * leaving out items that are placed in a table when `unplaced` is set; their indices go to out (at most max). */
int pc_frame_order(int first, int count, int unplaced, int* out, int max);
/* The packets item `index` is: what the GPU is given for it, up to three (a texture window, a draw mode, then the
 * primitive; a polygon that carries its page has no mode packet). The payload words of packet k go to words[k] (at most 16),
 * how many in lengths[k]; returns the number of packets. */
int pc_frame_lower(int index, unsigned words[3][16], int lengths[3]);
/* Widescreen: items [first, end) anchored as pc_hw_anchor_packets does a range of packets (their lowered packets have
 * a stable address per index). */
void pc_frame_anchor(int first, int end, int anchor);
/* Item `index`, to read or change before the frame is drawn; NULL out of range. */
pc_item* pc_frame_item(int index);
/* The items from `first` on drawn at once, as one packet through DrawPrim, then dropped (a screen that drew with DrawPrim). */
void pc_frame_immediate(int first);
/* The items from `first` on, a run (pc_frame_run_end), drawn at once in the order the run made them, as one packet through DrawPrim, then
 * given back. */
void pc_frame_immediate_run(int first);
/* Words the game wrote that are no item (a code the colour overflowed into): drawn as they are, as one packet. */
void pc_frame_immediate_words(const unsigned* words, int n);
/* The packet's last primitive's first word (the command word ui_text keys on), the packet built as pc_frame_immediate draws it. */
const void* pc_frame_immediate_command(int first);
/* The command word of item `index`'s lowered packet: ui_text keys its words on it, as it did on a packet's. The index
 * of the item a command word belongs to, -1 when it is none's. */
const void* pc_frame_command(int index);
int pc_frame_command_item(const void* command);
/* The address of item `index`'s lowered packet (where the draw puts it; the key of anchors and interpolation). */
const void* pc_frame_packet(int index);
/* An ordering table made (ClearOTag, ClearOTagR: `reverse`), and the order the runs placed in the table of a DrawOTag's `head` draw in. */
void pc_frame_table(const void* base, int n, int reverse);
/* The frame's ordering tables, as the game makes and draws them (boot_sdk.c): a table of `n` entries made (a `reverse` one is drawn from its last entry), and the
 * table whose first-reached entry is `head` drawn. */
void pc_frame_table_clear(unsigned* ot, int n, int reverse);
void pc_frame_table_draw(unsigned* head);
void pc_frame_splice_table(const void* at, const void* head); /* the table at `head` is walked after the entry `at` (pc_frame_table_after) */
int pc_frame_run_live(int first, int count); /* a run with an item that draws */
int pc_frame_expected_order(const void* head, int* out, int max);
/* The walk of a table takes the runs in by the slot they are placed at (boot_runtime.c adopt): the index is made once per table, then by entry: the marker packets
 * placed at `entry` in draw order (`tail`: those appended at the end of its chain), whether some are appended, and whether two entries lie in one table. */
void pc_frame_markers_index(void);
int pc_frame_entry_markers(const void* entry, int tail, const void** out, int max); /* (returns how many there are, writing up to max) */
int pc_frame_entry_has_tail(const void* entry);
int pc_frame_same_table(const void* a, const void* b);
/* Whether the run [first, first + count) has a native list's draw (PC_ITEM_NATIVE). */
int pc_frame_run_has_native(int first, int count);
/* Whether `packet` is a marker (pc_frame_place) and the run of items it places. */
int pc_frame_marker(const unsigned* packet, int* first, int* count);
int pc_frame_run_pages(int first, int count, unsigned current, int (*wanted)(unsigned page));
/* The item indices [*first, *end) of the markers that lie in [start, end) of packet memory (a site whose statement
 * added items by printers that place them with markers: its item range, whoever called it); 0 when none. */
int pc_frame_marker_range(const void* start, const void* end, int* first, int* last);
/* The lowered packets (their arena addresses) of the items whose marker lies in [start, end) of packet memory: [*lo, *hi);
 * both NULL when none does. For readers that know a range of packets by address (the vector bubbles). */
int pc_frame_marker_span(const void* start, const void* end, const void** lo, const void** hi);
/* The packets item `index` is, as the tagged words a packet list holds (a tag word with the length, then the words), appended
 * to out (at most max words); returns how many words. */
int pc_frame_packet_words(int index, unsigned* out, int max);
/* The sprite items [first, first + count) drawn in the E1 word `mode`, with the CLUT word `clut`, semi-transparent when `semi`:
 * a caller restyling the text it just printed (it used to patch the printer's packets by their words). */
void pc_frame_restyle(int first, int count, int mode, int clut, int semi);
/* A printer's call: its items [first, first + count) are one group (ui_text finds a word by it), and a draw mode alone is an
 * item too (a printer with nothing to print still set its draw mode). */
void pc_frame_group(int first, int count);
void pc_frame_mode_item(int depth, int mode);
/* The runs a marker placed in [start, end) of packet memory are anchored as that range is (pc_hw_anchor_packets). */
void pc_frame_anchor_markers(const void* start, const void* end, int anchor);
/* Items [first, end) drawn at `opacity` (16.16, 0x10000 opaque): a faded text box's glyphs. */
void pc_frame_opacity(int first, int end, int opacity);
/* A horizontal line item drawn as one row of a filled shape (the area map's floor): the shape it has, as gpu_hw.c draws it at upscale. */
void pc_frame_span(int item, int length, float top_left, float top_right, float bottom_left, float bottom_right);
/* A DR_MOVE the game built (SetDrawMove) and appends to the end of the top slot of the table (vs_render_appendOverlayPrimitive): its
 * copy becomes a PC_ITEM_COPY item, a marker is spliced where the packet would have linked, drawn after what the slot holds. A packet
 * that is not a copy (no size) is spliced as it is. */
void pc_frame_copy_tail(unsigned* top, unsigned* packet);
/* Draws the frame's items in order: what DrawOTag did for a table. */
void pc_frame_draw(void);
/* Links a marker for items [first, first + count) at the head of `*ot_entry`
 * in a packet of four words at `packet`; returns the next free word. The
 * ordering-table walk draws the run (in order among themselves) where the
 * marker is, with the drawing state in effect there. */
unsigned* pc_frame_place(int first, int count, unsigned* ot_entry, unsigned* packet);

#ifdef __cplusplus
}
#endif
#endif
