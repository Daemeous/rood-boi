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
 *  - the game's call stack: the game runs on a stack at a fixed address
 *    (boot_runtime.c) and hands control to the host once per vsync through
 *    pc_vsync_boundary (boot_sdk.c's pc_vblank_wait calls it instead of
 *    pc_tick). Every frame below that call is the game's, every frame above
 *    it the host's. The boundary records the game side's callee-saved
 *    registers and stack pointer; a snapshot is taken inside that vsync's
 *    pc_tick (dev_rpc.c's poll) and saves the stack from the boundary down,
 *    with those registers and the stack-protector canary. A new process
 *    copies the stack back and enters pc_tick_resumed as if the game had
 *    just called the boundary: the rest of that vsync runs on the new
 *    process's own host frames, then it returns into the game.
 * Host state (window, renderer, lights, editor, settings, RPC, input) is not
 * saved: the new process starts its own, and caches derived from the game
 * (the hardware VRAM target, native room/actor data) rebuild from the
 * restored game state (pc_hw_resync and each subsystem's own change checks).
 * The memory card files are the launcher's (ROOD_CARD1/2): a snapshot is
 * valid with the card contents it was made with (pc/tests/snapshots.py keys
 * its cache on them). The original run's log is replayed into the new one.
 *
 * Across rebuilds (64 bits): the link pins the game's code, read-only data
 * and state at a fixed address after the engine (pc/tools/snapshot_layout.py),
 * so a snapshot holds no engine address and an engine-only rebuild still
 * resumes it. Its keys (snapshot_key.c) are the game's code before linking
 * and the state layout; a snapshot whose keys differ is refused, saying
 * which. A build without them (32 bits) resumes only snapshots of the same
 * binary (a hash of its code), as does Windows (64 bits: no pinned layout
 * there yet). Linux and Windows on x86; elsewhere the RPC reports it
 * unavailable.
 *
 * Windows (x64): the boundary also saves rdi, rsi and xmm6-xmm15 (callee-saved
 * there); the game stack is fixed at the same address as on Linux
 * (boot_runtime.c); the state sections are the grouped .data$rood.state.m
 * between the zero-length markers below (build_boot.py); MinGW builds no
 * stack protector, so there is no canary; the log is not carried over.
 *
 * pc_snapshot_load_now (the player's Load state, savestate_slots.c) resumes a
 * file in this process instead: the in-process rewind's way (state_rewind.c),
 * from the file's contents.
 *
 * ROOD_SNAPSHOT=path resumes, or exits with the reason (tests);
 * ROOD_RESUME=path (1: the auto-snapshot) resumes when the file is valid for
 * this build and otherwise boots normally (pc/run-game.sh --resume), running.
 * The auto-snapshot is ROOD_AUTOSNAP (default pc/build/resume/last.snap):
 * written on a clean quit when ROOD_AUTOSNAP is set, and by RPC snapshot
 * {action:"autosave"} and RPC restart (dev_rpc.c). */
#define _GNU_SOURCE
#include "platform.h" /* pc_replace_file */
#include "snapshot_context.h"
#include "state_diff.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

extern unsigned pc_vsync_count(void);
extern void pc_tick(void); /* boot_runtime.c */

#if !(defined(__i386__) || defined(__x86_64__)) || (defined(_WIN32) && !defined(__x86_64__))
void pc_vsync_boundary(void) { pc_tick(); }
int pc_state_ranges(pc_state_range* out, int max) { (void)out; (void)max; return 0; }
int pc_snapshot_available(void) { return 0; }
int pc_snapshot_point(const char* path, char* error, int size)
{
    (void)path;
    snprintf(error, (size_t)size, "save states need x86 (Linux or 64-bit Windows)");
    return -1;
}
int pc_snapshot_load_now(const char* path, char* error, int size)
{
    return pc_snapshot_point(path, error, size);
}
void pc_snapshot_resume_if_requested(void) { }
int pc_snapshot_hash(char* out, int size)
{
    snprintf(out, (size_t)size, "{}");
    return 0;
}
const char* pc_snapshot_autosave_path(void) { return NULL; }
int pc_snapshot_check(const char* path, char* why, int size)
{
    (void)path;
    snprintf(why, (size_t)size, "save states need x86 (Linux or 64-bit Windows)");
    return 0;
}
void pc_snapshot_restart(const char* path, int paused)
{
    (void)path;
    (void)paused;
}
#else

/* ---- register context (snapshot_context.h) ---- */
/* The game side of the current vsync's boundary: its callee-saved registers,
 * the stack pointer at the call (pointing at the return address) and that
 * return address (into pc_vblank_wait). */
snap_context pc_snap_boundary;
void pc_vsync_boundary(void);
/* Load the registers of `c` and jump to `fn` with the stack pointer at the
 * boundary's: fn runs as if the game had just called it there. */
__attribute__((noreturn)) void pc_snap_enter(const snap_context* c, void (*fn)(void));
#ifdef _WIN32
/* Windows x64 (Microsoft ABI): c in rcx, fn in rdx. The caller's register
 * home area (32 bytes above the return address) is the game's frame, saved
 * with its stack. */
__asm__(".text\n"
        ".globl pc_vsync_boundary\n"
        ".def pc_vsync_boundary; .scl 2; .type 32; .endef\n"
        "pc_vsync_boundary:\n"
        "    leaq pc_snap_boundary(%rip), %rax\n"
        "    movq %rbx, 0(%rax)\n"
        "    movq %rbp, 8(%rax)\n"
        "    movq %rdi, 16(%rax)\n"
        "    movq %rsi, 24(%rax)\n"
        "    movq %r12, 32(%rax)\n"
        "    movq %r13, 40(%rax)\n"
        "    movq %r14, 48(%rax)\n"
        "    movq %r15, 56(%rax)\n"
        "    movq %rsp, 64(%rax)\n"
        "    movq (%rsp), %rdx\n"
        "    movq %rdx, 72(%rax)\n"
        "    stmxcsr 80(%rax)\n"
        "    fnstcw 84(%rax)\n"
        "    movdqu %xmm6, 96(%rax)\n"
        "    movdqu %xmm7, 112(%rax)\n"
        "    movdqu %xmm8, 128(%rax)\n"
        "    movdqu %xmm9, 144(%rax)\n"
        "    movdqu %xmm10, 160(%rax)\n"
        "    movdqu %xmm11, 176(%rax)\n"
        "    movdqu %xmm12, 192(%rax)\n"
        "    movdqu %xmm13, 208(%rax)\n"
        "    movdqu %xmm14, 224(%rax)\n"
        "    movdqu %xmm15, 240(%rax)\n"
        "    jmp pc_tick\n"
        ".globl pc_snap_enter\n"
        ".def pc_snap_enter; .scl 2; .type 32; .endef\n"
        "pc_snap_enter:\n"
        "    ldmxcsr 80(%rcx)\n"
        "    fldcw 84(%rcx)\n"
        "    movdqu 96(%rcx), %xmm6\n"
        "    movdqu 112(%rcx), %xmm7\n"
        "    movdqu 128(%rcx), %xmm8\n"
        "    movdqu 144(%rcx), %xmm9\n"
        "    movdqu 160(%rcx), %xmm10\n"
        "    movdqu 176(%rcx), %xmm11\n"
        "    movdqu 192(%rcx), %xmm12\n"
        "    movdqu 208(%rcx), %xmm13\n"
        "    movdqu 224(%rcx), %xmm14\n"
        "    movdqu 240(%rcx), %xmm15\n"
        "    movq 0(%rcx), %rbx\n"
        "    movq 8(%rcx), %rbp\n"
        "    movq 16(%rcx), %rdi\n"
        "    movq 24(%rcx), %rsi\n"
        "    movq 32(%rcx), %r12\n"
        "    movq 40(%rcx), %r13\n"
        "    movq 48(%rcx), %r14\n"
        "    movq 56(%rcx), %r15\n"
        "    movq 64(%rcx), %rsp\n"
        "    jmpq *%rdx\n");
_Static_assert(__builtin_offsetof(snap_context, mxcsr) == 80 && __builtin_offsetof(snap_context, xmm) == 96,
    "the boundary's offsets");
/* MinGW-w64 builds no stack protector (no canary to carry). */
static uintptr_t read_canary(void) { return 0; }
static void write_canary(uintptr_t v) { (void)v; }
#elif defined(__x86_64__)
__asm__(".text\n"
        ".globl pc_vsync_boundary\n"
        ".type pc_vsync_boundary, @function\n"
        "pc_vsync_boundary:\n"
        "    leaq pc_snap_boundary(%rip), %rax\n"
        "    movq %rbx, 0(%rax)\n"
        "    movq %rbp, 8(%rax)\n"
        "    movq %r12, 16(%rax)\n"
        "    movq %r13, 24(%rax)\n"
        "    movq %r14, 32(%rax)\n"
        "    movq %r15, 40(%rax)\n"
        "    movq %rsp, 48(%rax)\n"
        "    movq (%rsp), %rdx\n"
        "    movq %rdx, 56(%rax)\n"
        "    stmxcsr 64(%rax)\n"
        "    fnstcw 68(%rax)\n"
        "    jmp pc_tick\n"
        ".size pc_vsync_boundary, .-pc_vsync_boundary\n"
        ".globl pc_snap_enter\n"
        ".type pc_snap_enter, @function\n"
        "pc_snap_enter:\n"
        "    ldmxcsr 64(%rdi)\n"
        "    fldcw 68(%rdi)\n"
        "    movq 0(%rdi), %rbx\n"
        "    movq 8(%rdi), %rbp\n"
        "    movq 16(%rdi), %r12\n"
        "    movq 24(%rdi), %r13\n"
        "    movq 32(%rdi), %r14\n"
        "    movq 40(%rdi), %r15\n"
        "    movq 48(%rdi), %rsp\n"
        "    jmpq *%rsi\n"
        ".size pc_snap_enter, .-pc_snap_enter\n");
static uintptr_t read_canary(void)
{
    uintptr_t v;
    __asm__ volatile("movq %%fs:0x28, %0" : "=r"(v));
    return v;
}
static void write_canary(uintptr_t v) { __asm__ volatile("movq %0, %%fs:0x28" : : "r"(v) : "memory"); }
#else
__asm__(".text\n"
        ".globl pc_vsync_boundary\n"
        ".type pc_vsync_boundary, @function\n"
        "pc_vsync_boundary:\n"
        "    movl %ebx, pc_snap_boundary+0\n"
        "    movl %esi, pc_snap_boundary+4\n"
        "    movl %edi, pc_snap_boundary+8\n"
        "    movl %ebp, pc_snap_boundary+12\n"
        "    movl %esp, pc_snap_boundary+16\n"
        "    movl (%esp), %eax\n"
        "    movl %eax, pc_snap_boundary+20\n"
        "    stmxcsr pc_snap_boundary+32\n"
        "    fnstcw pc_snap_boundary+36\n"
        "    jmp pc_tick\n"
        ".size pc_vsync_boundary, .-pc_vsync_boundary\n"
        ".globl pc_snap_enter\n"
        ".type pc_snap_enter, @function\n"
        "pc_snap_enter:\n"
        "    movl 4(%esp), %edx\n"
        "    movl 8(%esp), %ecx\n"
        "    ldmxcsr 32(%edx)\n"
        "    fldcw 36(%edx)\n"
        "    movl 0(%edx), %ebx\n"
        "    movl 4(%edx), %esi\n"
        "    movl 8(%edx), %edi\n"
        "    movl 12(%edx), %ebp\n"
        "    movl 16(%edx), %esp\n"
        "    jmp *%ecx\n"
        ".size pc_snap_enter, .-pc_snap_enter\n");
static uintptr_t read_canary(void)
{
    uintptr_t v;
    __asm__ volatile("movl %%gs:0x14, %0" : "=r"(v));
    return v;
}
static void write_canary(uintptr_t v) { __asm__ volatile("movl %0, %%gs:0x14" : : "r"(v) : "memory"); }
#endif

/* Call fn with the stack pointer at `top` (aligned down); fn does not return
 * (state_rewind.c's restore and the in-process load run there). */
__attribute__((noreturn)) void pc_snap_call_on_stack(void* top, void (*fn)(void));
#ifdef _WIN32
__asm__(".text\n.globl pc_snap_call_on_stack\n.def pc_snap_call_on_stack; .scl 2; .type 32; .endef\n"
        "pc_snap_call_on_stack:\n movq %rcx,%rsp\n andq $-16,%rsp\n subq $32,%rsp\n callq *%rdx\n ud2\n");
#elif defined(__x86_64__)
__asm__(".text\n.globl pc_snap_call_on_stack\n.type pc_snap_call_on_stack,@function\n"
        "pc_snap_call_on_stack:\n movq %rdi,%rsp\n andq $-16,%rsp\n callq *%rsi\n ud2\n"
        ".size pc_snap_call_on_stack,.-pc_snap_call_on_stack\n");
#else
__asm__(".text\n.globl pc_snap_call_on_stack\n.type pc_snap_call_on_stack,@function\n"
        "pc_snap_call_on_stack:\n movl 4(%esp),%eax\n movl 8(%esp),%ecx\n"
        " movl %eax,%esp\n andl $-16,%esp\n calll *%ecx\n ud2\n"
        ".size pc_snap_call_on_stack,.-pc_snap_call_on_stack\n");
#endif

/* ---- what is saved ---- */
#ifdef _WIN32
/* PE has no __start_/__stop_ section symbols: ld sorts the grouped .data$
 * sections by name, so these zero-length markers bracket the state units'
 * statics (build_boot.py renames them all .data$rood.state.m; MinGW's GCC
 * puts zero-initialised statics in .data$ too, so there is no state .bss). */
__attribute__((section(".data$rood.state.a"))) char __start_rood_state_data[0];
__attribute__((section(".data$rood.state.z"))) char __stop_rood_state_data[0];
static char* const __start_rood_state_bss = NULL;
static char* const __stop_rood_state_bss = NULL;
/* The code a snapshot must match: the image's .text (no pinned layout). */
extern char __ImageBase[];
static const char* text_range(size_t* size)
{
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)__ImageBase;
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(__ImageBase + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
        if (!memcmp(s->Name, ".text", 6)) {
            *size = s->Misc.VirtualSize;
            return __ImageBase + s->VirtualAddress;
        }
    *size = 0;
    return __ImageBase;
}
#else
extern char __start_rood_state_data[] __attribute__((weak)), __stop_rood_state_data[] __attribute__((weak));
extern char __start_rood_state_bss[] __attribute__((weak)), __stop_rood_state_bss[] __attribute__((weak));
extern char __executable_start[], etext[];
static const char* text_range(size_t* size)
{
    *size = (size_t)(etext - __executable_start);
    return __executable_start;
}
#endif
/* snapshot_key.c (build_boot.py, 64 bits): the game's code and state layout. */
extern const unsigned long long pc_snapshot_code_key __attribute__((weak));
extern const unsigned long long pc_snapshot_layout_key __attribute__((weak));
struct pc_overlay_statics {
    const char* path;
    char *data_start, *data_end, *bss_start, *bss_end;
};
extern const struct pc_overlay_statics pc_overlay_statics[] __attribute__((weak));
extern const int pc_overlay_statics_count __attribute__((weak));
extern char* pc_game_stack(size_t* size); /* boot_runtime.c */
extern void pc_game_stack_entered(void);  /* boot_runtime.c: Windows' stack bounds for it */
extern void pc_input_rewound(void) __attribute__((weak)); /* input_record.c */
extern void pc_tick_resumed(void);        /* boot_runtime.c: the rest of a resumed vsync */
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
extern void pc_cd_release(void) __attribute__((weak));    /* cd_drive.c: close it before a load */
extern void pc_hw_resync(void) __attribute__((weak));     /* gpu_hw.c: the target from native VRAM */
extern void pc_rpc_restored(void) __attribute__((weak));  /* dev_rpc.c: listen, paused */
extern void pc_rpc_set_paused(int value) __attribute__((weak));

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

/* Oracle consumers share the snapshot's range list, including external VRAM. */
int pc_state_ranges(pc_state_range* out, int max)
{
    range r[MAX_RANGES];
    int n = ranges(r), count = n;
    uintptr_t vram = (uintptr_t)pc_native_vram();
    int inside = 0;
    for (int i = 0; i < n; i++) {
        if (out && i < max)
            out[i] = (pc_state_range) { r[i].start, r[i].size, r[i].name };
        uintptr_t start = (uintptr_t)r[i].start;
        inside |= vram >= start && vram - start <= r[i].size
            && 1024 * 512 * 2 <= r[i].size - (vram - start);
    }
    if (vram && !inside) {
        if (out && n < max)
            out[n] = (pc_state_range) { (char*)vram, 1024 * 512 * 2, "vram" };
        ++count;
    }
    return count;
}

static uint64_t fnv(const void* data, size_t size, uint64_t h)
{
    const unsigned char* p = data;
    for (size_t i = 0; i < size; i++)
        h = (h ^ p[i]) * 1099511628211ull;
    return h;
}
#define FNV0 1469598103934665603ull
/* The keys a snapshot must match: the pinned layout's (64 bits), else a hash
 * of the whole code (the same binary only). */
static uint64_t code_key(void)
{
    if (&pc_snapshot_code_key)
        return pc_snapshot_code_key;
    static uint64_t whole;
    if (!whole) {
        size_t size;
        const char* text = text_range(&size);
        whole = fnv(text, size, FNV0);
    }
    return whole;
}
static uint64_t layout_key(void) { return &pc_snapshot_layout_key ? pc_snapshot_layout_key : code_key(); }

/* ---- the file ---- */
#define MAGIC "ROODSNAP"
#define VERSION 2 /* 2: the stack from the vsync boundary, code and layout keys */
enum { CHUNK_END, CHUNK_MEMORY, CHUNK_SPU, CHUNK_LOG, CHUNK_FOG };
typedef struct {
    char magic[8];
    uint32_t version, pointer_size;
    uint64_t code_key, layout_key;
    uint32_t vsync, reserved;
    snap_context boundary;
    uint64_t canary;
} header;
typedef struct {
    uint32_t tag, reserved;
    uint64_t address, size;
} chunk;

static int write_chunk(FILE* f, uint32_t tag, const void* address, const void* data, uint64_t size)
{
    chunk c = { tag, 0, (uint64_t)(uintptr_t)address, size };
    return fwrite(&c, sizeof c, 1, f) == 1 && (!size || fwrite(data, 1, (size_t)size, f) == size);
}

/* The log so far (stderr, when it is a file): replayed by the new process. */
static char* read_log(size_t* size)
{
#ifdef _WIN32
    *size = 0; /* not carried over (no /proc/self/fd/2) */
    return NULL;
#else
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
#endif
}

#ifdef _WIN32
#define PC_PATH_BACKSLASH 1
#else
#define PC_PATH_BACKSLASH 0
#endif
/* mkdir -p of a file's directory. */
static void make_parent(const char* path)
{
    char dir[1100];
    snprintf(dir, sizeof dir, "%s", path);
    for (char* p = dir + 1; *p; p++)
        if (*p == '/' || (*p == '\\' && PC_PATH_BACKSLASH)) {
            char separator = *p;
            *p = 0;
            pc_mkdir(dir);
            *p = separator;
        }
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
    make_parent(path);
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
    h.code_key = code_key();
    h.layout_key = layout_key();
    h.vsync = pc_vsync_count();
    h.boundary = pc_snap_boundary;
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

/* Save to `path`: 0 when saved, -1 with `error` when it failed. Called
 * inside pc_tick (dev_rpc.c's poll), whose vsync boundary is live: the game
 * stack from the boundary down is saved, the host frames above it are not. */
extern int pc_game_snapshot_safe(void) __attribute__((weak));
__attribute__((noinline)) int pc_snapshot_point(const char* path, char* error, int size)
{
    if (pc_game_snapshot_safe && !pc_game_snapshot_safe()) {
        snprintf(error, (size_t)size, "save refused: owned frame live or incomplete unwind");
        return -1;
    }
    uintptr_t sp = CTX_SP(&pc_snap_boundary), here = (uintptr_t)__builtin_frame_address(0);
    if (!sp || here >= sp || *(uintptr_t*)sp != CTX_IP(&pc_snap_boundary)) {
        snprintf(error, (size_t)size, "not inside a vsync of the game (pc_tick through pc_vsync_boundary)");
        return -1;
    }
    return write_file(path, sp, error, size);
}

/* RPC snapshot {action:"autosave"}, restart and a clean quit: ROOD_AUTOSNAP,
 * else pc/build/resume/last.snap (relative to the working directory, the
 * repository root for pc/run-game.sh). */
const char* pc_snapshot_autosave_path(void)
{
    const char* path = getenv("ROOD_AUTOSNAP");
    return path && *path ? path : "pc/build/resume/last.snap";
}

/* RPC restart, after saving `path`: the same command line again, from the
 * executable's path now (a rebuilt binary), resuming `path` (ROOD_RESUME:
 * a snapshot the new build refuses boots normally, with the reason in the
 * log), paused or running. Returns only when the exec fails. */
#ifdef _WIN32
/* Windows: no exec. The same command line runs as a new process, which gets
 * this one's log (its standard handles), and this one exits at once so its
 * window and RPC port go away. */
void pc_snapshot_restart(const char* path, int paused)
{
    char absolute[1100];
    if (!GetFullPathNameA(path, sizeof absolute, absolute, NULL))
        return;
    SetEnvironmentVariableA("ROOD_RESUME", absolute);
    SetEnvironmentVariableA("ROOD_RESUME_PAUSED", paused ? "1" : "0");
    SetEnvironmentVariableA("ROOD_SNAPSHOT", NULL);
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH))
        return;
    wchar_t* cmdline = _wcsdup(GetCommandLineW()); /* CreateProcessW may write to it */
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    fprintf(stderr, "[snapshot] restarting from %s\n", absolute);
    fflush(stderr);
    fflush(stdout);
    if (!cmdline || !CreateProcessW(exe, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "[snapshot] restart: cannot run the game again (error %lu)\n", GetLastError());
        free(cmdline);
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    ExitProcess(0);
}
#else
void pc_snapshot_restart(const char* path, int paused)
{
    char exe[1024];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0)
        return;
    exe[n] = 0;
    char* deleted = strstr(exe, " (deleted)"); /* replaced by the rebuild: the path is the new file */
    if (deleted && deleted[10] == 0)
        *deleted = 0;
    static char cmdline[8192];
    char* argv[64];
    int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC), argc = 0;
    ssize_t got = fd >= 0 ? read(fd, cmdline, sizeof cmdline - 1) : -1;
    if (fd >= 0)
        close(fd);
    if (got <= 0)
        return;
    cmdline[got] = 0;
    for (char* p = cmdline; p < cmdline + got && argc < 63; p += strlen(p) + 1)
        argv[argc++] = p;
    argv[argc] = NULL;
    char absolute[1100];
    if (path[0] != '/' && getcwd(absolute, 1024)) {
        size_t len = strlen(absolute);
        snprintf(absolute + len, sizeof absolute - len, "/%s", path);
        path = absolute;
    }
    setenv("ROOD_RESUME", path, 1);
    setenv("ROOD_RESUME_PAUSED", paused ? "1" : "0", 1);
    unsetenv("ROOD_SNAPSHOT");
    const char* rpc = getenv("ROOD_RPC");
    if (rpc && *rpc && strncmp(rpc, "tcp:", 4))
        unlink(rpc);
    fprintf(stderr, "[snapshot] restarting %s from %s\n", exe, path);
    fflush(stderr);
    /* Only the log (stdio) carries over: the window, audio, GPU and sockets
     * are the new process's own. */
    for (int i = 3; i < 1024; i++)
        close(i);
    execv(exe, argv);
    fprintf(stderr, "[snapshot] restart: cannot run %s: %s\n", exe, strerror(errno));
}
#endif

/* Whether `path` is a snapshot this build resumes (its header: version, pointer size, code and
 * layout keys): 1, or 0 and why (rood.dev.bench lists and loads with it). */
int pc_snapshot_check(const char* path, char* why, int size)
{
    header h;
    FILE* f = fopen(path, "rb");
    const char* reason = "cannot open";
    int ok = 0;
    if (f) {
        if (fread(&h, sizeof h, 1, f) != 1 || memcmp(h.magic, MAGIC, 8) || h.version != VERSION)
            reason = "not a snapshot of this version";
        else if (h.pointer_size != sizeof(void*))
            reason = h.pointer_size == 8 ? "made by the 64-bit build" : "made by the 32-bit build";
        else if (h.code_key != code_key())
            reason = "stale: the game's code changed since it was saved";
        else if (h.layout_key != layout_key())
            reason = "stale: the game state's layout changed since it was saved";
        else
            ok = 1, reason = "";
        fclose(f);
    }
    snprintf(why, (size_t)size, "%s", reason);
    return ok;
}

/* ---- resuming ---- */
typedef struct {
    chunk c;
    char* data;
} loaded;

/* Read a whole snapshot and check it fits this build; NULL with `why`. */
static loaded* load(const char* path, header* h, int* count, const char** why)
{
    FILE* f = fopen(path, "rb");
    *why = "cannot open";
    if (!f)
        return NULL;
    loaded* chunks = NULL;
    int n = 0, cap = 0;
    *why = "not a snapshot of this version (made before resuming across rebuilds?)";
    if (fread(h, sizeof *h, 1, f) != 1 || memcmp(h->magic, MAGIC, 8) || h->version != VERSION)
        goto fail;
    if (h->pointer_size != sizeof(void*)) {
        *why = h->pointer_size == 8 ? "made by the 64-bit build" : "made by the 32-bit build";
        goto fail;
    }
    if (h->code_key != code_key()) {
        *why = &pc_snapshot_code_key ? "the game's code changed since it was saved (src/, or a pc/src unit built "
                                       "like the game's): only an engine-only rebuild keeps a snapshot"
                                     : "made by another build of the game";
        goto fail;
    }
    if (h->layout_key != layout_key()) {
        *why = "the game state's layout changed since it was saved (a host unit's saved statics, "
               "or the linker)";
        goto fail;
    }
    for (;;) {
        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            loaded* grown = realloc(chunks, (size_t)cap * sizeof *chunks);
            if (!grown)
                goto fail;
            chunks = grown;
        }
        loaded* l = &chunks[n];
        *why = "truncated";
        if (fread(&l->c, sizeof l->c, 1, f) != 1)
            goto fail;
        l->data = NULL;
        if (l->c.tag == CHUNK_END)
            break;
        l->data = malloc(l->c.size ? (size_t)l->c.size : 1);
        ++n;
        if (!l->data || (l->c.size && fread(l->data, 1, (size_t)l->c.size, f) != l->c.size))
            goto fail;
    }
    fclose(f);
    *count = n;
    *why = NULL;
    return chunks;
fail:
    for (int i = 0; i < n; i++)
        free(chunks[i].data);
    free(chunks);
    fclose(f);
    return NULL;
}

