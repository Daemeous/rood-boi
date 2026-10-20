/* Standalone: link with state_diff.c; no game, renderer or audio dependency.
 * Also build with -DSTATE_DIFF_TEST_OWNERSHIP to test optional owner routing.
 * -DSTATE_DIFF_TEST_ALLOC -Wl,--wrap=malloc -Wl,--wrap=calloc tests OOM. */
#include "state_diff.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char ram[4096 * 40 + 17], small[19], spu[37];
static int range_mode, spu_size = sizeof spu, load_ok = 1;
#ifdef STATE_DIFF_TEST_ALLOC
static int fail_alloc, allocation, fail_at;
void* __real_malloc(size_t);
void* __real_calloc(size_t, size_t);
static int allocation_fails(void) { return fail_alloc || (++allocation == fail_at); }
void* __wrap_malloc(size_t n) { return allocation_fails() ? NULL : __real_malloc(n); }
void* __wrap_calloc(size_t n, size_t s) { return allocation_fails() ? NULL : __real_calloc(n, s); }
#endif
#ifdef STATE_DIFF_TEST_OWNERSHIP
static int owner = 1;
int pc_owner_count(void) { return 1; }
int pc_owner_get(int i) { return i == 0 ? owner : -1; }
int pc_owner_set(int i, int v) { if (i || v < 0 || v > 3) return -1; owner = v; return 0; }
static int alternate_resets;
int pc_owner_alternate_reset(int i) { if (i) return -1; alternate_resets++; return 0; }
int pc_owner_alternate_report(int i, char* out, int size)
{
    return i ? -1 : snprintf(out, (size_t)size, "[{\"function\":\"f\",\"owned\":2,\"original\":1}]");
}
#endif
int pc_state_ranges(pc_state_range* out, int max)
{
    if (range_mode == 1) return 0;
    if (range_mode == 2) return 171;
    if (max < 2) return 2;
    out[0] = (pc_state_range){(char*)ram, sizeof ram, "a-long-range-name-with-\"quote\"-and-\\slash-and-more-characters"};
    out[1] = (pc_state_range){(char*)small, sizeof small, "small"};
    if (range_mode == 3) out[0].size = SIZE_MAX;
    if (range_mode == 4) out[1].size--;
    return 2;
}
int pc_spu_state_size(void) { return spu_size; }
void pc_spu_state_save(void* p) { memcpy(p, spu, (size_t)spu_size); }
int pc_spu_state_load(const void* p, int size)
{
    if (!load_ok || size != spu_size) return 0;
    memcpy(spu, p, (size_t)size);
    return 1;
}
static void tick(int n)
{
    ram[n % sizeof ram] ^= (unsigned char)(n + 1);
    small[n % sizeof small] += (unsigned char)n;
    spu[n % sizeof spu] ^= (unsigned char)(n * 7);
    pc_state_diff_vsync();
}
static void result_has(const char* text)
{
    char result[2048];
    int n = pc_digest_result(result, sizeof result);
    assert(n > 0 && n < (int)sizeof result);
    assert(strstr(result, text));
}
int main(void)
{
    assert(pc_digest_start(1, 0, -1) == -1);
    assert(pc_digest_start(0, 0, -1) == -1);
    assert(pc_digest_start(0, -1, -1) == -1);
    assert(pc_digest_start(0, INT_MAX, -1) == -1);
    assert(pc_state_masks_set(NULL, NULL, 0) == 0);
    assert(pc_state_masks_set(NULL, NULL, 1) == -1);
    assert(pc_state_masks_set(NULL, NULL, 65) == -1);
    assert(pc_digest_start(1, -1, -1) == -1);
    assert(pc_digest_start(0, 1, -2) == -1);
    assert(pc_digest_start(1, 1, INT_MAX) == -1);
    assert(pc_digest_result(NULL, -1) == -1);
    assert(pc_digest_result(NULL, 1) == -1);
    assert(pc_digest_result(NULL, 0) > 0);
    char tiny[3] = {'x','x','x'};
    assert(pc_digest_result(tiny, 2) > 2 && tiny[1] == 0 && tiny[2] == 'x');
    assert(pc_state_restore(NULL) == -1);
    assert(pc_state_image_compare(NULL, NULL) == -1);
    pc_state_image_free(NULL);
    for (range_mode = 1; range_mode <= 3; range_mode++) assert(!pc_state_capture());
    range_mode = 0;
    spu_size = -1; assert(!pc_state_capture()); spu_size = sizeof spu;
    pc_state_image* initial = pc_state_capture();
    assert(initial);
    assert(pc_digest_start(0, 600, -1) == 0);
    assert(pc_digest_start(0, 1, -1) == -1);
    assert(pc_state_mask_add((uintptr_t)ram, 1) == -1);
    assert(pc_state_masks_set(NULL, NULL, 0) == -1);
    for (int n = 0; n < 600; n++) tick(n);
    for (int n = 0; n < 20; n++) pc_state_diff_vsync();
    result_has("\"recorded\":600,\"ticks\":600");
    pc_digest_stop();
    assert(pc_digest_record_masks(INT_MAX, NULL, NULL, 0) == -1);
    result_has("\"recorded\":600");
    assert(pc_state_restore(initial) == 0);
    assert(pc_digest_start(1, 0, -1) == 0);
    for (int n = 0; n < 600; n++) tick(n);
    result_has("\"first_tick\":-1"); result_has("\"error\":0");
    assert(pc_state_restore(initial) == 0);
    assert(pc_digest_start(1, 0, -1) == 0);
    for (int n = 0; n < 600; n++) {
        if (n == 123) memset(ram, 0xff, sizeof ram);
        tick(n);
    }
    result_has("\"first_tick\":123");
    result_has("\\\"quote\\\"");
    assert(pc_state_restore(initial) == 0);
    ram[9] = 1;
    pc_state_image* changed = pc_state_capture(); assert(changed);
    assert(pc_state_image_compare(initial, changed) == 1);
    pc_state_difference difference;
    assert(pc_state_image_difference(initial, changed, &difference) == 1);
    assert(difference.address == (uintptr_t)&ram[9] && difference.offset == 9);
    assert(difference.expected == 0 && difference.actual == 1);
    assert(strstr(difference.range, "quote"));
    assert(pc_state_image_stable_difference(initial, changed, changed, &difference) == 0);
    ram[10] = 4;
    pc_state_image* stable_fault = pc_state_capture(); assert(stable_fault);
    assert(pc_state_image_stable_difference(initial, changed, stable_fault, &difference) == 1);
    assert(difference.address == (uintptr_t)&ram[10] && difference.actual == 4);
    pc_state_image_free(stable_fault);
    ram[10] = 0;
    assert(pc_state_image_difference(NULL, changed, &difference) == -1);
    assert(!difference.address && !difference.range[0]);
    assert(pc_state_mask_add((uintptr_t)ram + 9, 1) == 0);
    assert(pc_state_image_compare(initial, changed) == 0);
    assert(pc_state_image_difference(initial, changed, &difference) == 0);
    assert(!difference.address && !difference.range[0]);
    spu[3] = 7;
    pc_state_image* sound_changed = pc_state_capture(); assert(sound_changed);
    assert(pc_state_image_difference(initial, sound_changed, &difference) == 1);
    assert(!strcmp(difference.range, "spu") && difference.address == 3 && difference.offset == 3);
    assert(difference.expected == 0 && difference.actual == 7);
    pc_state_image_free(sound_changed);
    spu[3] = 0;
    assert(pc_digest_start(1, 0, -1) == -1); /* changed mask invalidates baseline */
    assert(pc_state_mask_add(UINTPTR_MAX, 2) == -1);
    assert(pc_state_mask_add(0, 0) == -1);
    assert(pc_digest_start(0, 1, -1) == 0); pc_state_diff_vsync();
    ram[9]++;
    assert(pc_digest_start(1, 0, -1) == 0); pc_state_diff_vsync();
    result_has("\"first_tick\":-1");
    spu[0]++;
    assert(pc_digest_start(1, 0, -1) == 0); pc_state_diff_vsync();
    result_has("\"first_pages\":[\"spu\"]");
#ifdef STATE_DIFF_TEST_OWNERSHIP
    /* Alternate is per call (owner 3, the trampolines flip), not per vsync:
     * the owner stays 3 for the whole run and is restored when it ends. */
    assert(pc_digest_start(1, 0, 0) == 0); assert(owner == 3 && alternate_resets == 1);
    pc_state_diff_vsync(); assert(owner == 1);
    result_has("\"alternate\":0,\"alternate_calls\":[{\"function\":\"f\",\"owned\":2,\"original\":1}]}");
#else
    assert(pc_digest_start(1, 0, 0) == -1);
#endif
    range_mode = 4; assert(pc_state_restore(initial) == -1);
    assert(pc_digest_start(1, 0, -1) == -1); range_mode = 0;
    load_ok = 0; assert(pc_state_restore(initial) == -1); load_ok = 1;
    assert(pc_digest_start(0, 2, -1) == 0);
    range_mode = 4; pc_state_diff_vsync(); range_mode = 0;
    result_has("\"error\":-1"); result_has("\"recorded\":0");
    assert(pc_digest_start(1, 0, -1) == -1);
    pc_state_mask_clear();
    for (int i = 0; i < 64; i++) assert(pc_state_mask_add((uintptr_t)ram + i, 1) == 0);
    assert(pc_state_mask_add((uintptr_t)ram, 1) == -1);
    pc_state_mask_clear();
#ifdef STATE_DIFF_TEST_ALLOC
    for (fail_at = 1; fail_at <= 4; fail_at++) {
        allocation = 0;
        assert(pc_digest_start(0, 2, -1) == -1);
    }
    fail_at = 0;
    fail_alloc = 1;
    assert(!pc_state_capture()); assert(pc_digest_start(0, 2, -1) == -1);
    fail_alloc = 0;
    assert(pc_digest_start(0, 2, -1) == 0);
    fail_alloc = 1; pc_state_diff_vsync(); fail_alloc = 0;
    result_has("\"error\":-1");
#endif
    pc_state_image_free(changed); pc_state_image_free(initial);
    puts("state_diff_test: passed");
    return 0;
}
