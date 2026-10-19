/* State captures and masked page digests; intentionally outside saved state. */
#include "state_diff.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int pc_spu_state_size(void) __attribute__((weak));
extern void pc_spu_state_save(void*) __attribute__((weak));
extern int pc_spu_state_load(const void*, int) __attribute__((weak));
/* Ownership is optional in standalone tools/tests. Use its checked API. */
extern int pc_owner_count(void) __attribute__((weak));
extern int pc_owner_get(int) __attribute__((weak));
extern int pc_owner_set(int, int) __attribute__((weak));

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
static int alternate = -1, previous_owner;
static uint64_t* recorded;
static uint64_t* now;
static pc_state_image* layout;
static char first_pages[1024];

static int describe(pc_state_image* m)
{
    m->count = pc_state_ranges(m->ranges, MAX_RANGES);
    if (m->count <= 0 || m->count > MAX_RANGES)
        return -1;
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
pc_state_image* pc_state_capture(void)
{
    pc_state_image* m = calloc(1, sizeof *m);
    if (!m)
        return NULL;
    if (describe(m) || !(m->data = malloc(m->bytes ? m->bytes : 1))) {
        pc_state_image_free(m);
        return NULL;
    }
    size_t at = 0;
    for (int i = 0; i < m->count; i++) {
        if (m->ranges[i].size)
            memcpy(m->data + at, m->ranges[i].start, m->ranges[i].size);
        at += m->ranges[i].size;
    }
    if (m->spu_size)
        pc_spu_state_save(m->data + at);
    return m;
}
void pc_state_image_free(pc_state_image* m)
{
    if (m) {
        free(m->data);
        free(m);
    }
}
int pc_state_restore(const pc_state_image* m)
{
    pc_state_image live;
    if (!m || describe(&live) || !same_layout(m, &live))
        return -1;
    size_t at = m->bytes - (size_t)m->spu_size;
    if (m->spu_size && !pc_spu_state_load(m->data + at, m->spu_size))
        return -1;
    at = 0;
    for (int i = 0; i < m->count; i++) {
        if (m->ranges[i].size)
            memcpy(m->ranges[i].start, m->data + at, m->ranges[i].size);
        at += m->ranges[i].size;
    }
    return 0;
}
static int masked(uintptr_t address)
{
    for (int i = 0; i < mask_count; i++)
        if (address >= masks[i].start && address - masks[i].start < masks[i].size)
            return 1;
    return 0;
}
int pc_state_image_compare(const pc_state_image* a, const pc_state_image* b)
{
    if (!same_layout(a, b))
        return -1;
    size_t at = 0;
    for (int i = 0; i < a->count; i++) {
        for (size_t j = 0; j < a->ranges[i].size; j++)
            if (!masked((uintptr_t)a->ranges[i].start + j) && a->data[at + j] != b->data[at + j])
                return 1;
        at += a->ranges[i].size;
    }
    return memcmp(a->data + at, b->data + at, (size_t)a->spu_size) != 0;
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
    if (alternate >= 0 && pc_owner_set)
        pc_owner_set(alternate, previous_owner);
    alternate = -1;
    mode = 0;
}
int pc_digest_start(int compare, int cap, int alt)
{
    if (mode || cap < 0 || alt < -1 || (!compare && (cap == 0 || alt != -1)))
        return -1;
    if (alt >= 0 && (!pc_owner_count || !pc_owner_get || !pc_owner_set || alt >= pc_owner_count()))
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
    if (alt >= 0) {
        previous_owner = pc_owner_get(alt);
        if (previous_owner < 0 || pc_owner_set(alt, 0)) return -1;
    }
    alternate = alt;
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
    if (!pc_digest_start(0, cap, -1)) return 0;
    memcpy(masks, previous, sizeof masks);
    mask_count = old_count;
    recorded_ticks = old_ticks;
    return -1;
}
int pc_digest_result(char* out, int size)
{
    if (size < 0 || (!out && size)) return -1;
    return snprintf(out, (size_t)size,
        "{\"recorded\":%d,\"ticks\":%d,\"pages\":%d,\"first_tick\":%d,\"first_pages\":[%s],\"error\":%d}",
        recorded_ticks, ticks, pages, first_tick, first_pages, error);
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
        else if (alternate >= 0 && pc_owner_set(alternate, ticks & 1)) {
            error = -1;
            pc_digest_stop();
        }
    }
}