/* A snapshot read and checked against this build, ready to be written in. */
typedef struct {
    header h;
    loaded* chunks;
    int count;
    char* stack_at; /* the saved stack's place (its sp at the boundary) */
} prepared;

static void discard(prepared* p)
{
    for (int i = 0; p->chunks && i < p->count; i++)
        free(p->chunks[i].data);
    free(p->chunks);
    p->chunks = NULL;
    p->count = 0;
}

/* Read `path` and check every chunk before anything is written: a refused
 * file must leave the process as it was. NULL, or why it does not fit. */
static const char* prepare(const char* path, prepared* p)
{
    const char* why = NULL;
    header* hp = &p->h;
    p->count = 0;
    p->stack_at = NULL;
    p->chunks = load(path, hp, &p->count, &why);
    loaded* chunks = p->chunks;
    int count = p->count;
    size_t game_stack_size;
    char* stack = pc_game_stack(&game_stack_size);
    range r[MAX_RANGES];
    int n = ranges(r), have_stack = 0;
    char* stack_at = NULL;
    for (int i = 0; chunks && i < count && !why; i++) {
        const chunk* c = &chunks[i].c;
        if (c->tag == CHUNK_MEMORY) {
            char* at = (char*)(uintptr_t)c->address;
            int known = 0;
            for (int j = 0; j < n; j++)
                known |= r[j].start == at && r[j].size == c->size;
            if (known)
                continue;
            if (stack && at >= stack && at + c->size == stack + game_stack_size && !have_stack
                && (uintptr_t)at == CTX_SP(&hp->boundary)) {
                have_stack = 1;
                stack_at = at;
            } else
                why = "a memory range this build does not have";
        } else if (c->tag != CHUNK_SPU && c->tag != CHUNK_FOG && c->tag != CHUNK_LOG)
            why = "unknown chunk";
    }
    if (chunks && !why && !stack)
        why = "the game stack is not at its fixed address";
    if (chunks && !why && !have_stack)
        why = "no game stack";
    if (chunks && !why) {
        int memory = 0;
        for (int i = 0; i < count; i++)
            memory += chunks[i].c.tag == CHUNK_MEMORY;
        if (memory != n + 1)
            why = "missing ranges";
    }
    if (why)
        discard(p);
    p->stack_at = stack_at;
    return why;
}

