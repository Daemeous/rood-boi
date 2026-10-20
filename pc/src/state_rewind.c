/* In-process oracle checkpoint at the vsync boundary (snapshot.c). Host
 * allocations stay outside snapshot ranges; restoration runs on a side stack
 * because the game stack itself is restored. */
#include "state_diff.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || !(defined(__i386__) || defined(__x86_64__))
int pc_snapshot_mark_point(char* error, int size)
{
    snprintf(error, (size_t)size, "rewind needs Linux on x86");
    return -1;
}
int pc_snapshot_rewind(char* error, int size) { return pc_snapshot_mark_point(error, size); }
#else
/* Matches snapshot.c's unmangled register context. */
typedef struct {
    uintptr_t regs[8];
    uint32_t mxcsr;
    uint16_t fpu_cw;
} snap_context;
/* snapshot.c: the game side of the current vsync's boundary, and entering
 * the game there (a resume uses the same pair). */
extern snap_context pc_snap_boundary;
extern void pc_snap_enter(const snap_context*, void (*)(void)) __attribute__((noreturn));
extern void pc_tick_resumed(void); /* boot_runtime.c */
extern void pc_rpc_rewound(void);  /* dev_rpc.c: reply from the resumed vsync */
extern char* pc_game_stack(size_t*);
extern int pc_spu_state_size(void) __attribute__((weak));
extern void pc_spu_state_save(void*) __attribute__((weak));
extern int pc_spu_state_load(const void*, int) __attribute__((weak));
extern int pc_fog_state_size(void) __attribute__((weak));
extern void pc_fog_state_save(void*) __attribute__((weak));
extern int pc_fog_state_load(const void*, int) __attribute__((weak));
extern void pc_cd_restored(void) __attribute__((weak));
extern void pc_hw_resync(void) __attribute__((weak));
extern void pc_input_rewound(void) __attribute__((weak));
#define MAX_RANGES 170
static struct {
    int valid, count, spu_size, fog_size;
    snap_context context;
    pc_state_range ranges[MAX_RANGES];
    char *memory, *stack, *spu, *fog;
    uintptr_t sp;
    size_t stack_bytes;
} mark;

extern int pc_game_snapshot_safe(void) __attribute__((weak));
__attribute__((noinline)) int pc_snapshot_mark_point(char* error, int size)
{
    if (pc_game_snapshot_safe && !pc_game_snapshot_safe()) {
        snprintf(error, (size_t)size, "mark refused: owned frame live or incomplete unwind");
        return -1;
    }
    /* Invalidate before replacing: an allocation failure must not leave a
     * context paired with an older image. The mark is the current vsync's
     * boundary (the game side only); a rewind re-enters the game there. */
    mark.valid = 0;
    mark.context = pc_snap_boundary;
    size_t stack_size;
    char* stack = pc_game_stack(&stack_size);
    uintptr_t sp = mark.context.regs[sizeof(void*) == 8 ? 6 : 4];
    uintptr_t top = (uintptr_t)stack + stack_size;
    if (!stack || sp < (uintptr_t)stack || sp >= top) {
        snprintf(error, (size_t)size, "not on the game stack");
        return -1;
    }
    mark.count = pc_state_ranges(mark.ranges, MAX_RANGES);
    if (mark.count <= 0 || mark.count > MAX_RANGES) {
        snprintf(error, (size_t)size, "invalid state ranges");
        return -1;
    }
    size_t bytes = 0;
    for (int i = 0; i < mark.count; i++) {
        if (mark.ranges[i].size > SIZE_MAX - bytes)
            goto oom;
        bytes += mark.ranges[i].size;
    }
    mark.spu_size = pc_spu_state_size ? pc_spu_state_size() : 0;
    mark.fog_size = pc_fog_state_size ? pc_fog_state_size() : 0;
    if (mark.spu_size < 0 || mark.fog_size < 0)
        goto oom;
    free(mark.memory); free(mark.stack); free(mark.spu); free(mark.fog);
    mark.memory = malloc(bytes ? bytes : 1);
    mark.stack_bytes = top - sp;
    mark.stack = malloc(mark.stack_bytes);
    mark.spu = mark.spu_size ? malloc((size_t)mark.spu_size) : NULL;
    mark.fog = mark.fog_size ? malloc((size_t)mark.fog_size + 1) : NULL;
    if (!mark.memory || !mark.stack || (mark.spu_size && !mark.spu)
        || (mark.fog_size && !mark.fog))
        goto oom;
    char* p = mark.memory;
    for (int i = 0; i < mark.count; i++) {
        memcpy(p, mark.ranges[i].start, mark.ranges[i].size);
        p += mark.ranges[i].size;
    }
    if (mark.spu) pc_spu_state_save(mark.spu);
    if (mark.fog) pc_fog_state_save(mark.fog);
    mark.sp = sp;
    memcpy(mark.stack, (void*)sp, mark.stack_bytes);
    mark.valid = 1;
    return 0;
oom:
    snprintf(error, (size_t)size, "cannot allocate checkpoint");
    return -1;
}

__attribute__((noreturn)) void pc_rewind_call_on_stack(void*, void (*)(void));
#ifdef __x86_64__
__asm__(".text\n.globl pc_rewind_call_on_stack\n.type pc_rewind_call_on_stack,@function\n"
        "pc_rewind_call_on_stack:\n movq %rdi,%rsp\n andq $-16,%rsp\n callq *%rsi\n ud2\n"
        ".size pc_rewind_call_on_stack,.-pc_rewind_call_on_stack\n");
#else
__asm__(".text\n.globl pc_rewind_call_on_stack\n.type pc_rewind_call_on_stack,@function\n"
        "pc_rewind_call_on_stack:\n movl 4(%esp),%eax\n movl 8(%esp),%ecx\n"
        " movl %eax,%esp\n andl $-16,%esp\n calll *%ecx\n ud2\n"
        ".size pc_rewind_call_on_stack,.-pc_rewind_call_on_stack\n");
#endif
static __attribute__((noreturn)) void restore(void)
{
    const char* p = mark.memory;
    for (int i = 0; i < mark.count; i++) {
        memcpy(mark.ranges[i].start, p, mark.ranges[i].size);
        p += mark.ranges[i].size;
    }
    memcpy((void*)mark.sp, mark.stack, mark.stack_bytes);
    if (mark.spu) pc_spu_state_load(mark.spu, mark.spu_size);
    if (mark.fog) pc_fog_state_load(mark.fog, mark.fog_size);
    if (pc_cd_restored) pc_cd_restored();
    if (pc_hw_resync) pc_hw_resync();
    if (pc_input_rewound) pc_input_rewound();
    /* Same process/thread: the TLS stack canary has not changed. The rest of
     * the marked vsync runs again from the boundary, as after a resume. */
    pc_rpc_rewound();
    pc_snap_enter(&mark.context, pc_tick_resumed);
}
int pc_snapshot_rewind(char* error, int size)
{
    static char* side;
    enum { SIDE = 1 << 20 };
    if (!mark.valid) {
        snprintf(error, (size_t)size, "no mark (snapshot mark first)");
        return -1;
    }
    if (!side && !(side = malloc(SIDE))) {
        snprintf(error, (size_t)size, "cannot allocate rewind stack");
        return -1;
    }
    pc_rewind_call_on_stack(side + SIDE, restore);
}
#endif
