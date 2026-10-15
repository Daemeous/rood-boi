/* Save states: the whole game state at a vsync, written to a file and
 * resumed in a new process (ROOD_SNAPSHOT=path at launch; RPC snapshot).
 *
 * The game is native C, so its state is not one emulator struct:
 *  - guest memory: RAM (0x80000000), scratchpad, the I/O page;
 *  - the native statics of the game's own units (src/ and the pc/src units
 *    built like them, the SDK layer, the CPU rasterizer and VRAM, GTE, CD
 *    drive, MDEC, XA): build_boot.py moves their .data/.bss into the sections
 *    rood_state_data / rood_state_bss; the overlays' statics are their
 *    rood_ovl_* sections (overlay_statics.c);
 *  - the software SPU (audio_spu.cpp: the sound driver reads envelopes back);
 *  - the interactive fog's fields (fog_sim.cpp: host state that follows the game);
 *  - the game's call stack and registers: the game runs on a stack at a fixed
 *    address (boot_runtime.c), and a snapshot is taken at one place, inside
 *    the paused RPC loop (pc_tick, between frames). Its used part is saved
 *    with the callee-saved registers and the stack-protector canary, and a
 *    new process of the same binary copies it back and jumps into it, so the
 *    snapshot call returns a second time, in the new process.
 * Host state (window, renderer, lights, editor, settings, RPC, input) is not
 * saved: the new process starts its own, and caches derived from the game
 * (the hardware VRAM target, native room/actor data) rebuild from the
 * restored game state (pc_hw_resync and each subsystem's own change checks).
 * The memory card files are the launcher's (ROOD_CARD1/2): a snapshot is
 * valid with the card contents it was made with (pc/tests/snapshots.py keys
 * its cache on them). The original run's log is replayed into the new one.
 *
 * A snapshot only resumes in the binary that wrote it (a hash of its code
 * is checked). Linux only; elsewhere the RPC reports it unavailable. */
#define _GNU_SOURCE
#include "platform.h" /* pc_replace_file */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

extern unsigned pc_vsync_count(void);

#if defined(_WIN32) || !(defined(__i386__) || defined(__x86_64__))
int pc_snapshot_available(void) { return 0; }
int pc_snapshot_point(const char* path, char* error, int size)
{
    (void)path;
    snprintf(error, (size_t)size, "save states need Linux on x86");
    return -1;
}
void pc_snapshot_resume_if_requested(void) { }
int pc_snapshot_hash(char* out, int size)
{
    snprintf(out, (size_t)size, "{}");
    return 0;
}
#else

/* ---- register context: setjmp/longjmp without glibc's pointer mangling
 * (a mangled jmp_buf only works in the process that made it) ---- */
typedef struct {
    uintptr_t regs[8]; /* x86_64: rbx rbp r12 r13 r14 r15 rsp rip; i386: ebx esi edi ebp esp eip */
    uint32_t mxcsr;
    uint16_t fpu_cw;
} snap_context;
int pc_snap_save_context(snap_context* c) __attribute__((returns_twice));
__attribute__((noreturn)) void pc_snap_jump(const snap_context* c, int value);
#ifdef __x86_64__
__asm__(".text\n"
        ".globl pc_snap_save_context\n"
        ".type pc_snap_save_context, @function\n"
        "pc_snap_save_context:\n"
        "    movq %rbx, 0(%rdi)\n"
        "    movq %rbp, 8(%rdi)\n"
        "    movq %r12, 16(%rdi)\n"
        "    movq %r13, 24(%rdi)\n"
        "    movq %r14, 32(%rdi)\n"
        "    movq %r15, 40(%rdi)\n"
        "    leaq 8(%rsp), %rdx\n"
        "    movq %rdx, 48(%rdi)\n"
        "    movq (%rsp), %rdx\n"
        "    movq %rdx, 56(%rdi)\n"
        "    stmxcsr 64(%rdi)\n"
        "    fnstcw 68(%rdi)\n"
        "    xorl %eax, %eax\n"
        "    ret\n"
        ".size pc_snap_save_context, .-pc_snap_save_context\n"
        ".globl pc_snap_jump\n"
        ".type pc_snap_jump, @function\n"
        "pc_snap_jump:\n"
        "    ldmxcsr 64(%rdi)\n"
        "    fldcw 68(%rdi)\n"
        "    movq 0(%rdi), %rbx\n"
        "    movq 8(%rdi), %rbp\n"
        "    movq 16(%rdi), %r12\n"
        "    movq 24(%rdi), %r13\n"
        "    movq 32(%rdi), %r14\n"
        "    movq 40(%rdi), %r15\n"
        "    movq 48(%rdi), %rsp\n"
        "    movl %esi, %eax\n"
        "    jmpq *56(%rdi)\n"
        ".size pc_snap_jump, .-pc_snap_jump\n");