/* Write a prepared snapshot in, except the game stack (the caller's last
 * step: it may be running on it), and free its chunks: the stack image is
 * returned (the caller frees it). Guest memory is written before the SPU is
 * checked, so a failure there exits. */
static char* apply(prepared* p, const char* path, int replay_log, size_t* stack_bytes)
{
    char* stack_image = NULL;
    *stack_bytes = 0;
    for (int i = 0; i < p->count; i++) {
        const chunk* c = &p->chunks[i].c;
        char* data = p->chunks[i].data;
        if (c->tag == CHUNK_MEMORY) {
            if ((char*)(uintptr_t)c->address == p->stack_at) {
                stack_image = data, *stack_bytes = (size_t)c->size;
                continue;
            }
            memcpy((void*)(uintptr_t)c->address, data, (size_t)c->size);
        } else if (c->tag == CHUNK_SPU) {
            if (!pc_spu_state_load || !pc_spu_state_load(data, (int)c->size)) {
                fprintf(stderr, "[snapshot] cannot resume %s: the SPU state does not fit\n", path);
                exit(1); /* guest memory is already written */
            }
        } else if (c->tag == CHUNK_FOG) {
            if (pc_fog_state_load && !pc_fog_state_load(data, (int)c->size))
                fprintf(stderr, "[snapshot] the interactive fog's state does not fit: it starts at rest\n");
        } else if (c->tag == CHUNK_LOG && replay_log) {
            fprintf(stderr, "[snapshot] the log of the run that saved %s:\n", path);
            fwrite(data, 1, (size_t)c->size, stderr);
            fprintf(stderr, "[snapshot] (end of the saved log)\n");
        }
        free(data);
    }
    free(p->chunks);
    p->chunks = NULL;
    p->count = 0;
    if (pc_cd_restored)
        pc_cd_restored();
    if (pc_hw_resync)
        pc_hw_resync();
    return stack_image;
}

