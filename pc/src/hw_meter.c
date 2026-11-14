/* Hardware renderer hook meter (hw_meter.h). Host state only: nothing here is game state. */
#define _POSIX_C_SOURCE 200809L
#include "hw_meter.h"
#include "frame_cost.h"
#include "render_thread.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int pc_hw_meter_on;
static int queue_check;

static const char* const names[PC_HW_HOOK_COUNT] = {
#define X(name, cls) #name,
    PC_HW_HOOKS(X)
#undef X
};
static const char classes[PC_HW_HOOK_COUNT] = {
#define X(name, cls) cls,
    PC_HW_HOOKS(X)
#undef X
};

#define RING 120 /* frames of per-hook counts kept (two seconds) */
static struct {
    unsigned long long total[PC_HW_HOOK_COUNT];     /* calls */
    unsigned long long in_walk[PC_HW_HOOK_COUNT];   /* calls made inside the draw walk */
    unsigned long long off_thread[PC_HW_HOOK_COUNT]; /* calls from a thread other than the game's (RPC, window) */
    unsigned long long ns[PC_HW_HOOK_COUNT];        /* time inside the hook (ROOD_PERF), including nested hooks */
    unsigned frame[PC_HW_HOOK_COUNT];               /* this frame */
    unsigned ring[RING][PC_HW_HOOK_COUNT];          /* the last RING frames */
    unsigned max_frame[PC_HW_HOOK_COUNT];
    unsigned long long frames, walk_calls;
    unsigned long long unlisted_in_walk[PC_HW_HOOK_COUNT];
    unsigned long long results[PC_HW_WHY_COUNT][2]; /* [why][value] */
    int result_hook[PC_HW_WHY_COUNT];
    /* the previous frame's calls in order, run-length coded (hook, run) */
    struct {
        unsigned short hook;
        unsigned short run;
    } trace[2][4096];
    int trace_len[2], trace_now;
} m;
static int walk_depth;
static pthread_t game_thread;
static int game_thread_set;

__attribute__((constructor)) static void meter_init(void)
{
    const char* check = getenv("ROOD_HW_QUEUE_CHECK");
    queue_check = check && *check && *check != '0';
    const char* on = getenv("ROOD_HW_METER");
    pc_hw_meter_on = queue_check || (on && *on && *on != '0');
}

static int listed(int id) { return classes[id] == 'A' || classes[id] == 'N' || classes[id] == 'Q' || classes[id] == 'V'; }

pc_hw_meter_scope pc_hw_meter_begin(int id)
{
    pc_hw_meter_scope scope = { id, 0 };
    if (pc_render_thread)
        pc_rt_note_hook(id, names[id]); /* (a direct call while the renderer thread is on: ROOD_RT_TRACE) */
    if (!pc_hw_meter_on)
        return scope;
    ++m.total[id];
    ++m.frame[id];
    int off = game_thread_set && !pthread_equal(pthread_self(), game_thread);
    if (off)
        ++m.off_thread[id];
    else if (walk_depth) {
        ++m.in_walk[id];
        if (!listed(id))
            ++m.unlisted_in_walk[id];
    }
    if (!off) { /* the trace is the game thread's order */
        int t = m.trace_now, n = m.trace_len[t];
        if (n && m.trace[t][n - 1].hook == (unsigned short)id && m.trace[t][n - 1].run < 65535)
            ++m.trace[t][n - 1].run;
        else if (n < 4096) {
            m.trace[t][n].hook = (unsigned short)id, m.trace[t][n].run = 1;
            m.trace_len[t] = n + 1;
        }
    }
    if (pc_cost_on)
        scope.start = pc_cost_clock();
    return scope;
}

void pc_hw_meter_end(pc_hw_meter_scope* scope)
{
    if (pc_hw_meter_on && scope->start)
        m.ns[scope->id] += pc_cost_clock() - scope->start;
}

void pc_hw_meter_result(int hook, int why, int value)
{
    if (!pc_hw_meter_on)
        return;
    ++m.results[why][value ? 1 : 0];
    m.result_hook[why] = hook;
}

void pc_hw_meter_walk(int enter)
{
    if (!pc_hw_meter_on)
        return;
    if (enter) {
        if (!walk_depth++)
            ++m.walk_calls;
    } else if (walk_depth)
        --walk_depth;
}