static uintptr_t read_canary(void)
{
    uintptr_t v;
    __asm__ volatile("movq %%fs:0x28, %0" : "=r"(v));
    return v;
}
static void write_canary(uintptr_t v) { __asm__ volatile("movq %0, %%fs:0x28" : : "r"(v) : "memory"); }
#else
__asm__(".text\n"
        ".globl pc_snap_save_context\n"
        ".type pc_snap_save_context, @function\n"
        "pc_snap_save_context:\n"
        "    movl 4(%esp), %eax\n"
        "    movl %ebx, 0(%eax)\n"
        "    movl %esi, 4(%eax)\n"
        "    movl %edi, 8(%eax)\n"
        "    movl %ebp, 12(%eax)\n"
        "    leal 4(%esp), %ecx\n"
        "    movl %ecx, 16(%eax)\n"
        "    movl (%esp), %ecx\n"
        "    movl %ecx, 20(%eax)\n"
        "    stmxcsr 32(%eax)\n"
        "    fnstcw 36(%eax)\n"
        "    xorl %eax, %eax\n"
        "    ret\n"
        ".size pc_snap_save_context, .-pc_snap_save_context\n"
        ".globl pc_snap_jump\n"
        ".type pc_snap_jump, @function\n"
        "pc_snap_jump:\n"
        "    movl 4(%esp), %edx\n"
        "    movl 8(%esp), %eax\n"
        "    ldmxcsr 32(%edx)\n"
        "    fldcw 36(%edx)\n"
        "    movl 0(%edx), %ebx\n"
        "    movl 4(%edx), %esi\n"
        "    movl 8(%edx), %edi\n"
        "    movl 12(%edx), %ebp\n"
        "    movl 16(%edx), %esp\n"
        "    jmp *20(%edx)\n"
        ".size pc_snap_jump, .-pc_snap_jump\n");
static uintptr_t read_canary(void)
{
    uintptr_t v;
    __asm__ volatile("movl %%gs:0x14, %0" : "=r"(v));
    return v;
}
static void write_canary(uintptr_t v) { __asm__ volatile("movl %0, %%gs:0x14" : : "r"(v) : "memory"); }
#endif
#define SAVED_SP(c) ((c)->regs[sizeof(void*) == 8 ? 6 : 4])

/* ---- what is saved ---- */
extern char __start_rood_state_data[] __attribute__((weak)), __stop_rood_state_data[] __attribute__((weak));
extern char __start_rood_state_bss[] __attribute__((weak)), __stop_rood_state_bss[] __attribute__((weak));
extern char __executable_start[], etext[];
struct pc_overlay_statics {
    const char* path;
    char *data_start, *data_end, *bss_start, *bss_end;
};
extern const struct pc_overlay_statics pc_overlay_statics[] __attribute__((weak));
extern const int pc_overlay_statics_count __attribute__((weak));
extern char* pc_game_stack(size_t* size); /* boot_runtime.c */
extern const uint16_t* pc_native_vram(void);
/* Subsystems with state outside those ranges (weak: absent in some builds). */
extern int pc_spu_state_size(void) __attribute__((weak));
extern void pc_spu_state_save(void* out) __attribute__((weak));
extern int pc_spu_state_load(const void* in, int size) __attribute__((weak));
/* fog_sim.cpp: the interactive fog's fields (host state, but it follows the
 * game: a resumed run's fog continues where the saved one was). */
