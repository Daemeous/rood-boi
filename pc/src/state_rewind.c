/* In-process oracle checkpoint at the vsync boundary (snapshot.c). Host
 * allocations stay outside snapshot ranges; restoration runs on a side stack
 * because the game stack itself is restored. Slots: 0 the RPC's (snapshot
 * mark / rewind: the RPC pauses at the rewound vsync and the hardware target
 * is rebuilt from native VRAM), 1 the photo mode's (photo.cpp: the frozen
 * moment a render returns to, the picture kept, the RPC left as it is), and
 * PC_REWIND_TAKE_FIRST.. the cutscene player's ring (cutscene.cpp; restored
 * like the RPC's). A slot's bookkeeping is allocated on first use. */
#include "snapshot_context.h"
#include "state_diff.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !(defined(__i386__) || defined(__x86_64__)) || (defined(_WIN32) && !defined(__x86_64__))
int pc_snapshot_mark_point(char* error, int size)
{
    snprintf(error, (size_t)size, "rewind needs x86 (Linux or 64-bit Windows)");
    return -1;
}
int pc_snapshot_rewind(char* error, int size) { return pc_snapshot_mark_point(error, size); }
int pc_snapshot_mark_slot(int slot, char* error, int size) { return (void)slot, pc_snapshot_mark_point(error, size); }
int pc_snapshot_rewind_slot(int slot, char* error, int size) { return (void)slot, pc_snapshot_mark_point(error, size); }
void pc_snapshot_drop_slot(int slot) { (void)slot; }
int pc_snapshot_slot_valid(int slot) { return (void)slot, 0; }
size_t pc_snapshot_slot_bytes(int slot) { return (void)slot, 0; }
#else
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
extern void pc_cd_release(void) __attribute__((weak));
extern void pc_hw_resync(void) __attribute__((weak));
extern void pc_input_rewound(void) __attribute__((weak));
#define MAX_RANGES 170
#define PC_REWIND_TAKE_FIRST 2   /* cutscene.cpp's slots: PC_REWIND_TAKE_FIRST .. + PC_REWIND_TAKE_SLOTS - 1 */
#define PC_REWIND_TAKE_SLOTS 512
#define SLOTS (PC_REWIND_TAKE_FIRST + PC_REWIND_TAKE_SLOTS) /* 0: the RPC's, 1: the photo mode's */
#define PHOTO_SLOT 1
static struct rewind_mark {
    int valid, count, spu_size, fog_size;
    snap_context context;
    pc_state_range ranges[MAX_RANGES];
    char *memory, *stack, *spu, *fog;
    uintptr_t sp;
    size_t stack_bytes, bytes;
    size_t memory_cap, stack_cap, spu_cap, fog_cap; /* kept between marks of a slot (no fresh pages to fault in) */
} * marks[SLOTS];
/* A slot's buffer at least `need` bytes: the one it has when it is big enough. */
static int reserve(char** buffer, size_t* capacity, size_t need)
{
    if (*buffer && *capacity >= need)
        return 1;
    free(*buffer);
    *buffer = malloc(need ? need : 1);
    *capacity = *buffer ? need : 0;
    return *buffer != NULL;
}
static int restoring; /* the slot restore() puts back */