void pc_hw_meter_vsync(void)
{
    if (!pc_hw_meter_on)
        return;
    if (!game_thread_set)
        game_thread = pthread_self(), game_thread_set = 1;
    int slot = (int)(m.frames % RING);
    for (int i = 0; i < PC_HW_HOOK_COUNT; i++) {
        m.ring[slot][i] = m.frame[i];
        if (m.frame[i] > m.max_frame[i])
            m.max_frame[i] = m.frame[i];
        m.frame[i] = 0;
    }
    ++m.frames;
    if (queue_check) {
        static unsigned char reported[PC_HW_HOOK_COUNT];
        for (int i = 0; i < PC_HW_HOOK_COUNT; i++)
            if (m.unlisted_in_walk[i] && !reported[i]) {
                reported[i] = 1;
                fprintf(stderr, "[hw-queue-check] frame %llu: %s (class %c) called inside the draw walk\n", m.frames, names[i],
                    classes[i]);
            }
    }
    m.trace_now ^= 1;
    m.trace_len[m.trace_now] = 0;
}

/* What each return reason depends on: "frame" the packet and the frame's native lists, "setting" a configuration
 * value, "flag" a renderer flag the game or classic layer sets, "table" bookkeeping the earlier hooks of the frame
 * wrote (stale areas). None depends on the GPU's state or on timing. */
static const char* const why_names[PC_HW_WHY_COUNT] = { "tri_inactive", "tri_suppressed", "tri_effect_stale",
    "tri_drawn", "skip_inactive", "skip_disabled", "skip_covered", "skip_not_covered", "ot_stale_dropped_by_readback" };
static const char* const why_depends[PC_HW_WHY_COUNT] = { "flag", "flag", "frame+setting+table", "frame", "flag+table", "setting",
    "table", "table", "readback (the game sampling or storing stale texels)" };

int pc_hw_meter_json(char* out, int size, int reset)
{
    int n = 0;
    n += snprintf(out + n, (size_t)(size - n), "{\"on\":%d,\"queue_check\":%d,\"frames\":%llu,\"walks\":%llu,\"hooks\":[", pc_hw_meter_on,
        queue_check, m.frames, m.walk_calls);
    unsigned long long frames = m.frames < RING ? m.frames : RING;
    for (int i = 0; i < PC_HW_HOOK_COUNT && n < size - 400; i++) {
        if (!m.total[i])
            continue;
        unsigned long long sum = 0;
        unsigned active = 0;
        for (unsigned long long f = 0; f < frames; f++) {
            sum += m.ring[f][i];
            active += m.ring[f][i] != 0;
        }
        n += snprintf(out + n, (size_t)(size - n),
            "%s{\"hook\":\"%s\",\"class\":\"%c\",\"calls\":%llu,\"in_walk\":%llu,\"off_thread\":%llu,\"per_frame\":%.2f,"
            "\"max_per_frame\":%u,\"frames_with_calls\":%u,\"ns\":%llu,\"unlisted_in_walk\":%llu}",
            n > 80 && out[n - 1] == '}' ? "," : "", names[i], classes[i], m.total[i], m.in_walk[i], m.off_thread[i],
            frames ? (double)sum / (double)frames : 0.0, m.max_frame[i], active, m.ns[i], m.unlisted_in_walk[i]);
    }
    n += snprintf(out + n, (size_t)(size - n), "],\"results\":[");
    int first = 1;
    for (int w = 0; w < PC_HW_WHY_COUNT; w++) {
        if (!m.results[w][0] && !m.results[w][1])
            continue;
        n += snprintf(out + n, (size_t)(size - n), "%s{\"why\":\"%s\",\"returned_0\":%llu,\"returned_1\":%llu,\"depends_on\":\"%s\"}",
            first ? "" : ",", why_names[w], m.results[w][0], m.results[w][1], why_depends[w]);
        first = 0;
    }
    /* The previous frame's calls in order: [hook, run]. */
    n += snprintf(out + n, (size_t)(size - n), "],\"last_frame\":[");
    int prev = m.trace_now ^ 1;
    for (int i = 0; i < m.trace_len[prev] && n < size - 80; i++)
        n += snprintf(out + n, (size_t)(size - n), "%s[\"%s\",%u]", i ? "," : "", names[m.trace[prev][i].hook], m.trace[prev][i].run);
    n += snprintf(out + n, (size_t)(size - n), "]}");
    if (reset) {
        memset(m.total, 0, sizeof m.total), memset(m.in_walk, 0, sizeof m.in_walk), memset(m.off_thread, 0, sizeof m.off_thread);
        memset(m.ns, 0, sizeof m.ns), memset(m.max_frame, 0, sizeof m.max_frame), memset(m.unlisted_in_walk, 0, sizeof m.unlisted_in_walk);
        memset(m.results, 0, sizeof m.results), memset(m.ring, 0, sizeof m.ring);
        m.frames = 0, m.walk_calls = 0;
    }
    return n;
}