/* The last step of a resume or a load: the game stack, the registers, and
 * into the game at the saved boundary (the rest of that vsync). */
static __attribute__((noreturn)) void enter_saved(prepared* p, char* stack_image, size_t stack_bytes)
{
    memcpy(p->stack_at, stack_image, stack_bytes);
    free(stack_image);
    write_canary((uintptr_t)p->h.canary);
    pc_snap_boundary = p->h.boundary;
    pc_game_stack_entered();
    pc_snap_enter(&pc_snap_boundary, pc_tick_resumed);
}

/* Called by main instead of starting the game, after the window, audio and
 * overlay statics are initialised. ROOD_SNAPSHOT: resumes or exits with the
 * reason. ROOD_RESUME: resumes when the file fits this build, else returns
 * (the game boots). Does not return when it resumes. */
void pc_snapshot_resume_if_requested(void)
{
    const char* path = getenv("ROOD_SNAPSHOT");
    int strict = path && *path;
    if (!strict) {
        path = getenv("ROOD_RESUME");
        if (!path || !*path || !strcmp(path, "0"))
            return;
        if (!strcmp(path, "1"))
            path = pc_snapshot_autosave_path();
    }
    static prepared p;
    const char* why = prepare(path, &p);
    if (why) {
        fprintf(stderr, "[snapshot] cannot resume %s: %s\n", path, why);
        if (strict)
            exit(1);
        fprintf(stderr, "[snapshot] booting normally\n");
        return;
    }
    size_t stack_bytes;
    /* This runs on the main thread's stack, not the game's: no side stack. */
    char* stack_image = apply(&p, path, 1, &stack_bytes);
    if (pc_rpc_restored)
        pc_rpc_restored();
    /* The player's resume runs on, as it was (RPC restart {paused} keeps it paused). */
    const char* keep_paused = getenv("ROOD_RESUME_PAUSED");
    if (!strict && pc_rpc_set_paused && !(keep_paused && !strcmp(keep_paused, "1")))
        pc_rpc_set_paused(0);
    fprintf(stderr, "[snapshot] resumed %s at vsync %u\n", path, p.h.vsync);
    fflush(stderr);
    enter_saved(&p, stack_image, stack_bytes);
}