extern int pc_fog_state_size(void) __attribute__((weak));
extern void pc_fog_state_save(void* out) __attribute__((weak));
extern int pc_fog_state_load(const void* in, int size) __attribute__((weak));
extern void pc_cd_restored(void) __attribute__((weak));   /* cd_drive.c: reopen the disc */
extern void pc_hw_resync(void) __attribute__((weak));     /* gpu_hw.c: the target from native VRAM */
extern void pc_rpc_restored(void) __attribute__((weak));  /* dev_rpc.c: listen, paused */

typedef struct {
    char* start;
    size_t size;
    const char* name;
} range;
#define MAX_RANGES 160
static int ranges(range* out)
{
    int n = 0;
    out[n++] = (range) { (char*)(uintptr_t)0x80000000u, 0x200000, "ram" };
    out[n++] = (range) { (char*)(uintptr_t)0x1f800000u, 0x1000, "scratch" };
    out[n++] = (range) { (char*)(uintptr_t)0x1f801000u, 0x1000, "io" };
    if (__start_rood_state_data)
        out[n++] = (range) { __start_rood_state_data, (size_t)(__stop_rood_state_data - __start_rood_state_data), "state_data" };
    if (__start_rood_state_bss)
        out[n++] = (range) { __start_rood_state_bss, (size_t)(__stop_rood_state_bss - __start_rood_state_bss), "state_bss" };
    if (&pc_overlay_statics_count)
        for (int i = 0; i < pc_overlay_statics_count && n + 2 <= MAX_RANGES; i++) {
            const struct pc_overlay_statics* o = &pc_overlay_statics[i];
            int seen = 0; /* byte-identical overlay copies share one region */
            for (int j = 0; j < i; j++)
                seen |= pc_overlay_statics[j].data_start == o->data_start;
            if (seen)
                continue;
            if (o->data_end > o->data_start)
                out[n++] = (range) { o->data_start, (size_t)(o->data_end - o->data_start), o->path };
            if (o->bss_end > o->bss_start)
                out[n++] = (range) { o->bss_start, (size_t)(o->bss_end - o->bss_start), o->path };
        }
    return n;
}

static uint64_t fnv(const void* data, size_t size, uint64_t h)
{
    const unsigned char* p = data;
    for (size_t i = 0; i < size; i++)
        h = (h ^ p[i]) * 1099511628211ull;
    return h;
}
#define FNV0 1469598103934665603ull
static uint64_t code_hash(void) { return fnv(__executable_start, (size_t)(etext - __executable_start), FNV0); }

/* ---- the file ---- */
#define MAGIC "ROODSNAP"
#define VERSION 1
enum { CHUNK_END, CHUNK_MEMORY, CHUNK_SPU, CHUNK_LOG, CHUNK_FOG };
typedef struct {
    char magic[8];
    uint32_t version, pointer_size;
    uint64_t code_hash;
    uint32_t vsync, reserved;
    snap_context context;
    uint64_t canary;
} header;
typedef struct {
    uint32_t tag, reserved;
    uint64_t address, size;
} chunk;

static snap_context context;

static int write_chunk(FILE* f, uint32_t tag, const void* address, const void* data, uint64_t size)
{
    chunk c = { tag, 0, (uint64_t)(uintptr_t)address, size };
    return fwrite(&c, sizeof c, 1, f) == 1 && (!size || fwrite(data, 1, (size_t)size, f) == size);
}

/* The log so far (stderr, when it is a file): replayed by the new process. */
static char* read_log(size_t* size)
{
    struct stat st;
    *size = 0;
    if (fstat(2, &st) || !S_ISREG(st.st_mode))
        return NULL;
    fflush(stderr);
    off_t end = lseek(2, 0, SEEK_CUR);
    if (end <= 0)
        return NULL;
    /* stderr is usually write-only: read the file through a new descriptor. */
    int fd = open("/proc/self/fd/2", O_RDONLY | O_CLOEXEC);
    char* text = fd >= 0 ? malloc((size_t)end) : NULL;
    ssize_t got = text ? pread(fd, text, (size_t)end, 0) : -1;
    if (fd >= 0)
        close(fd);
    *size = got > 0 ? (size_t)got : 0;
    return text;
}

