/* The renderer thread's mailbox under ThreadSanitizer (render_thread.c, T1 R4e): the game thread posts copies, calls and drains from one
 * thread while the renderer thread runs them, in lockstep (render_thread 1) and async (2). Checks the order (every message runs after the
 * ones posted before it), the copies (the argument is the posted value even when the poster has reused its buffer), the blocking call (the
 * result is there when it returns) and the drain (everything posted has run). Built by render_thread_tsan.py with -fsanitize=thread. */
#include "../src/render_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int pc_vk_active = 1; /* (the Vulkan presenter is what the thread serves) */
static long long ran, last_seq = -1;
static int order_errors, copy_errors;
typedef struct {
    long long seq;
    char pad[200];
    long long check;
} message;
static void on_post(void* arg)
{
    message* m = arg;
    if (m->seq != last_seq + 1)
        ++order_errors;
    last_seq = m->seq;
    if (m->check != ~m->seq)
        ++copy_errors;
    ++ran; /* (renderer-owned: the game thread reads it only after a drain or call: they order it) */
}
static void on_call(void* arg)
{
    long long* out = arg;
    *out = ran;
}
int main(int argc, char** argv)
{
    pc_render_thread = argc > 1 ? atoi(argv[1]) : 2;
    const int total = 20000;
    long long seq = 0;
    for (int round = 0; round < total / 100; round++) {
        for (int i = 0; i < 100; i++, seq++) {
            message m = { .seq = seq, .check = ~seq };
            pc_rt_post(on_post, &m, sizeof m); /* (m is reused at once: the post took a copy) */
            m.check = 0;
        }
        long long seen = -1;
        pc_rt_call(on_call, &seen); /* after all 100 */
        if (seen != seq)
            ++order_errors;
        if (round % 10 == 0)
            pc_rt_drain();
    }
    pc_rt_drain();
    if (ran != total || order_errors || copy_errors) {
        printf("FAIL: ran %lld of %d, %d order errors, %d copy errors (render_thread %d)\n", ran, total, order_errors, copy_errors,
            pc_render_thread);
        return 1;
    }
    printf("PASS: %d messages in order with their copies, calls and drains (render_thread %d, %s)\n", total, pc_render_thread,
        pc_rt_active() ? "on the thread" : "inline");
    return 0;
}
