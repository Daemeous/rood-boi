/* Requests to the renderer (renderer_rq.h). Host state: nothing here is game state. */
#include "renderer_rq.h"
#include "hw_cmd.h"
#include "renderer_frame_state.h"
#include "hw_registry.h"
#include "render_thread.h"
#include <stdlib.h>
#include <string.h>

extern void pc_hw_drain(void) __attribute__((weak));
extern void pc_hw_resync(const uint16_t*, const uint8_t*) __attribute__((weak));
extern void pc_hw_stale_get(pc_stale_set* out); /* gpu_hw.c: the renderer's stale set */
extern void pc_hw_sync(int, int, int, int) __attribute__((weak));
extern void pc_hw_capture_read(int, int, int, int, int) __attribute__((weak));
extern void pc_hw_sync_texture(unsigned, unsigned, unsigned, int, int, int, int, const int*) __attribute__((weak));
extern void pc_hw_readback_into(pc_rq_readback* reply); /* gpu_hw.c: where the next read-backs put their pixels (NULL: the game's array) */

void pc_rq_run(pc_rq* rq)
{
    rq->result = 0;
    switch (rq->kind) {
    case PC_RQ_SYNC:
    case PC_RQ_CAPTURE:
    case PC_RQ_SYNC_TEXTURE:
        if (rq->out)
            memset(rq->out, 0, rq->out_bytes), pc_hw_readback_into(rq->out);
        if (rq->kind == PC_RQ_SYNC && pc_hw_sync)
            pc_hw_sync(rq->arg[0], rq->arg[1], rq->arg[2], rq->arg[3]);
        else if (rq->kind == PC_RQ_CAPTURE && pc_hw_capture_read)
            pc_hw_capture_read(rq->arg[0], rq->arg[1], rq->arg[2], rq->arg[3], rq->arg[4]);
        else if (rq->kind == PC_RQ_SYNC_TEXTURE && pc_hw_sync_texture)
            pc_hw_sync_texture((unsigned)rq->arg[0], (unsigned)rq->arg[1], (unsigned)rq->arg[2], rq->arg[3], rq->arg[4], rq->arg[5],
                rq->arg[6], rq->in);
        if (rq->out)
            pc_hw_readback_into(NULL);
        break;
    case PC_RQ_DRAIN:
        if (pc_hw_drain)
            pc_hw_drain();
        break;
    case PC_RQ_FRAME_STATE:
        if (rq->out)
            pc_hw_frame_state(rq->out);
        break;
    case PC_RQ_RESYNC:
        if (pc_hw_resync)
            pc_hw_resync(rq->in, rq->in2);
        if (rq->out && rq->out_bytes == sizeof(pc_stale_set))
            pc_hw_stale_get(rq->out); /* the restored state's stale areas: the producer's copy becomes it */
        break;
    }
}
static void run_call(void* arg) { pc_rq_run(arg); }
static unsigned long long pc_rq_counts[16];
const unsigned long long* pc_rq_counts_get(void) { return pc_rq_counts; }
int pc_renderer_rq(pc_rq* rq)
{
    ++pc_rq_counts[(unsigned)rq->kind & 15];
    pc_hwc_flush(); /* (every record and frame posted before it is applied first) */
    pc_rt_call(run_call, rq); /* on the renderer's thread when it is on, else here */
    return rq->result;
}
void pc_rq_readback_apply(pc_rq_readback* reply, uint16_t* vram)
{
    for (int i = 0; i < reply->count; i++) {
        for (int row = 0; row < reply->rect[i].h; row++)
            memcpy(vram + (size_t)(reply->rect[i].y + row) * 1024 + reply->rect[i].x, reply->rect[i].pixels + (size_t)row * reply->rect[i].w,
                (size_t)reply->rect[i].w * sizeof(uint16_t));
        free(reply->rect[i].pixels);
    }
    reply->count = 0;
}