extern int pc_game_snapshot_safe(void) __attribute__((weak));
__attribute__((noinline)) int pc_snapshot_mark_slot(int slot, char* error, int size)
{
    if (slot < 0 || slot >= SLOTS) {
        snprintf(error, (size_t)size, "no slot %d", slot);
        return -1;
    }
    if (!marks[slot] && !(marks[slot] = calloc(1, sizeof *marks[slot]))) {
        snprintf(error, (size_t)size, "cannot allocate checkpoint");
        return -1;
    }
    struct rewind_mark* m = marks[slot];
#define mark (*m)
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
    uintptr_t sp = CTX_SP(&mark.context);
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
    mark.stack_bytes = top - sp;
    if (!reserve(&mark.memory, &mark.memory_cap, bytes) || !reserve(&mark.stack, &mark.stack_cap, mark.stack_bytes)
        || (mark.spu_size && !reserve(&mark.spu, &mark.spu_cap, (size_t)mark.spu_size))
        || (mark.fog_size && !reserve(&mark.fog, &mark.fog_cap, (size_t)mark.fog_size + 1)))
        goto oom;
    char* p = mark.memory;
    for (int i = 0; i < mark.count; i++) {
        memcpy(p, mark.ranges[i].start, mark.ranges[i].size);
        p += mark.ranges[i].size;
    }
    if (mark.spu_size) pc_spu_state_save(mark.spu);
    if (mark.fog_size) pc_fog_state_save(mark.fog);
    mark.sp = sp;
    memcpy(mark.stack, (void*)sp, mark.stack_bytes);
    mark.bytes = bytes + mark.stack_bytes + (size_t)mark.spu_size + (size_t)mark.fog_size;
    mark.valid = 1;
    return 0;
oom:
    snprintf(error, (size_t)size, "cannot allocate checkpoint");
    return -1;
#undef mark
}
__attribute__((noinline)) int pc_snapshot_mark_point(char* error, int size) { return pc_snapshot_mark_slot(0, error, size); }
/* The slot's memory given back (the photo mode's moment, once resumed). */
void pc_snapshot_drop_slot(int slot)
{
    if (slot < 0 || slot >= SLOTS || !marks[slot])
        return;
    struct rewind_mark* m = marks[slot];
    m->valid = 0;
    free(m->memory); free(m->stack); free(m->spu); free(m->fog);
    m->memory = m->stack = m->spu = m->fog = NULL;
    m->bytes = m->memory_cap = m->stack_cap = m->spu_cap = m->fog_cap = 0;
}
int pc_snapshot_slot_valid(int slot) { return slot >= 0 && slot < SLOTS && marks[slot] && marks[slot]->valid; }
/* The memory a valid slot holds (cutscene.cpp's budget). */
size_t pc_snapshot_slot_bytes(int slot) { return pc_snapshot_slot_valid(slot) ? marks[slot]->bytes : 0; }

__attribute__((noreturn)) void pc_rewind_call_on_stack(void*, void (*)(void));
#ifdef _WIN32
__asm__(".text\n.globl pc_rewind_call_on_stack\n.def pc_rewind_call_on_stack; .scl 2; .type 32; .endef\n"
        "pc_rewind_call_on_stack:\n movq %rcx,%rsp\n andq $-16,%rsp\n subq $32,%rsp\n callq *%rdx\n ud2\n");
#elif defined(__x86_64__)
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
    struct rewind_mark* m = marks[restoring];
#define mark (*m)
    if (pc_cd_release)
        pc_cd_release(); /* the open disc file: closed, not leaked (pc_cd_restored opens it) */
    const char* p = mark.memory;
    for (int i = 0; i < mark.count; i++) {
        memcpy(mark.ranges[i].start, p, mark.ranges[i].size);
        p += mark.ranges[i].size;
    }
    memcpy((void*)mark.sp, mark.stack, mark.stack_bytes);
    if (mark.spu_size) pc_spu_state_load(mark.spu, mark.spu_size);
    if (mark.fog_size) pc_fog_state_load(mark.fog, mark.fog_size);
    if (pc_cd_restored) pc_cd_restored();
    if (pc_input_rewound) pc_input_rewound();
    /* The photo mode's slot keeps the hardware target (the render's picture: photo.cpp resyncs before
     * the next one and at resume) and the RPC as it is (photo.cpp knows it landed). */
    if (restoring != PHOTO_SLOT) {
        if (pc_hw_resync) pc_hw_resync();
        pc_rpc_rewound();
    }
    /* Same process/thread: the TLS stack canary has not changed. The rest of
     * the marked vsync runs again from the boundary, as after a resume. */
    pc_snap_enter(&mark.context, pc_tick_resumed);
#undef mark
}
int pc_snapshot_rewind_slot(int slot, char* error, int size)
{
    static char* side;
    enum { SIDE = 1 << 20 };
    if (!pc_snapshot_slot_valid(slot)) {
        snprintf(error, (size_t)size, "no mark (snapshot mark first)");
        return -1;
    }
    if (!side && !(side = malloc(SIDE))) {
        snprintf(error, (size_t)size, "cannot allocate rewind stack");
        return -1;
    }
    restoring = slot;
    pc_rewind_call_on_stack(side + SIDE, restore);
}
int pc_snapshot_rewind(char* error, int size) { return pc_snapshot_rewind_slot(0, error, size); }
#endif