static int write_file(const char* path, uintptr_t sp, char* error, int esize)
{
    size_t stack_size;
    char* stack = pc_game_stack(&stack_size);
    if (!stack) {
        snprintf(error, (size_t)esize, "the game stack is not at its fixed address");
        return -1;
    }
    char* top = stack + stack_size;
    if (sp < (uintptr_t)stack || sp >= (uintptr_t)top) {
        snprintf(error, (size_t)esize, "not on the game stack");
        return -1;
    }
    char part[1100];
    snprintf(part, sizeof part, "%s.part", path);
    FILE* f = fopen(part, "wb");
    if (!f) {
        snprintf(error, (size_t)esize, "cannot write %s", part);
        return -1;
    }
    header h;
    memset(&h, 0, sizeof h);
    memcpy(h.magic, MAGIC, 8);
    h.version = VERSION;
    h.pointer_size = sizeof(void*);
    h.code_hash = code_hash();
    h.vsync = pc_vsync_count();
    h.context = context;
    h.canary = read_canary();
    int ok = fwrite(&h, sizeof h, 1, f) == 1;
    range r[MAX_RANGES];
    int n = ranges(r);
    for (int i = 0; i < n && ok; i++)
        ok = write_chunk(f, CHUNK_MEMORY, r[i].start, r[i].start, r[i].size);
    ok = ok && write_chunk(f, CHUNK_MEMORY, (void*)sp, (void*)sp, (uint64_t)((uintptr_t)top - sp));
    if (ok && pc_spu_state_size) {
        int size = pc_spu_state_size();
        void* spu = malloc((size_t)size);
        ok = spu != NULL;
        if (ok) {
            pc_spu_state_save(spu);
            ok = write_chunk(f, CHUNK_SPU, NULL, spu, (uint64_t)size);
        }
        free(spu);
    }
    if (ok && pc_fog_state_size) {
        int size = pc_fog_state_size();
        void* fog = malloc((size_t)size + 1);
        ok = fog != NULL;
        if (ok) {
            pc_fog_state_save(fog);
            ok = write_chunk(f, CHUNK_FOG, NULL, fog, (uint64_t)size);
        }
        free(fog);
    }
    size_t log_size;
    char* log = read_log(&log_size);
    if (ok && log)
        ok = write_chunk(f, CHUNK_LOG, NULL, log, log_size);
    free(log);
    ok = ok && write_chunk(f, CHUNK_END, NULL, NULL, 0);
    ok = !fclose(f) && ok;
    if (!ok || pc_replace_file(part, path)) {
        remove(part);
        snprintf(error, (size_t)esize, "writing %s failed", path);
        return -1;
    }
    fprintf(stderr, "[snapshot] saved %s at vsync %u (%d ranges, stack %zu bytes)\n", path, h.vsync, n + 1,
        (size_t)((uintptr_t)top - sp));
    return 0;
}

int pc_snapshot_available(void)
{
    size_t size;
    return pc_game_stack(&size) != NULL;
}

/* Save to `path` here: 0 when saved, 1 when this is the new process resuming
 * from such a file (the call returns twice), -1 with `error` when it failed.
 * Called on the game stack (dev_rpc.c's paused loop). */
__attribute__((noinline)) int pc_snapshot_point(const char* path, char* error, int size)
{
    if (pc_snap_save_context(&context))
        return 1;
    return write_file(path, SAVED_SP(&context), error, size);
}

/* ---- resuming ---- */
__attribute__((noreturn)) static void fail(const char* path, const char* why)
{
    fprintf(stderr, "[snapshot] cannot resume %s: %s\n", path, why);
    exit(1);
}

static char* stack_image;
static uintptr_t stack_sp;
static size_t stack_bytes;

/* ROOD_SNAPSHOT=path: called by main instead of starting the game, after the
 * window, audio and overlay statics are initialised; does not return. */
