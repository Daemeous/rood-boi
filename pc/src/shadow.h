/* Shadow transaction backend. Callbacks run on the game thread only. */
#ifndef PC_SHADOW_H
#define PC_SHADOW_H
#include "state_diff.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pc_shadow_call)(void*);
typedef struct {
    const char* function;
    unsigned frame;
    int mismatch, panicked, error;
    pc_state_difference difference;
    char message[256];
} pc_shadow_result;
/* Callers must isolate host effects while pc_shadow_speculative() is true.
 * Outputs describe disjoint writable caller buffers, not callback arguments
 * or this function's stack. Original results and outputs always win.
 * No nested transactions. Returns -1 if unavailable before executing original.
 * A restore failure is fatal: continuing with partly restored state is unsafe. */
int pc_shadow_compare(const char* function, unsigned frame,
    pc_shadow_call original, pc_shadow_call owned, void* context,
    const pc_state_range* outputs, int output_count, pc_shadow_result* result);
int pc_shadow_speculative(void);
/* Routed wrapper ABI: frame is the wrapper context's frame pointer. Nested
 * calls use the enclosing side and are compared as part of that transaction.
 * Return 1 only when nested owned executed (select its return value). */
int pc_shadow_route(const char* name, uintptr_t frame, pc_shadow_call original,
    pc_shadow_call repeat, pc_shadow_call owned, int (*equal)(void*), void* context);
const pc_shadow_result* pc_shadow_last(void);
unsigned pc_shadow_calls(void);
/* Bounded JSON report; returns -1 when the buffer cannot hold the report. */
int pc_shadow_report(char* out, int size, int reset);
/* Explicit test-only fault: xor one captured byte after the named owned call.
 * Requires ROOD_ORACLE=1, idle transaction and a captured address. Empty clears. */
int pc_shadow_set_fault(const char* function, uintptr_t address);
/* Returns 0 outside the owned callback; otherwise escapes to its C guard. */
int pc_shadow_panic(const char* message, size_t length);
#ifdef __cplusplus
}
#endif
#endif
