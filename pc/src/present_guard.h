/* The present canary's call guard (T1 R4a), light enough for any unit to include. */
#ifndef PC_PRESENT_GUARD_H
#define PC_PRESENT_GUARD_H
#ifdef __cplusplus
extern "C" {
#endif
extern __thread int pc_present_inside; /* this thread is inside a present (the renderer's: a header read is the header's) */
/* Call guard (ROOD_PRESENT_CANARY_CALLS=1): a function that reads game-owned state, called while the presenter runs and not through the
 * header, is reported (once per name, with a count at exit; ROOD_PRESENT_CANARY_ABORT=1 aborts at the first). Put PC_PRESENT_GUARD("name")
 * first in such a function. The header's own fill (before the presenter) is outside it. */
extern int pc_present_guard_on, pc_present_paused;
void pc_present_violation(const char* what);
#define PC_PRESENT_GUARD(what) \
    do { \
        if (pc_present_guard_on && pc_present_inside && !pc_present_paused) \
            pc_present_violation(what); \
    } while (0)
#ifdef __cplusplus
}
#endif
#endif