void pc_snapshot_resume_if_requested(void)
{
    const char* path = getenv("ROOD_SNAPSHOT");
    if (!path || !*path)
        return;
    FILE* f = fopen(path, "rb");
    if (!f)
        fail(path, "cannot open");
    header h;
    if (fread(&h, sizeof h, 1, f) != 1 || memcmp(h.magic, MAGIC, 8) || h.version != VERSION)
        fail(path, "not a snapshot of this version");
    if (h.pointer_size != sizeof(void*) || h.code_hash != code_hash())
        fail(path, "made by another build of the game");
    size_t game_stack_size;
    char* stack = pc_game_stack(&game_stack_size);
    if (!stack)
        fail(path, "the game stack is not at its fixed address");
    range r[MAX_RANGES];
    int n = ranges(r), restored = 0, have_stack = 0;
    for (;;) {
        chunk c;
        if (fread(&c, sizeof c, 1, f) != 1)
            fail(path, "truncated");
        if (c.tag == CHUNK_END)
            break;
        char* data = malloc(c.size ? (size_t)c.size : 1);
        if (!data || (c.size && fread(data, 1, (size_t)c.size, f) != c.size))
            fail(path, "truncated");
        if (c.tag == CHUNK_MEMORY) {
            char* at = (char*)(uintptr_t)c.address;
            int known = 0;
            for (int i = 0; i < n; i++)
                if (r[i].start == at && r[i].size == c.size)
                    known = 1;
            if (known) {
                memcpy(at, data, (size_t)c.size);
                ++restored;
                free(data);
            } else if (at >= stack && at + c.size == stack + game_stack_size && !have_stack) {
                /* Copied in just before the jump (this function runs on the
                 * main thread's stack, not that one, but keep it simple). */
                stack_image = data, stack_sp = (uintptr_t)at, stack_bytes = (size_t)c.size;
                have_stack = 1;
            } else
                fail(path, "a memory range this build does not have");
        } else if (c.tag == CHUNK_SPU) {
            if (!pc_spu_state_load || !pc_spu_state_load(data, (int)c.size))
                fail(path, "the SPU state does not fit");
            free(data);
        } else if (c.tag == CHUNK_FOG) {
            if (pc_fog_state_load && !pc_fog_state_load(data, (int)c.size))
                fprintf(stderr, "[snapshot] the interactive fog's state does not fit: it starts at rest\n");
            free(data);
        } else if (c.tag == CHUNK_LOG) {
            fprintf(stderr, "[snapshot] the log of the run that saved %s:\n", path);
            fwrite(data, 1, (size_t)c.size, stderr);
            fprintf(stderr, "[snapshot] (end of the saved log)\n");
            free(data);
        } else
            fail(path, "unknown chunk");
    }
    fclose(f);
    if (restored != n || !have_stack)
        fail(path, "missing ranges");
    if (pc_cd_restored)
        pc_cd_restored();
    if (pc_hw_resync)
        pc_hw_resync();
    if (pc_rpc_restored)
        pc_rpc_restored();
    fprintf(stderr, "[snapshot] resumed %s at vsync %u\n", path, h.vsync);
    fflush(stderr);
    memcpy((void*)stack_sp, stack_image, stack_bytes);
    free(stack_image);
    write_canary((uintptr_t)h.canary);
    context = h.context;
    pc_snap_jump(&context, 1);
}

/* RPC snapshot {action: "hash"}: FNV-1a hashes of what a snapshot holds
 * (guest memory, native game statics, overlays, VRAM), for comparisons. */
int pc_snapshot_hash(char* out, int size)
{
    range r[MAX_RANGES];
    int n = ranges(r);
    uint64_t state = FNV0, overlays = FNV0;
    for (int i = 3; i < n; i++) {
        if (r[i].name[0] == 's') /* state_data, state_bss */
            state = fnv(r[i].start, r[i].size, state);
        else
            overlays = fnv(r[i].start, r[i].size, overlays);
    }
    return snprintf(out, (size_t)size,
        "{\"ram\":\"%016llx\",\"scratch\":\"%016llx\",\"io\":\"%016llx\",\"state\":\"%016llx\","
        "\"overlays\":\"%016llx\",\"vram\":\"%016llx\",\"vsync\":%u}",
        (unsigned long long)fnv(r[0].start, r[0].size, FNV0), (unsigned long long)fnv(r[1].start, r[1].size, FNV0),
        (unsigned long long)fnv(r[2].start, r[2].size, FNV0), (unsigned long long)state,
        (unsigned long long)overlays, (unsigned long long)fnv(pc_native_vram(), 1024 * 512 * 2, FNV0),
        pc_vsync_count());
}
#endif
