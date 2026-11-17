/* State captures and masked page digests; intentionally outside saved state. */
#include "state_diff.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void pc_sound_lock(void) __attribute__((weak)), pc_sound_unlock(void) __attribute__((weak)); /* audio_output.c */
extern int pc_spu_state_size(void) __attribute__((weak));
extern void pc_spu_state_save(void*) __attribute__((weak));
extern int pc_spu_state_load(const void*, int) __attribute__((weak));

/* draw_items.c: the frame's drawing bookkeeping, one object inside the saved state (not compared). */
extern void pc_frame_book(void** start, size_t* size) __attribute__((weak));
#define MAX_RANGES 170
#define PAGE 4096
struct pc_state_image {
    pc_state_range ranges[MAX_RANGES];
    int count, spu_size;
    size_t bytes;
    unsigned char* data;
};

typedef struct { uintptr_t start; size_t size; } mask;
static mask masks[64];
static int mask_count;
static int mode, ticks, capacity, recorded_ticks, pages, first_tick = -1, error;
static uint64_t* recorded;
static uint64_t* now;
static pc_state_image* layout;
static char first_pages[1024];

static int describe(pc_state_image* m)
{
    m->count = pc_state_ranges(m->ranges, MAX_RANGES);
    if (m->count <= 0 || m->count > MAX_RANGES)
        return -1;
    if (pc_frame_book) { /* (the range it lies in, split around it) */
        void* at;
        size_t size;
        pc_frame_book(&at, &size);
        for (int i = 0; i < m->count && m->count < MAX_RANGES; i++) {
            pc_state_range r = m->ranges[i];
            char *lo = (char*)r.start, *hi = lo + r.size, *b = at;
            if (b < lo || b + size > hi)
                continue;
            memmove(&m->ranges[i + 1], &m->ranges[i], (size_t)(m->count - i) * sizeof m->ranges[0]);
            m->ranges[i] = (pc_state_range) { r.start, (size_t)(b - lo), r.name };
            m->ranges[i + 1] = (pc_state_range) { b + size, (size_t)(hi - (b + size)), r.name };
            m->count++;
            break;
        }
    }
    m->bytes = 0;
    for (int i = 0; i < m->count; i++) {
        pc_state_range* r = &m->ranges[i];
        if ((r->size && !r->start) || r->size > UINTPTR_MAX - (uintptr_t)r->start ||
            r->size > SIZE_MAX - m->bytes)
            return -1;
        m->bytes += r->size;
    }
    m->spu_size = pc_spu_state_size ? pc_spu_state_size() : 0;
    if (m->spu_size < 0 || (m->spu_size && (!pc_spu_state_save || !pc_spu_state_load)) ||
        (size_t)m->spu_size > SIZE_MAX - m->bytes)
        return -1;
    m->bytes += (size_t)m->spu_size;
    return 0;
}
static int same_layout(const pc_state_image* a, const pc_state_image* b)
{
    if (!a || !b || a->count != b->count || a->spu_size != b->spu_size || a->bytes != b->bytes)
        return 0;
    for (int i = 0; i < a->count; i++)
        if (a->ranges[i].start != b->ranges[i].start || a->ranges[i].size != b->ranges[i].size)
            return 0;
    return 1;
}
/* The shadow runner captures four ~13 MB images per routed call. Fresh malloc/free of that size is an mmap and
 * ~3000 page faults each time, so freed data buffers are kept and reused when the size matches. The allocation
 * failure tests build with DATA_POOL 0 so every capture reaches malloc. */
