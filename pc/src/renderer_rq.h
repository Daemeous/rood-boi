/* Requests to the renderer (T1 R4e): what the game side asks of it and waits for. The one-way stream is the hw_cmd records; a request is
 * the other kind, with a reply. pc_renderer_rq applies every record pending first (the caller's order is the renderer's), runs the
 * request on the renderer's side (pc_rq_run) and returns once the reply is in the caller's `out`. Today it all runs on the caller; the
 * renderer thread (the overlay worker's) posts the request to its mailbox and blocks until it has run, in the order of everything posted
 * before it. The caller owns `in` and `out` and both stay valid until it returns. */
#ifndef PC_RENDERER_RQ_H
#define PC_RENDERER_RQ_H
#include <stddef.h>
#include <stdint.h>
typedef enum {
    PC_RQ_SYNC,         /* arg x, y, w, h: the stale parts of the rectangle read back from the target; out: pc_rq_readback */
    PC_RQ_CAPTURE,      /* arg x, y, w, h, mode (2: the target's own 8-bit pixels, w * h * 4 bytes in the reply's first rectangle, nothing in the renderer changes): the rectangle read back from the target as it is, whole and as asked (a screenshot, a checkpoint, the game's own read of the frame: pc_frame_read); no stale area is touched; out: pc_rq_readback */
    PC_RQ_SYNC_TEXTURE, /* arg tpage, clut, window, u0, v0, u1, v1; in: int clip[4] or NULL; out: pc_rq_readback */
    PC_RQ_DRAIN,        /* every record applied and the GPU idle (the snapshot, rewind and photo fences) */
    PC_RQ_FRAME_STATE,  /* out: pc_renderer_frame_state: the shown picture's flags, fade and emissive lists (the header fill's) */
    PC_RQ_RESYNC,       /* the renderer's state rebuilt from the game's VRAM after a restore (pc_hw_resync); in: the game's array ; in2: a state file's picture of the target (RGBA8, 1024 x 512) put over it, the cells the game's mask says were drawn in stale again, or NULL */
} pc_rq_kind;
typedef struct pc_rq {
    pc_rq_kind kind;
    int arg[8];
    const void* in;
    size_t in_bytes;
    const void* in2; /* a second input (a resync's picture of the target) */
    void* out;
    size_t out_bytes;
    int result;
} pc_rq;
/* What a read-back brings: the rectangles whose pixels the game's array now takes (the renderer never writes the game's memory). The
 * pixels are malloc'd by the renderer, rows tightly packed, and freed by pc_rq_readback_apply. A call reads back at most MAX_STALE (8). */
typedef struct {
    int count;
    struct {
        int x, y, w, h;
        uint16_t* pixels;
    } rect[8];
} pc_rq_readback;
int pc_renderer_rq(pc_rq* rq);
void pc_rq_run(pc_rq* rq); /* the renderer's side: the one handler table */
/* The game side takes a reply into its VRAM array and frees it. */
void pc_rq_readback_apply(pc_rq_readback* reply, uint16_t* vram);
#endif