/* ---- loading in this process (the player's Load state) ---- */
/* The game stack is replaced while the load runs, so the load runs on a side
 * stack: the bottom of the game stack's own reservation (inside the stack
 * bounds Windows keeps for this thread), far below anything the game uses. */
#define SIDE_STACK (1u << 20)
static prepared pending;
static char pending_path[1024];
static __attribute__((noreturn)) void load_on_side_stack(void)
{
    size_t stack_bytes;
    if (pc_cd_release)
        pc_cd_release(); /* this process's disc file: closed, not leaked */
    char* stack_image = apply(&pending, pending_path, 0, &stack_bytes);
    if (pc_input_rewound)
        pc_input_rewound(); /* a replay seeks again; held RPC input is dropped */
    fprintf(stderr, "[snapshot] loaded %s (saved at vsync %u)\n", pending_path, pending.h.vsync);
    fflush(stderr);
    enter_saved(&pending, stack_image, stack_bytes);
}
/* Load `path` into this process: does not return when it loads; -1 with
 * `error` when the file does not fit this build (nothing is changed then).
 * Called inside pc_tick (dev_rpc.c's poll), like pc_snapshot_point. */
__attribute__((noinline)) int pc_snapshot_load_now(const char* path, char* error, int size)
{
    if (pc_game_snapshot_safe && !pc_game_snapshot_safe()) {
        snprintf(error, (size_t)size, "load refused: owned frame live or incomplete unwind");
        return -1;
    }
    uintptr_t sp = CTX_SP(&pc_snap_boundary), here = (uintptr_t)__builtin_frame_address(0);
    if (!sp || here >= sp || *(uintptr_t*)sp != CTX_IP(&pc_snap_boundary)) {
        snprintf(error, (size_t)size, "not inside a vsync of the game (pc_tick through pc_vsync_boundary)");
        return -1;
    }
    size_t game_stack_size;
    char* stack = pc_game_stack(&game_stack_size);
    const char* why = stack ? prepare(path, &pending) : "the game stack is not at its fixed address";
    if (!why && pending.stack_at < stack + SIDE_STACK) {
        discard(&pending);
        why = "the saved game stack reaches the loader's side stack";
    }
    if (why) {
        snprintf(error, (size_t)size, "%s", why);
        return -1;
    }
    snprintf(pending_path, sizeof pending_path, "%s", path);
    pc_snap_call_on_stack(stack + SIDE_STACK, load_on_side_stack);
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
        "\"overlays\":\"%016llx\",\"vram\":\"%016llx\",\"vsync\":%u,\"code_key\":\"%016llx\","
        "\"layout_key\":\"%016llx\"}",
        (unsigned long long)fnv(r[0].start, r[0].size, FNV0), (unsigned long long)fnv(r[1].start, r[1].size, FNV0),
        (unsigned long long)fnv(r[2].start, r[2].size, FNV0), (unsigned long long)state,
        (unsigned long long)overlays, (unsigned long long)fnv(pc_native_vram(), 1024 * 512 * 2, FNV0),
        pc_vsync_count(), (unsigned long long)code_key(), (unsigned long long)layout_key());
}
#endif
