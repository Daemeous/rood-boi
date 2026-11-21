/* What the hardware path knows about a packet beyond its words (T4 slice S1): which registered ranges of packet memory it
 * lies in. Resolved once per packet, when a table is adopted (boot_runtime.c) or a single packet is drawn, then read by the
 * draw as attributes: the packet's address is no longer looked up while drawing. The indices point into the active
 * tables of gpu_hw.c for the table being drawn (what a range records while it draws, such as a native list having drawn,
 * stays there). */
#ifndef ROOD_GPU_ATTR_H
#define ROOD_GPU_ATTR_H
typedef struct {
    short effect;  /* the drop shadow or effect range holding it (gpu_hw.c active_effects), -1 none */
    short span;    /* its scanline fill shape (pc_hw_span_shape), -1 none */
    short opacity; /* 0..256 */
    short bubble;  /* the vector talk bubble whose packets hold it (ui_bubbles.c), -1 none */
    unsigned char has_fade; /* a draw item's edge fade (draw_items.h pc_frame_fade) */
    unsigned char fade_a[2];
    short fade_y[2];
    short precise; /* the recorded sub-pixel corners of its first command (ui_upscale.c), -1 none */
    int anchor;    /* PC_ANCHOR_*: the innermost anchored range */
    unsigned char exact;
    unsigned char has_span; /* a draw item's scanline fill shape (draw_items.h pc_frame_span): span_shape = length, top left and right, bottom left and right */
    float span_shape[5];
    unsigned char has_wire; /* a draw item's line ends, unrounded (draw_items.h PC_ITEM_WIRE): wire = x0, y0, x1, y1 before the drawing offset */
    float wire[4];
} pc_hw_attr;
#endif
