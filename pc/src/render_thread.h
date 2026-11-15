/* The renderer thread (T1 R4e). The renderer is whatever applies the game's hw_cmd records and shows its frames: gpu_hw.c's state, the
 * Vulkan context, the presenter and the present service (present_service.h). With the setting render_thread on, that work runs on a thread of
 * its own and the game thread reaches it only by a call (pc_rt_call), never by touching its state.
 *
 *   render_thread 0   off: pc_rt_call runs the function on the calling thread (today's behaviour exactly).
 *   render_thread 2   async: the game thread posts frames and record buffers and goes on (the stage after the direct-hook list is empty).
 *   render_thread 1   lockstep: the thread runs each call and the game thread waits for it. Nothing runs concurrently, so every game-side
 *                     reach into the renderer that is still direct (the list ROOD_RT_TRACE=1 prints) is harmless; what the thread
 *                     proves is the affinity: the Vulkan context, the swapchain, the ImGui backend and the present service work from one
 *                     thread that is not the game's, and the frames equal the inline ones.
 *
 * ROOD_RT_TRACE=1 reports, at exit, every renderer hook (gpu_hw.c's pc_hw_*) the game thread called directly while the thread was on: the
 * work that remains before the calls can overlap (requests on the mailbox, renderer_rq.h). */
#ifndef PC_RENDER_THREAD_H
#define PC_RENDER_THREAD_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
extern int pc_render_thread; /* settings render_thread (ROOD_RENDER_THREAD overrides at start) */
/* Run fn(arg) on the renderer thread and wait for it (inline when the thread is off, not started, or this is it). */
void pc_rt_call(void (*fn)(void*), void* arg);
/* Queue fn(copy of arg) (at most 512 bytes) for the thread, in order after everything posted before: lockstep waits for it, async does not. */
void pc_rt_post(void (*fn)(void*), const void* arg, size_t size);
/* Wait until everything posted so far has run (async: a fence; the snapshot, rewind and reload paths use it). */
void pc_rt_drain(void);
int pc_rt_on_thread(void); /* the caller is the renderer thread */
unsigned long long pc_rt_pushed(void); /* messages posted so far (the game thread's: an answer the renderer gave stays right until this moves) */
int pc_rt_direct_json(char* out, int size); /* the direct hook calls so far, as JSON */
void pc_rt_stop(void); /* drain, then end and join the thread (a new one starts at the next use) */
int pc_rt_active(void);    /* the thread is running (calls from other threads are requests) */
/* A renderer hook was entered (hw_meter.h): counted when the game thread calls it while the thread is active. */
void pc_rt_note_hook(int hook, const char* name);
#ifdef __cplusplus
}
#endif
#endif
