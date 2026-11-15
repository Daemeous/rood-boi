/* The renderer thread (render_thread.h). */
#include "render_thread.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stddef.h>
#ifndef _WIN32
#include <execinfo.h> /* (ROOD_RT_TRACE=2: where the first direct call of each hook came from) */
#endif

int pc_render_thread;
/* The mailbox: messages run in order on the thread. A message carries a function and either a pointer the caller keeps valid until it
 * returns (pc_rt_call, which waits) or a copy of the argument (pc_rt_post). */
enum { RING = 32, COPY_MAX = 512 };
typedef struct {
    void (*fn)(void*);
    void* arg;
    unsigned char copy[COPY_MAX];
} message;
static struct {
    pthread_mutex_t lock;
    pthread_cond_t wake, done, room;
    pthread_t thread;
    message ring[RING];
    unsigned long long pushed, completed; /* messages pushed, and run to their end */
    int started;
} rt = { .lock = PTHREAD_MUTEX_INITIALIZER, .wake = PTHREAD_COND_INITIALIZER, .done = PTHREAD_COND_INITIALIZER,
         .room = PTHREAD_COND_INITIALIZER };
static __thread int is_renderer;
extern int pc_vk_active __attribute__((weak)); /* vk_hooks.h: the Vulkan presenter is the one the thread serves (a GL context is bound to its thread) */
__attribute__((constructor)) static void env_init(void)
{
    const char* v = getenv("ROOD_RENDER_THREAD");
    if (v && *v)
        pc_render_thread = atoi(v);
}

static void* thread_main(void* unused)
{
    (void)unused;
    is_renderer = 1;
    pthread_mutex_lock(&rt.lock);
    for (;;) {
        while (rt.completed == rt.pushed)
            pthread_cond_wait(&rt.wake, &rt.lock);
        message* m = &rt.ring[rt.completed % RING];
        pthread_mutex_unlock(&rt.lock);
        m->fn(m->arg);
        pthread_mutex_lock(&rt.lock);
        ++rt.completed;
        pthread_cond_broadcast(&rt.done);
        pthread_cond_signal(&rt.room);
    }
    return NULL;
}
static void trace_report(void);
static int start(void)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u << 20); /* (the walk's hooks ran on the game's own stack: room for the same frames) */
    int failed = pthread_create(&rt.thread, &attr, thread_main, NULL);
    pthread_attr_destroy(&attr);
    if (failed) {
        fprintf(stderr, "[render] cannot start the renderer thread; rendering on the game thread\n");
        pc_render_thread = 0;
        return 0;
    }
    rt.started = 1;
    atexit(trace_report);
    fprintf(stderr, "[render] renderer thread started (%s)\n", pc_render_thread >= 2 ? "async" : "lockstep");
    return 1;
}
int pc_rt_active(void) { return pc_render_thread && rt.started; }
int pc_rt_on_thread(void) { return is_renderer; }
unsigned long long pc_rt_pushed(void) { return rt.pushed; }
static int usable(void)
{
    return pc_render_thread && !is_renderer && &pc_vk_active && pc_vk_active && (rt.started || start());
}
/* Push a message (waiting for a free place); the caller holds no lock. Returns its sequence number. */
static unsigned long long push(void (*fn)(void*), void* arg, const void* copy, size_t size)
{
    pthread_mutex_lock(&rt.lock);
    while (rt.pushed - rt.completed >= RING)
        pthread_cond_wait(&rt.room, &rt.lock);
    message* m = &rt.ring[rt.pushed % RING];
    m->fn = fn;
    if (copy) {
        memcpy(m->copy, copy, size);
        m->arg = m->copy;
    } else {
        m->arg = arg;
    }
    unsigned long long seq = rt.pushed++;
    pthread_cond_signal(&rt.wake);
    pthread_mutex_unlock(&rt.lock);
    return seq;
}
/* ROOD_RT_TRACE: how long the game thread waited on the renderer (calls and posts in lockstep, drains), and how often. */
static long long waited_ns[3];
static unsigned long long waits[3];
static long long now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000LL + t.tv_nsec;
}
static void wait_for(unsigned long long seq)
{
    long long t0 = now_ns();
    pthread_mutex_lock(&rt.lock);
    while (rt.completed <= seq)
        pthread_cond_wait(&rt.done, &rt.lock);
    pthread_mutex_unlock(&rt.lock);
    waited_ns[0] += now_ns() - t0, ++waits[0];
}
void pc_rt_call(void (*fn)(void*), void* arg)
{
    if (!usable()) {
        fn(arg);
        return;
    }
    wait_for(push(fn, arg, NULL, 0));
}
void pc_rt_post(void (*fn)(void*), const void* arg, size_t size)
{
    if (!usable()) {
        unsigned char local[COPY_MAX];
        memcpy(local, arg, size);
        fn(local);
        return;
    }
    unsigned long long seq = push(fn, NULL, arg, size);
    if (pc_render_thread < 2)
        wait_for(seq); /* lockstep: the game thread waits for each message */
}
void pc_rt_drain(void)
{
    if (!usable())
        return;
    long long t0 = now_ns();
    pthread_mutex_lock(&rt.lock);
    unsigned long long to = rt.pushed;
    while (rt.completed < to)
        pthread_cond_wait(&rt.done, &rt.lock);
    pthread_mutex_unlock(&rt.lock);
    waited_ns[1] += now_ns() - t0, ++waits[1];
}