#ifndef DATA_POOL
#define DATA_POOL 8
#endif
static struct { unsigned char* data; size_t bytes; } data_pool[DATA_POOL ? DATA_POOL : 1];
static unsigned char* data_take(size_t bytes)
{
    for (int i = 0; i < DATA_POOL; i++)
        if (data_pool[i].data && data_pool[i].bytes == bytes) {
            unsigned char* d = data_pool[i].data;
            data_pool[i].data = NULL;
            return d;
        }
    return malloc(bytes ? bytes : 1);
}
static void data_give(unsigned char* data, size_t bytes)
{
    for (int i = 0; i < DATA_POOL; i++)
        if (!data_pool[i].data) {
            data_pool[i].data = data;
            data_pool[i].bytes = bytes;
            return;
        }
    free(data);
}
pc_state_image* pc_state_capture(void)
{
    pc_state_image* m = calloc(1, sizeof *m);
    if (!m)
        return NULL;
    if (describe(m) || !(m->data = data_take(m->bytes))) {
        pc_state_image_free(m);
        return NULL;
    }
    if (pc_sound_lock)
        pc_sound_lock(); /* the audio thread ticks the driver: not while its state is copied */
    size_t at = 0;
    for (int i = 0; i < m->count; i++) {
        if (m->ranges[i].size)
            memcpy(m->data + at, m->ranges[i].start, m->ranges[i].size);
        at += m->ranges[i].size;
    }
    if (m->spu_size)
        pc_spu_state_save(m->data + at);
    if (pc_sound_unlock)
        pc_sound_unlock();
    return m;
}
void pc_state_image_free(pc_state_image* m)
{
    if (m) {
        if (m->data)
            data_give(m->data, m->bytes);
        free(m);
    }
}
int pc_state_restore(const pc_state_image* m)
{
    pc_state_image live;
    if (!m || describe(&live) || !same_layout(m, &live))
        return -1;
    size_t at = m->bytes - (size_t)m->spu_size;
    if (pc_sound_lock)
        pc_sound_lock(); /* the audio thread ticks the driver: not while its state is copied */
    if (m->spu_size && !pc_spu_state_load(m->data + at, m->spu_size)) {
        if (pc_sound_unlock)
            pc_sound_unlock();
        return -1;
    }
    at = 0;
    for (int i = 0; i < m->count; i++) {
        if (m->ranges[i].size)
            memcpy(m->ranges[i].start, m->data + at, m->ranges[i].size);
        at += m->ranges[i].size;
    }
    if (pc_sound_unlock)
        pc_sound_unlock();
    return 0;
}
static int masked(uintptr_t address)
{
    for (int i = 0; i < mask_count; i++)
        if (address >= masks[i].start && address - masks[i].start < masks[i].size)
            return 1;
    return 0;
}
int pc_state_image_stable_difference(const pc_state_image* a, const pc_state_image* repeat,
    const pc_state_image* b, pc_state_difference* difference)
{
    if (difference)
        memset(difference, 0, sizeof *difference);
    if (!same_layout(a, b) || (repeat && !same_layout(a, repeat)))
        return -1;
    size_t at = 0;
    for (int i = 0; i <= a->count; i++) {
        int spu = i == a->count;
        size_t size = spu ? (size_t)a->spu_size : a->ranges[i].size;
        uintptr_t base = spu ? 0 : (uintptr_t)a->ranges[i].start;
        for (size_t j = 0; j < size; j++) {
            if (!(j & (PAGE - 1))) { /* equal pages (nearly all of them) are skipped whole */
                size_t n = size - j < PAGE ? size - j : PAGE;
                if (!memcmp(a->data + at + j, b->data + at + j, n)) {
                    j += n - 1;
                    continue;
                }
            }
            if (a->data[at + j] == b->data[at + j] || (!spu && masked(base + j)) ||
                (repeat && a->data[at + j] != repeat->data[at + j]))
                continue;
            if (difference) {
                difference->address = base + j;
                difference->offset = j;
                difference->expected = a->data[at + j];
                difference->actual = b->data[at + j];
                const char* name = spu ? "spu" : a->ranges[i].name;
                snprintf(difference->range, sizeof difference->range, "%s", name ? name : "unnamed");
            }
            return 1;
        }
        at += size;
    }
    return 0;
}
int pc_state_image_difference(const pc_state_image* a, const pc_state_image* b,
    pc_state_difference* difference)
{
    return pc_state_image_stable_difference(a, NULL, b, difference);
}
int pc_state_image_compare(const pc_state_image* a, const pc_state_image* b)
{
    return pc_state_image_difference(a, b, NULL);
}
void pc_state_mask_clear(void)
{
    if (!mode) {
        mask_count = 0;
        recorded_ticks = 0;
    }
}
int pc_state_mask_add(uintptr_t start, size_t size)
{
    if (mode || !size || size > UINTPTR_MAX - start || mask_count == 64)
        return -1;
    masks[mask_count++] = (mask){start, size};
    recorded_ticks = 0;
    return 0;
}
int pc_state_masks_set(const uintptr_t* starts, const size_t* sizes, int count)
{
    if (mode || count < 0 || count > 64 || (count && (!starts || !sizes)))
        return -1;
    for (int i = 0; i < count; i++)
        if (!sizes[i] || sizes[i] > UINTPTR_MAX - starts[i])
            return -1;
    for (int i = 0; i < count; i++)
        masks[i] = (mask){starts[i], sizes[i]};
    mask_count = count;
    recorded_ticks = 0;
    return 0;
}
static int page_count(const pc_state_image* m)
{
    size_t n = 1; /* SPU, including an absent SPU's zero digest */
    for (int i = 0; i < m->count; i++) {
        size_t k = m->ranges[i].size / PAGE + (m->ranges[i].size % PAGE != 0);
        if (k > (size_t)INT_MAX - n)
            return -1;
        n += k;
    }
    return (int)n;
}
static void digest(const pc_state_image* m, uint64_t* out)
{
    size_t base = 0;
    int k = 0;
    for (int i = 0; i < m->count; i++) {
        size_t at = 0;
        while (at < m->ranges[i].size) {
            size_t n = m->ranges[i].size - at;
            if (n > PAGE) n = PAGE;
            uint64_t h = UINT64_C(14695981039346656037);
            for (size_t j = 0; j < n; j++) {
                unsigned char v = masked((uintptr_t)m->ranges[i].start + at + j) ? 0 : m->data[base + at + j];
                h = (h ^ v) * UINT64_C(1099511628211);
            }
            out[k++] = h;
            at += n;
        }
        base += m->ranges[i].size;
    }
    uint64_t h = UINT64_C(14695981039346656037);
    for (int j = 0; j < m->spu_size; j++)
        h = (h ^ m->data[base + (size_t)j]) * UINT64_C(1099511628211);
    out[k] = m->spu_size ? h : 0;
}
static void page_name(int k, char* out, size_t size)
{
    for (int i = 0; i < layout->count; i++) {
        size_t n = layout->ranges[i].size / PAGE + (layout->ranges[i].size % PAGE != 0);
        if ((size_t)k < n) {
            /* Escape arbitrary range names as JSON; bound each entry. */
            char safe[48];
            const unsigned char* name = (const unsigned char*)layout->ranges[i].name;
            size_t p = 0;
            while (name && *name && p + 2 < sizeof safe) {
                unsigned char c = *name++;
                if (c == '"' || c == '\\') safe[p++] = '\\';
                safe[p++] = c < 32 || c > 126 ? '?' : (char)c;
            }
            safe[p] = 0;
            snprintf(out, size, "%s+%#zx@%#llx", safe, (size_t)k * PAGE,
                (unsigned long long)((uintptr_t)layout->ranges[i].start + (size_t)k * PAGE));
            return;
        }
        k -= (int)n;
    }
    snprintf(out, size, "spu");
}
void pc_digest_stop(void)
{
    mode = 0;
}
int pc_digest_start(int compare, int cap)
{
    if (mode || cap < 0 || (!compare && cap == 0))
        return -1;
    pc_state_image* m = pc_state_capture();
    if (!m) return -1;
    int n = page_count(m);
    if (n < 1 || (size_t)n > SIZE_MAX / sizeof(uint64_t) ||
        (compare && (!recorded_ticks || !recorded || !same_layout(layout, m)))) {
        pc_state_image_free(m);
        return -1;
    }
    if (!compare) {
        if ((size_t)cap > SIZE_MAX / sizeof(uint64_t) / (size_t)n ||
            (size_t)cap > (256u * 1024u * 1024u) / sizeof(uint64_t) / (size_t)n) {
            pc_state_image_free(m);
            return -1;
        }
        uint64_t* r = malloc((size_t)cap * (size_t)n * sizeof *r);
        uint64_t* scratch = malloc((size_t)n * sizeof *scratch);
        if (!r || !scratch) {
            free(r); free(scratch); pc_state_image_free(m);
            return -1;
        }
        free(recorded); free(now); pc_state_image_free(layout);
        recorded = r; now = scratch; layout = m;
        capacity = cap; recorded_ticks = 0; pages = n;
    } else {
        pc_state_image_free(m);
    }
    ticks = 0; first_tick = -1; first_pages[0] = 0; error = 0;
    mode = compare ? 2 : 1;
    return 0;
}
int pc_digest_record_masks(int cap, const uintptr_t* starts, const size_t* sizes, int count)
{
    mask previous[64];
    memcpy(previous, masks, sizeof previous);
    int old_count = mask_count, old_ticks = recorded_ticks;
    if (pc_state_masks_set(starts, sizes, count)) return -1;
    if (!pc_digest_start(0, cap)) return 0;
    memcpy(masks, previous, sizeof masks);
    mask_count = old_count;
    recorded_ticks = old_ticks;
    return -1;
}
int pc_digest_result(char* out, int size)
{
    if (size < 0 || (!out && size)) return -1;
    int n = snprintf(out, (size_t)size,
        "{\"recorded\":%d,\"ticks\":%d,\"pages\":%d,\"first_tick\":%d,\"first_pages\":[%s],\"error\":%d",
        recorded_ticks, ticks, pages, first_tick, first_pages, error);
    if (n < 0) return -1;
    int m = snprintf(n < size ? out + n : NULL, n < size ? (size_t)(size - n) : 0, "}");
    return m < 0 ? -1 : n + m;
}
void pc_state_diff_vsync(void)
{
    if (!mode) return;
    pc_state_image* m = pc_state_capture();
    if (!m || !same_layout(layout, m)) {
        pc_state_image_free(m);
        error = -1;
        pc_digest_stop();
        return;
    }
    digest(m, now);
    pc_state_image_free(m);
    if (mode == 1) {
        memcpy(recorded + (size_t)ticks * pages, now, (size_t)pages * sizeof *now);
        recorded_ticks = ++ticks;
        if (ticks == capacity) pc_digest_stop();
    } else {
        if (first_tick < 0) {
            size_t used = 0;
            int listed = 0;
            for (int k = 0; k < pages; k++) {
                if (recorded[(size_t)ticks * pages + k] == now[k]) continue;
                first_tick = ticks;
                if (listed == 24) continue;
                char name[128];
                page_name(k, name, sizeof name);
                size_t len = strlen(name);
                if (used + len + 4 >= sizeof first_pages) continue;
                int n = snprintf(first_pages + used, sizeof first_pages - used, "%s\"%s\"", listed ? "," : "", name);
                used += (size_t)n;
                listed++;
            }
        }
        ticks++;
        if (ticks == recorded_ticks) pc_digest_stop();
    }
}
