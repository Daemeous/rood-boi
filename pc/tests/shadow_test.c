#include "shadow.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef SHADOW_ZIG_PROBE
#include <limits.h>
#include <stdlib.h>
extern void shadow_probe_overflow(void*);
void pc_unimplemented(const char* what) { (void)what; abort(); }
#endif

#ifdef SHADOW_ALLOC_PROBE
static int allocation, fail_at;
void* __real_malloc(size_t);
void* __real_calloc(size_t, size_t);
void* __wrap_malloc(size_t n) { return ++allocation == fail_at ? NULL : __real_malloc(n); }
void* __wrap_calloc(size_t n, size_t s) { return ++allocation == fail_at ? NULL : __real_calloc(n, s); }
#endif

static unsigned char state[32];
static int effects, original_calls;
int pc_state_ranges(pc_state_range* out, int max)
{
    if (max) out[0] = (pc_state_range){(char*)state, sizeof state, "battle.actor"};
    return 1;
}
static void original(void* output)
{
    ++original_calls;
    state[7] += 3;
    *(int*)output = 42;
    if (!pc_shadow_speculative()) ++effects;
}
static void same(void* output)
{
    state[7] += 3;
    *(int*)output = 42;
    if (!pc_shadow_speculative()) ++effects;
}
static void wrong(void* output) { same(output); state[7]++; }
static void wrong_output(void* output) { same(output); *(int*)output = 43; }
static void panic_owned(void* output)
{
    same(output);
    state[7] = 99;
    assert(pc_shadow_panic("integer overflow", 16) == 0);
    assert(!"panic must escape");
}
int main(void)
{
    int output = 0;
    pc_state_range range = {(char*)&output, sizeof output, "caller.damage"};
    pc_shadow_result result;
    assert(!pc_shadow_panic("outside", 7));
    assert(!pc_shadow_compare("attack", 123, original, same, &output, &range, 1, &result));
    assert(!result.error && !result.mismatch && state[7] == 3 && output == 42 && effects == 1);
    assert(!pc_shadow_compare("attack", 124, original, wrong, &output, &range, 1, &result));
    assert(!result.error && result.mismatch && state[7] == 6 && result.frame == 124);
    assert(result.difference.address == (uintptr_t)&state[7]);
    assert(!strcmp(result.difference.range, "battle.actor"));
    assert(!pc_shadow_compare("attack", 125, original, wrong_output, &output, &range, 1, &result));
    assert(result.mismatch && !strcmp(result.difference.range, "caller.damage") && output == 42);
    assert(!pc_shadow_compare("attack", 126, original, panic_owned, &output, &range, 1, &result));
    assert(!result.error && result.panicked && result.mismatch && state[7] == 12 && output == 42);
    assert(!strcmp(result.message, "integer overflow") && effects == 4 && original_calls == 4);
    assert(!pc_shadow_speculative() && !pc_shadow_panic("outside", 7));
    pc_state_range overlapping[] = {range, range};
    assert(pc_shadow_compare("attack", 127, original, same, &output, overlapping, 2, &result) == -1);
    assert(original_calls == 4);
    assert(!pc_shadow_compare("attack", 128, original, same, &output, &range, 1, &result));
    assert(!result.error && !result.mismatch && state[7] == 15);
#ifdef SHADOW_ZIG_PROBE
    output = INT_MAX;
    assert(!pc_shadow_compare("shadow_probe_overflow", 129, original, shadow_probe_overflow,
        &output, &range, 1, &result));
    assert(!result.error && result.panicked && result.mismatch && output == 42 && state[7] == 18);
    assert(strstr(result.message, "overflow") && result.frame == 129);
    printf("caught Zig panic: function=%s frame=%u message=%s\n", result.function, result.frame, result.message);
#endif
#ifdef SHADOW_ALLOC_PROBE
    for (fail_at = 1; fail_at <= 8; fail_at++) {
        allocation = 0;
        output = 123;
        state[7] = 0;
        int calls = original_calls;
        int status = pc_shadow_compare("allocation", 130, original, same, &output, &range, 1, &result);
        if (fail_at <= 4) {
            assert(status == -1 && result.error == -1);
            assert(original_calls == calls && output == 123 && state[7] == 0);
        } else {
            assert(!status && result.error == -2);
            assert(original_calls == calls + 1 && output == 42 && state[7] == 3);
        }
        assert(!pc_shadow_speculative());
    }
    fail_at = 0;
#endif
    puts("shadow transaction: equality, mismatch, caller outputs, panic, rollback and hook isolation passed");
    return 0;
}