/* ---- the direct hooks ---- */
enum { MAX_HOOKS = 256 };
static int trace_level(void)
{
    static int level = -1;
    if (level < 0) {
        const char* v = getenv("ROOD_RT_TRACE");
        level = v ? (atoi(v) ? atoi(v) : 1) : 0;
    }
    return level;
}
static const char* hook_name[MAX_HOOKS];
static unsigned long long hook_count[MAX_HOOKS];
void pc_rt_note_hook(int hook, const char* name)
{
    if (!pc_rt_active() || pc_rt_on_thread() || hook < 0 || hook >= MAX_HOOKS)
        return;
    hook_name[hook] = name;
    if (!hook_count[hook]++ && trace_level() >= 2) {
#ifndef _WIN32
        void* frames[12];
        int n = backtrace(frames, 12);
        fprintf(stderr, "[render] first direct call of %s from:\n", name);
        backtrace_symbols_fd(frames, n, 2);
#endif
    }
}
/* The hooks the game thread called directly while the thread was on, as JSON (RPC render_thread: the test harness fails a run in mode 2 that has any). */
int pc_rt_direct_json(char* out, int size)
{
    int n = snprintf(out, (size_t)size, "{\"mode\":%d,\"active\":%s,\"direct\":[", pc_render_thread, pc_rt_active() ? "true" : "false");
    int first = 1;
    for (int i = 0; i < MAX_HOOKS && n < size - 80; i++)
        if (hook_count[i]) {
            n += snprintf(out + n, (size_t)(size - n), "%s{\"hook\":\"%s\",\"calls\":%llu}", first ? "" : ",", hook_name[i], hook_count[i]);
            first = 0;
        }
    return n + snprintf(out + n, (size_t)(size - n), "]}");
}
extern void pc_hwc_flush_report(void) __attribute__((weak));
extern const unsigned long long* pc_rq_counts_get(void) __attribute__((weak)); /* renderer_rq.c */
static void trace_report(void)
{
    if (!getenv("ROOD_RT_TRACE"))
        return;
    if (trace_level() >= 3 && pc_hwc_flush_report)
        pc_hwc_flush_report();
    if (trace_level() >= 3 && pc_rq_counts_get)
        for (int k = 0; k < 16; k++)
            if (pc_rq_counts_get()[k])
                fprintf(stderr, "[render]   requests of kind %d: %llu\n", k, pc_rq_counts_get()[k]);
    fprintf(stderr, "[render] the game thread waited: %llu calls/posts %.1f ms, %llu drains %.1f ms\n", waits[0], waited_ns[0] / 1e6, waits[1],
        waited_ns[1] / 1e6);
    fprintf(stderr, "[render] hooks the game thread called directly while the renderer thread was on:\n");
    for (int i = 0; i < MAX_HOOKS; i++)
        if (hook_count[i])
            fprintf(stderr, "[render]   %-28s %llu\n", hook_name[i], hook_count[i]);
}
