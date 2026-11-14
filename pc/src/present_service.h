/* The present as a request (T1 R3): the game's tick posts what the window should show; the service performs it. Today the
 * service runs inline on the calling thread (boot_window.c: pc_present_post); a renderer thread (R4) takes the same
 * requests from a mailbox. A request is a value: the display rectangle and the CPU frame it is taken from travel in it, and
 * nothing in it points into the game's state. SYNC is a fence with a reply: it returns when the rectangle's pixels from the
 * GPU target are in the game's VRAM (the screenshot RPC and the checkpoints need them). */
#ifndef PC_PRESENT_SERVICE_H
#define PC_PRESENT_SERVICE_H
#include <stdint.h>
typedef enum {
    PC_PRESENT_FRAME, /* one game vsync's images: the frame, then its fractions when interpolating (present_rate) */
    PC_PRESENT_SYNC,  /* the display rectangle read back into the game's VRAM (pc_hw_sync) */
} pc_present_kind;
/* What the frame loop owns that the images' pacing reads, as of the request (boot_window.c): the service never looks at the game thread's
 * variables. */
typedef struct {
    int pacing;               /* settings present_pacing: 0 classic, 1 display */
    int paced;                /* the loop paces this vsync (not fast, fast-forward held, or a charge-skipped one) */
    int rate;                 /* images per vsync (pc_window_present_rate) */
    int limit;                /* display pacing's cap on them (the setting itself, 0 = 8) */
    int interp;               /* frame interpolation is on */
    long long deadline_ns;    /* this vsync's deadline (CLOCK_MONOTONIC), 0: none */
    long long field_start_ns; /* when this field began, on the pacing clock */
} pc_present_pacing_in;
typedef struct {
    pc_present_kind kind;
    const uint16_t* vram; /* the CPU frame (the software presenter and the null renderer) */
    int x, y, w, h;       /* the display rectangle */
    /* filled by the service's post: */
    int header;                  /* the slot of the frame header (present_header.h) the images read, -1: none */
    pc_present_pacing_in pace;
} pc_present_request;
void pc_present_post(const pc_present_request* request);
#endif
