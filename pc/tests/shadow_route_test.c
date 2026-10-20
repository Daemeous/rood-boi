#define _GNU_SOURCE
#include "shadow.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int shadow_state, shadow_fault;
static char* stack_base;
static size_t stack_size;
char* pc_game_stack(size_t* size) { *size = stack_size; return stack_base; }
unsigned pc_vsync_count(void) { return 321; }
void pc_unimplemented(const char* what) { fprintf(stderr, "%s\n", what); abort(); }
int pc_state_ranges(pc_state_range* out, int max)
{
    if (max) out[0] = (pc_state_range){(char*)&shadow_state, sizeof shadow_state, "battle.test"};
    return 1;
}
extern int probe_eight__shadow(int, int, int, int, int, int, int, int*);
extern int probe_inner__shadow(int);
int probe_inner(int value) { return probe_inner__shadow(value); }
int probe_inner__orig(int value) { return value + 7; }
int probe_eight__orig(int a, int b, int c, int d, int e, int f, int g, int* output)
{
    shadow_state += 3;
    *output = a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7;
    static int noise;
    if (shadow_fault == 5 || shadow_fault == 6) *output += ++noise;
    return probe_inner(*output);
}
int main(void)
{
    pthread_attr_t attr;
    assert(!pthread_getattr_np(pthread_self(), &attr));
    assert(!pthread_attr_getstack(&attr, (void**)&stack_base, &stack_size));
    assert(!pthread_attr_destroy(&attr));
    for (int fault = 0; fault <= 4; fault++) {
        shadow_fault = fault;
        int output = -1;
        int value = probe_eight__shadow(1, 2, 3, 4, 5, 6, 7, &output);
        const pc_shadow_result* result = pc_shadow_last();
        assert(value == 147 && output == 140 && shadow_state == 3 * (fault + 1));
        assert(result->error == 0 && result->frame == 321);
        assert(!strcmp(result->function, "probe_eight"));
        assert(result->mismatch == (fault != 0));
        if (fault == 1) {
            assert(!strcmp(result->difference.range, "caller.stack"));
            assert(result->difference.address == (uintptr_t)&output);
        }
        if (fault == 2) assert(!strcmp(result->difference.range, "battle.test"));
        if (fault == 3) assert(result->panicked && strstr(result->message, "routed owned panic"));
        if (fault == 4) assert(!strcmp(result->difference.range, "return"));
        printf("routed fault=%d function=%s frame=%u field=%s panic=%s\n", fault,
            result->function, result->frame, result->difference.range, result->message);
    }
    assert(pc_shadow_calls() == 19); /* panic prevents one owned inner call */
    for (int fault = 5; fault <= 6; fault++) {
        shadow_fault = fault;
        int output = -1;
        int value = probe_eight__shadow(1, 2, 3, 4, 5, 6, 7, &output);
        assert(output == 141 + (fault - 5) * 2 && value == output + 7);
        assert(shadow_state == 3 * (fault + 1));
        const pc_shadow_result* result = pc_shadow_last();
        assert(!result->error && result->mismatch == (fault == 6));
        if (fault == 6) assert(!strcmp(result->difference.range, "battle.test"));
    }
    char report[8192], tiny[128];
    assert(pc_shadow_report(NULL, 0, 1) == -1);
    assert(pc_shadow_report(tiny, sizeof tiny, 1) == -1);
    assert(pc_shadow_calls() == 27); /* failed serialization must not reset */
    assert(pc_shadow_report(report, sizeof report, 1) > 0);
    assert(strstr(report, "\"count\":5") && strstr(report, "\"frame\":321"));
    assert(strstr(report, "caller.stack") && strstr(report, "routed owned panic"));
    assert(strstr(report, "\"function\":\"probe_eight\",\"calls\":7,\"nested\":0"));
    assert(strstr(report, "\"function\":\"probe_inner\",\"calls\":0,\"nested\":20"));
    puts(report);
    assert(pc_shadow_calls() == 0);
    assert(pc_shadow_report(report, sizeof report, 0) > 0);
    assert(strstr(report, "\"mismatches\":[]"));
    puts("routed shadow: eight parameters, nested calls, return, caller stack and Zig panic passed");
}
