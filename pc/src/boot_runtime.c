#define _GNU_SOURCE
#include "audio.h"
#include "pc_paths.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#else
#include <sys/mman.h>
#include <execinfo.h>
#include <ucontext.h>
#endif
static uint16_t vram[1024 * 512];
extern void pc_snapshot_resume_if_requested(void) __attribute__((weak)); /* snapshot.c (ROOD_SNAPSHOT) */
const uint16_t* pc_native_vram(void) { return vram; }
/* Hardware rasterizer mirror (gpu_hw.c); no-ops until a GL context exists. */
extern void pc_hw_mark(int, int, int, int);
extern int pc_hw_triangle(const int*, const int*, const int*, unsigned, unsigned, unsigned,
    unsigned, const int*, int, int);
extern void pc_hw_rect(int, int, int, int, int, int, unsigned, unsigned, unsigned, unsigned,
    unsigned, const int*, int, int);
extern void pc_hw_line(int, int, int, int, unsigned, unsigned, int, unsigned, const int*, int);
extern void pc_hw_fill(int, int, int, int, int, int, int);
extern void pc_hw_upload(int, int, int, int);
extern void pc_hw_copy(int, int, int, int, int, int);
/* Native VRAM is about to be read here: copy back what only the hardware
 * target has (gpu_hw.c; absent in unit probes). */
extern void pc_hw_sync(int, int, int, int) __attribute__((weak));
extern void pc_hw_sync_store(int, int, int, int) __attribute__((weak));
extern void pc_hw_sync_texture(unsigned, unsigned, unsigned, int, int, int, int) __attribute__((weak));
extern void pc_hw_stale_copy(int, int, int, int, int, int) __attribute__((weak));
extern void pc_hw_set_2d(int);
extern void pc_hw_set_bounds(int, int, int, int, unsigned, unsigned, const int*, const int*);
extern void pc_hw_set_room(int);
extern void pc_hw_set_packet(const void*);
extern int pc_hw_is_room_packet(const void*);
extern int pc_hw_room_depth(const void*, const unsigned*, int, int*) __attribute__((weak)); /* absent in unit probes */
extern int pc_ui_precise_lookup(const void*, const unsigned*, int, float*) __attribute__((weak)); /* ui_upscale.c */
extern int pc_ui_precise_find(const void*, const unsigned*, int, float*) __attribute__((weak)); /* ui_upscale.c */
/* ui_text_layout line (gpu_hw.c): scaled glyph quads laid out with their lines. */
extern int pc_hw_text_quad(const float*, const int*, const int*, unsigned, unsigned, unsigned, unsigned, const int*,
    int, int) __attribute__((weak));
extern void pc_hw_text_piece_done(void) __attribute__((weak));
/* Room polygon depths from room_geometry.c (1) or the projection log (0). */
int pc_room_exact_depth = 1;
/* Widescreen HUD anchoring of the packet about to draw (gpu_hw.c). */
extern void pc_hw_anchor_packet(const void*, int, int) __attribute__((weak));
extern int pc_gte_projected(int, int);
extern int pc_gte_projected_depth(int, int, int*);
extern void pc_hw_set_depth(const int*);
extern void pc_hw_set_precise(const float*);
extern void pc_hw_set_flipped_uv(const int*, const int*);
extern int pc_gte_projected_precise(int, int, int*, float*, float*);
extern int pc_gte_lookup_view[3];
extern int pc_gte_projected_candidates(int, int, int*, unsigned*, int);
extern const void* pc_gte_projection_caller(int);
extern int pc_gte_log; /* off: no projection log (everything 3D is native) */
extern int pc_gte_widescreen __attribute__((weak));
extern void pc_hw_set_narrowed(void) __attribute__((weak));
/* Widescreen with the log off: whether every vertex was projected (narrowed)
 * this frame (the GTE still records projections in widescreen). */
static int narrowed_2d(const int (*xy)[2], int count)
{
    if (pc_gte_log || !&pc_gte_widescreen || !pc_gte_widescreen || !pc_hw_set_narrowed)
        return 0;
    for (int i = 0; i < count; ++i) {
        int found = pc_gte_projected(xy[i][0], xy[i][1]);
        /* Lines: within a pixel (the targeting sphere nudges each segment's
         * start by its slope, BATTLE 573B8.c func_800C0990). */
        for (int k = 0; count == 2 && !found && k < 9; ++k)
            found = k != 4 && pc_gte_projected(xy[i][0] + k % 3 - 1, xy[i][1] + k / 3 - 1);
        if (!found)
            return 0;
    }
    return 1;
}
extern const void* pc_hw_caller;
extern void pc_gte_projection_entry(int, int, int, int*, float*, float*); /* OFX, OFY (16.16), H of the last lookup */
extern int pc_scene_game_projection(float*, float*, float*);
static int stop_at_loading, stop_at_title, stop_at_opening, opening_ready;
static unsigned opening_frames, battle_frames, stop_at_battle_frame;
extern void pc_window_init(int, int);
extern void pc_window_tick(void);
extern void pc_window_frame(const uint16_t*, int, int, int, int);
int pc_skip_movies;
int pc_room_checkpoint_requested;
static int display_x, display_y, display_w = 320, display_h = 224;
static int draw_x, draw_y, clip_x, clip_y, clip_w = 1024, clip_h = 512, tpage;
static unsigned int frames;
static unsigned texture_window, mask_set, mask_check;
void vs_main_exec(void);
/* Overlay statics restart on each overlay load (overlay_statics.c). */
extern void pc_overlay_statics_init(void);
extern void pc_overlay_statics_loaded(int);
static void pc_dump_stack(int sig);
void pc_unimplemented(const char* name)
{
    fprintf(stderr, "[UNIMPLEMENTED] %s (battle frame %u)\n", name, battle_frames);
    pc_dump_stack(0); /* where it was reached: pc/tools/where.sh resolve */
    exit(2);
}
void pc_log(const char* message) { fprintf(stderr, "[boot] %s\n", message); }
/* ROOD_SYNC_DEBUG: who reads VRAM back (StoreImage callers). */
void pc_store_image_debug(int x, int y, int w, int h, const void* caller)
{
    static int debug __attribute__((section(".data.rood_host"))) = -1; /* host setting: not saved state */
    if (debug < 0)
        debug = getenv("ROOD_SYNC_DEBUG") != NULL;
    if (debug)
        fprintf(stderr, "[sync] StoreImage %d,%d %dx%d from %p\n", x, y, w, h, caller);
}
void pc_vram_transfer(int x, int y, int w, int h, void* data, int write)
{
    if (x < 0 || y < 0 || w < 0 || h < 0 || x + w > 1024 || y + h > 512) {
        fprintf(stderr, "VRAM transfer: %d,%d %dx%d\n", x, y, w, h);
        pc_unimplemented("VRAM rectangle out of bounds");
    }
    if (!write && pc_hw_sync_store)
        pc_hw_sync_store(x, y, w, h); /* StoreImage */
    for (int row = 0; row < h; row++) {
        void* vp = vram + (y + row) * 1024 + x;
        void* dp = (uint16_t*)data + row * w;
        if (write)
            memcpy(vp, dp, (size_t)w * 2);
        else
            memcpy(dp, vp, (size_t)w * 2);
    }
    if (write)
        pc_hw_upload(x, y, w, h);
}
/* GPU fill (GP0 0x02) as the hardware does it: x is rounded down to 16
 * pixels, the width rounded up to 16, both wrapped to VRAM (1024 x 512).
 * Out-of-range requests are logged once, not rejected. */
void pc_vram_clear(int x, int y, int w, int h, int r, int g, int b)
{
    static int reported;
    if ((x < 0 || y < 0 || w < 0 || h < 0 || x + w > 1024 || y + h > 512) && !reported++)
        fprintf(stderr, "[gpu] VRAM fill %d,%d %dx%d wraps (hardware fill semantics)\n", x, y,
            w, h);
    uint16_t color = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10);
    x &= 0x3f0;
    y &= 0x1ff;
    w = ((w & 0x3ff) + 0xf) & ~0xf;
    h &= 0x1ff;
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            vram[((y + row) & 511) * 1024 + ((x + col) & 1023)] = color;
    /* Mirror as up to four in-range pieces of the wrapped rectangle. */
    for (int part = 0; part < 4; part++) {
        int px = part & 1 ? 0 : x, py = part & 2 ? 0 : y;
        int pw = part & 1 ? x + w - 1024 : (x + w > 1024 ? 1024 - x : w);
        int ph = part & 2 ? y + h - 512 : (y + h > 512 ? 512 - y : h);
        if (pw > 0 && ph > 0) {
            pc_hw_mark(px, py, px + pw - 1, py + ph - 1);
            pc_hw_fill(px, py, pw, ph, r, g, b);
        }
    }
}
static void copy_file(const char* from, const char* to)
{
    FILE *src = fopen(from, "rb"), *dst = fopen(to, "wb");
    if (src && dst) {
        int c;
        while ((c = fgetc(src)) != EOF)
            fputc(c, dst);
    }
    if (src)
        fclose(src);
    if (dst)
        fclose(dst);
}
/* Displayed pixel (x, y) as 8-bit RGB: BGR555, or in 24-bit display mode
 * (PutDispEnv isrgb24, the intro movie) bytes R, G, B packed across VRAM
 * halfwords from the display's left edge. */
int pc_display_rgb24;
static void display_pixel(int x, int y, unsigned char* rgb)
{
    const uint16_t* row = vram + ((y + display_y) & 511) * 1024;
    if (pc_display_rgb24) {
        int byte = display_x * 2 + x * 3;
        for (int i = 0; i < 3; i++, byte++) {
            uint16_t p = row[(byte >> 1) & 1023];
            rgb[i] = byte & 1 ? p >> 8 : p & 0xFF;
        }
        return;
    }
    uint16_t p = row[(x + display_x) & 1023];
    rgb[0] = ((p & 31) * 255) / 31;
    rgb[1] = (((p >> 5) & 31) * 255) / 31;
    rgb[2] = (((p >> 10) & 31) * 255) / 31;
}
void pc_present(void)
{
    FILE* f = fopen(pc_out_path("loading.ppm"), "wb");
    if (!f) {
        perror("framebuffer");
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", display_w, display_h);
    for (int y = 0; y < display_h; y++) {
        unsigned char row[1024 * 3];
        for (int x = 0; x < display_w && x < 1024; x++)
            display_pixel(x, y, row + x * 3);
        fwrite(row, 3, display_w < 1024 ? display_w : 1024, f);
    }
    fclose(f);
    /* Debug aid: ROOD_SNAP_EVERY=N copies the displayed frame to
     * snap-<battle frame>.ppm in ROOD_OUT (pc/build/boot) every N battle frames. */
    static int snap_every __attribute__((section(".data.rood_host"))) = -1; /* host setting */
    static unsigned snapped;
    if (snap_every < 0)
        snap_every = getenv("ROOD_SNAP_EVERY") ? atoi(getenv("ROOD_SNAP_EVERY")) : 0;
    if (snap_every > 0 && battle_frames && battle_frames % snap_every == 0
        && snapped != battle_frames) {
        char name[24];
        snapped = battle_frames;
        snprintf(name, sizeof name, "snap-%05u.ppm", battle_frames);
        copy_file(pc_out_path("loading.ppm"), pc_out_path(name));
    } else if (snap_every > 0 && !battle_frames && frames % snap_every == 0) {
        /* Before the battle loop (title, load screen) count presented frames. */
        char name[24];
        snprintf(name, sizeof name, "title-%05u.ppm", frames);
        copy_file(pc_out_path("loading.ppm"), pc_out_path(name));
    }
    if (frames == 100 || frames == 400) {
        char name[24];
        snprintf(name, sizeof name, "frame-%u.ppm", frames);
        copy_file(pc_out_path("loading.ppm"), pc_out_path(name));
    }
    if (frames++ == 0)
        fprintf(stderr, "[boot] original game submitted display frame: %s\n", pc_out_path("loading.ppm"));
    if (stop_at_loading) {
        pc_log("loading-screen checkpoint reached (not title boot)");
        exit(0);
    }
}
/* Debug aid: `kill -USR1 <pid>` prints the native call stack; resolve the
 * addresses with pc/tools/where.sh. */
static void pc_dump_stack(int sig)
{
    void* frames[64];
#ifdef _WIN32
    int n = CaptureStackBackTrace(0, 62, frames, NULL);
#else
    int n = backtrace(frames, 64);
#endif
    char line[32];
    (void)sig;
    write(2, "[stack]", 7);
    for (int i = 0; i < n; i++) {
        int len = snprintf(line, sizeof line, " %p", frames[i]);
        write(2, line, len);
    }
    write(2, "\n", 1);
}
/* Fatal signals print the native call stack (resolve it with
 * pc/tools/where.sh resolve) and the faulting battle frame, then re-raise. */
static void pc_crash(int sig)
{
    char line[96];
    int len = snprintf(line, sizeof line, "[crash] signal %d (%s) at battle frame %u\n", sig,
        sig == SIGSEGV ? "segmentation fault" : sig == SIGFPE ? "arithmetic" : "fatal",
        battle_frames);
    write(2, line, len);
    pc_dump_stack(sig);
    signal(sig, SIG_DFL);
    raise(sig);
}
/* psx_set() (pc/src/psx_types.h) was given a host pointer with no 4-byte
 * guest address: a PS1-layout field cannot hold it. Stop with the stack. */
__attribute__((noreturn)) void pc_psx_bad_pointer(const void* pointer)
{
    fprintf(stderr, "[lp64] %p has no guest address (psx_set on a PS1-layout field)\n", pointer);
    abort(); /* pc_crash prints the stack */
}
/* The game's stack on 64 bits. The PS1's stack was guest RAM, and game code
 * keeps addresses of its locals in 4-byte fields (psx_ptr members, guest
 * words, effect spawn records built on the stack), so the game thread runs on
 * a stack mapped below 2 GB (MAP_32BIT), where every address fits 32 bits. */
#ifdef _WIN32
/* Windows cannot place a thread's or a fiber's stack, so the main thread
 * switches to one allocated below 2 GB: rood_call_on_stack(top, fn) calls
 * fn with rsp = top. Its unwind info (frame pointer rbp) lets stack walks
 * and exception dispatch cross back to the original stack. */
extern void rood_call_on_stack(void* top, void (*fn)(void));
__asm__(".text\n"
        ".globl rood_call_on_stack\n"
        ".def rood_call_on_stack; .scl 2; .type 32; .endef\n"
        ".seh_proc rood_call_on_stack\n"
        "rood_call_on_stack:\n"
        "    pushq %rbp\n"
        "    .seh_pushreg %rbp\n"
        "    movq %rsp, %rbp\n"
        "    .seh_setframe %rbp, 0\n"
        "    .seh_endprologue\n"
        "    movq %rcx, %rsp\n"
        "    subq $32, %rsp\n" /* the callee's register home area */
        "    callq *%rdx\n"
        "    movq %rbp, %rsp\n"
        "    popq %rbp\n"
        "    ret\n"
        ".seh_endproc\n");
/* Fatal exceptions in the game's own code print [crash] and the stack like
 * the Linux signal handler. A vectored handler runs before the frame-based
 * search (which cannot leave the switched stack); exceptions elsewhere (a
 * driver's own handled ones) are left alone. */
extern char __ImageBase[];
static LONG WINAPI pc_exception(EXCEPTION_POINTERS* info)
{
    DWORD code = info->ExceptionRecord->ExceptionCode;
    uintptr_t at = (uintptr_t)info->ExceptionRecord->ExceptionAddress;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION
        && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_PRIV_INSTRUCTION
        && code != EXCEPTION_STACK_OVERFLOW)
        return EXCEPTION_CONTINUE_SEARCH;
    if (at < (uintptr_t)__ImageBase || at >= 0x80000000u)
        return EXCEPTION_CONTINUE_SEARCH;
    char line[160];
    int len = snprintf(line, sizeof line,
        "[crash] exception 0x%08lx (%s) at %p, address %p, battle frame %u\n", (unsigned long)code,
        code == EXCEPTION_ACCESS_VIOLATION ? "access violation" : code == EXCEPTION_INT_DIVIDE_BY_ZERO ? "arithmetic" : "fatal",
        (void*)at, code == EXCEPTION_ACCESS_VIOLATION ? (void*)info->ExceptionRecord->ExceptionInformation[1] : NULL,
        battle_frames);
    write(2, line, len);
    /* The faulting frame first: the backtrace below starts in this handler. */
    pc_dump_stack(0);
    _exit(3);
}
/* Reserved in main before the window: graphics drivers reserve much of the
 * address space below 2 GB when they load. */
static char* game_stack;
static const size_t game_stack_size = 64u << 20;
static int reserve_game_stack(void)
{
    for (uintptr_t at = 0x02000000; !game_stack && at + game_stack_size <= 0x7f000000; at += 1u << 20)
        game_stack = VirtualAlloc((void*)at, game_stack_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    return game_stack != NULL;
}
static void run_game_on_low_stack(void)
{
    size_t size = game_stack_size;
    char* stack = game_stack;
    /* Stack bounds in the thread information block, for code that checks
     * them (exception dispatch, drivers). */
    NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
    void *base = tib->StackBase, *limit = tib->StackLimit;
    tib->StackBase = stack + size;
    tib->StackLimit = stack;
    rood_call_on_stack(stack + size - 64, vs_main_exec);
    tib->StackBase = base;
    tib->StackLimit = limit;
}
#else
static ucontext_t game_context, host_context;
static void run_game(void) { vs_main_exec(); }
/* The game's stack at a fixed address (both word sizes), reserved in main
 * before the window: a save state (snapshot.c) holds the game's stack and
 * resumes it in a new process, where every saved frame pointer and return
 * address must mean the same. Elsewhere (taken) only snapshots are lost. */
#define GAME_STACK_BASE 0x60000000u
#define GAME_STACK_SIZE (64u << 20)
static char* game_stack;
static int game_stack_fixed;
static void reserve_game_stack(void)
{
    void* p = mmap((void*)(uintptr_t)GAME_STACK_BASE, GAME_STACK_SIZE, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != MAP_FAILED && (uintptr_t)p == GAME_STACK_BASE) {
        game_stack = p;
        game_stack_fixed = 1;
        return;
    }
    if (p != MAP_FAILED)
        munmap(p, GAME_STACK_SIZE); /* an old kernel's hint, not the address */
    fprintf(stderr, "[snapshot] game stack: 0x%x is taken; save states are off\n", GAME_STACK_BASE);
    p = mmap(NULL, GAME_STACK_SIZE, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | (sizeof(void*) == 8 ? MAP_32BIT : 0), -1, 0);
    if (p == MAP_FAILED || (uintptr_t)p + GAME_STACK_SIZE > 0x80000000u) {
        perror("game stack below 2 GB");
        exit(1);
    }
    game_stack = p;
}
/* snapshot.c: the stack's range (NULL when it is not at its fixed address). */
char* pc_game_stack(size_t* size)
{
    *size = GAME_STACK_SIZE;
    return game_stack_fixed ? game_stack : NULL;
}
static void run_game_on_low_stack(void)
{
    getcontext(&game_context);
    game_context.uc_stack.ss_sp = game_stack;
    game_context.uc_stack.ss_size = GAME_STACK_SIZE;
    game_context.uc_link = &host_context;
    makecontext(&game_context, run_game, 0);
    swapcontext(&host_context, &game_context);
}
#endif
/* Guest memory at its PS1 addresses (RAM, scratchpad, the I/O page); NULL
 * when a range is taken. */
static void* map_guest(uintptr_t address, size_t size)
{
#ifdef _WIN32
    return VirtualAlloc((void*)address, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void* p = mmap((void*)address, size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return p == MAP_FAILED ? NULL : p;
#endif
}
int main(int argc, char** argv)
{
#ifdef _WIN32
    AddVectoredExceptionHandler(1, pc_exception);
    setvbuf(stderr, NULL, _IONBF, 0); /* the log, as on Linux */
#else
    signal(SIGUSR1, pc_dump_stack);
    signal(SIGBUS, pc_crash);
#endif
    signal(SIGSEGV, pc_crash);
    signal(SIGFPE, pc_crash);
    signal(SIGILL, pc_crash);
    signal(SIGABRT, pc_crash); /* e.g. stack protector failures */
    int window = 0, fast = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--loading-checkpoint"))
            stop_at_loading = 1;
        else if (!strcmp(argv[i], "--title-checkpoint"))
            stop_at_title = 1;
        else if (!strcmp(argv[i], "--opening-checkpoint"))
            stop_at_opening = 1;
        else if (!strcmp(argv[i], "--battle-frame-checkpoint")) {
            char* end;
            if (++i == argc) {
                fprintf(stderr, "--battle-frame-checkpoint requires a frame number\n");
                return 1;
            }
            unsigned long value = strtoul(argv[i], &end, 10);
            if (*end || !value || value > 1000000) {
                fprintf(stderr, "invalid battle checkpoint frame: %s\n", argv[i]);
                return 1;
            }
            stop_at_battle_frame = value;
        } else if (!strcmp(argv[i], "--room-checkpoint"))
            pc_room_checkpoint_requested = 1;
        else if (!strcmp(argv[i], "--skip-movies"))
            pc_skip_movies = 1;
        else if (!strcmp(argv[i], "--window"))
            window = 1;
        else if (!strcmp(argv[i], "--fast"))
            fast = 1;
        else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 1;
        }
    }
    /* Guest memory first: the window's graphics driver (Vulkan and its
     * layers especially) maps memory of its own, which must not land in the
     * guest's fixed ranges. */
    void* ram = map_guest(0x80000000, 0x200000);
#ifdef _WIN32
    /* Windows reserves in 64 KB granules: the scratchpad and the I/O page
     * (below) are one allocation. */
    void* scratch = map_guest(0x1f800000, 0x2000);
    void* io = scratch;
#else
    void* scratch = map_guest(0x1f800000, 0x1000);
    /* I/O page: the sound driver writes SPU registers (0x1F801C00..) here;
     * pc/src/audio_spu.cpp synchronises them into the software SPU. */
    void* io = map_guest(0x1f801000, 0x1000);
#endif
    if (!ram || !scratch || !io) {
        perror("guest mapping");
        return 1;
    }
#ifdef _WIN32
    if (!reserve_game_stack()) {
        fprintf(stderr, "game stack below 2 GB: no free range\n");
        return 1;
    }
#else
    reserve_game_stack();
#endif
    pc_window_init(window, fast);
    pc_audio_output_start();
    FILE* f = fopen("data/SLUS_010.40", "rb");
    if (!f) {
        perror("original executable");
        return 1;
    }
    unsigned char header[2048];
    if (fread(header, 1, sizeof header, f) != sizeof header
        || memcmp(header, "PS-X EXE", 8))
        return 1;
    uint32_t address, size;
    memcpy(&address, header + 0x18, 4);
    memcpy(&size, header + 0x1c, 4);
    if (address < 0x80000000u || address >= 0x80200000u || size > 0x80200000u - address)
        return 1;
    if (fread((void*)(uintptr_t)address, 1, size, f) != size)
        return 1;
    fclose(f);
    pc_log(sizeof(void*) == 8
            ? "64-bit native game startup; original RAM data loaded, MIPS code is non-executable"
            : "32-bit native game startup; original RAM data loaded, MIPS code is non-executable");
    pc_log(window ? "backend: keyboard/gamepad input, file-backed memory cards (pc/save)"
                  : "backend: disconnected controllers, file-backed memory cards (pc/save)");
    pc_overlay_statics_init();
    if (pc_snapshot_resume_if_requested)
        pc_snapshot_resume_if_requested(); /* ROOD_SNAPSHOT: does not return (snapshot.c) */
    run_game_on_low_stack();
    return 0;
}
int pc_disc_read(int lba, unsigned int size, void* dest)
{
    if (lba < 0)
        pc_unimplemented("negative disc LBA");
    FILE* f = fopen("disks/SLUS-01040.bin", "rb");
    if (!f) {
        perror("disc image");
        exit(1);
    }
    unsigned char sector[2352];
    int first = lba;
    fprintf(stderr, "[disc] LBA %d, %u bytes -> %p\n", lba, size, dest);
    while (size) {
        if (fseek(f, (long)lba * 2352, SEEK_SET)
            || fread(sector, 1, sizeof sector, f) != sizeof sector)
            pc_unimplemented("disc read failed");
        unsigned int offset = sector[15] == 2 ? 24 : 16;
        if (sector[15] != 1 && sector[15] != 2)
            pc_unimplemented("unsupported sector format");
        unsigned int n = size < 2048 ? size : 2048;
        memcpy(dest, sector + offset, n);
        dest = (unsigned char*)dest + n;
        size -= n;
        ++lba;
    }
    fclose(f);
    pc_overlay_statics_loaded(first);
    return 1;
}
extern int pc_gte_centre __attribute__((weak)); /* PsyX_GTE.cpp: widescreen narrows about it */
void pc_display_env(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0 || w > 1024 || h > 512)
        pc_unimplemented("display dimensions");
    if (&pc_gte_centre)
        pc_gte_centre = w / 2;
    display_x = x;
    display_y = y;
    display_w = w;
    display_h = h;
    if (frames)
        pc_present();
}
void pc_draw_env(int x, int y, int w, int h, int ox, int oy, int tp)
{
    clip_x = x;
    clip_y = y;
    clip_w = w;
    clip_h = h;
    draw_x = ox;
    draw_y = oy;
    tpage = tp;
}
static int clamp5(int x) { return x < 0 ? 0 : x > 31 ? 31 : x; }
static void pixel(int x, int y, uint16_t color, int semi)
{
    if (x < clip_x || y < clip_y || x >= clip_x + clip_w || y >= clip_y + clip_h || x < 0
        || y < 0 || x >= 1024 || y >= 512)
        return;
    uint16_t* dst = &vram[y * 1024 + x];
    if (mask_check && (*dst & 0x8000))
        return;
    if (semi) {
        int output = 0;
        for (int ch = 0; ch < 3; ch++) {
            int a = (*dst >> (ch * 5)) & 31, b = (color >> (ch * 5)) & 31, c = 0;
            switch ((tpage >> 5) & 3) {
            case 0:
                c = (a / 2) + (b / 2);
                break;
            case 1:
                c = a + b;
                break;
            case 2:
                c = a - b;
                break;
            case 3:
                c = a + b / 4;
                break;
            }
            output |= clamp5(c) << (ch * 5);
        }
        color = (color & 0x8000) | output;
    }
    *dst = color | (mask_set ? 0x8000 : 0);
}
static uint16_t texture(int u, int v, unsigned clut)
{
    int tx = (tpage & 15) * 64, ty = ((tpage >> 4) & 1) * 256;
    unsigned val;
    unsigned maskx = (texture_window & 31) * 8, masky = ((texture_window >> 5) & 31) * 8;
    u = ((u & ~maskx) | (((texture_window >> 10) & 31) * 8 & maskx)) & 255;
    v = ((v & ~masky) | (((texture_window >> 15) & 31) * 8 & masky)) & 255;
    switch ((tpage >> 7) & 3) {
    case 0:
        val =
            (vram[((ty + v) & 511) * 1024 + ((tx + u / 4) & 1023)] >> ((u & 3) * 4)) & 15;
        break;
    case 1:
        val = (vram[((ty + v) & 511) * 1024 + ((tx + u / 2) & 1023)] >> ((u & 1) * 8))
            & 255;
        break;
    default:
        /* 2: 15-bit direct. 3 is "reserved" and the GPU reads it as 15-bit
         * too (the ending's rank screen background uses it). */
        return vram[((ty + v) & 511) * 1024 + ((tx + u) & 1023)];
    }
    return vram[((clut >> 6) & 511) * 1024 + (((clut & 63) * 16 + val) & 1023)];
}
/* The command word of the sprite or polygon the hardware path is being
 * handed (gpu_hw.c: which game string a glyph belongs to, ui_text.c). */
const void* pc_gpu_command;
#include "boot_polygons.inc"
#include "boot_lines.inc"

static void commands(uint32_t* p, unsigned n)
{
    unsigned i = 0;
    while (i < n) {
        unsigned code = p[i] >> 24;
        if (code == 0xe1) {
            tpage = p[i++] & 0x7ff;
            continue;
        }
        if (code == 0xe2) {
            texture_window = p[i] & 0xfffff;
            ++i;
            continue;
        }
        if (code == 0xe6) {
            mask_set = p[i] & 1;
            mask_check = p[i] & 2;
            ++i;
            continue;
        }
        if ((code & 0xe0) == 0x80) {
            if (n - i < 4)
                pc_unimplemented("truncated VRAM copy");
            unsigned sx = p[i + 1] & 1023, sy = (p[i + 1] >> 16) & 511;
            unsigned dx = p[i + 2] & 1023, dy = (p[i + 2] >> 16) & 511;
            unsigned w = ((p[i + 3] - 1) & 1023) + 1;
            unsigned h = (((p[i + 3] >> 16) - 1) & 511) + 1;
            if (pc_hw_stale_copy)
                pc_hw_stale_copy((int)sx, (int)sy, (int)dx, (int)dy, (int)w, (int)h);
            for (unsigned y = 0; y < h; ++y)
                for (unsigned x = 0; x < w; x += 16) {
                    uint16_t block[16];
                    unsigned count = w - x < 16 ? w - x : 16;
                    for (unsigned j = 0; j < count; ++j)
                        block[j] = vram[((sy + y) & 511) * 1024 + ((sx + x + j) & 1023)];
                    for (unsigned j = 0; j < count; ++j) {
                        uint16_t* dst =
                            &vram[((dy + y) & 511) * 1024 + ((dx + x + j) & 1023)];
                        if (!mask_check || !(*dst & 0x8000))
                            *dst = block[j] | (mask_set ? 0x8000 : 0);
                    }
                }
            pc_hw_mark(dx, dy, dx + w - 1, dy + h - 1);
            pc_hw_copy(sx, sy, dx, dy, w, h);
            i += 4;
            continue;
        }
        /* GP0 0x00/0x01 and the undocumented 0x03-0x1E occupy one FIFO word
         * and draw nothing (PSX-SPX); 0x02 (fill) remains unsupported. */
        if (code == 0x00 || code == 0x01 || (code >= 0x03 && code <= 0x1e)) {
            ++i;
            continue;
        }
        /* GP0 0xE0, 0xE7-0xEF and the undefined 0xF0-0xFF also take one word
         * and draw nothing (as DuckStation decodes them). The memory-card
         * screen sends 0xFF when its fade colour overflows the command byte. */
        if (code == 0xe0 || (code >= 0xe7 && code <= 0xff)) {
            static int reported;
            if (code >= 0xf0 && !reported++)
                fprintf(stderr, "[gpu] undefined GP0 0x%02x ignored as on hardware\n", code);
            ++i;
            continue;
        }
        if (code >= 0x20 && code <= 0x3f) {
            i += polygon_command(p + i, n - i);
            continue;
        }
        if (code >= 0x40 && code <= 0x5f) {
            i += line_command(p + i, n - i);
            continue;
        }
        if (code == 0xe3) {
            int right = clip_x + clip_w, bottom = clip_y + clip_h;
            clip_x = p[i] & 1023;
            clip_y = (p[i] >> 10) & 511;
            clip_w = right - clip_x;
            clip_h = bottom - clip_y;
            ++i;
            continue;
        }
        if (code == 0xe4) {
            clip_w = (p[i] & 1023) + 1 - clip_x;
            clip_h = ((p[i] >> 10) & 511) + 1 - clip_y;
            ++i;
            continue;
        }
        if (code == 0xe5) {
            draw_x = (int32_t)(p[i] << 21) >> 21;
            draw_y = (int32_t)(p[i] << 10) >> 21;
            ++i;
            continue;
        }
        /* Rectangles: GP0 0x60-0x7F. Bits 3-4 select variable size (from a
         * size word), 1x1, 8x8 or 16x16; bit 2 textured, bit 1 semi, bit 0 raw. */
        if ((code & 0xe0) == 0x60) {
            int tex = (code & 4) != 0, size = (code >> 3) & 3;
            unsigned words = 2 + tex + (size == 0);
            if (n - i < words)
                pc_unimplemented("truncated sprite packet");
            int x = (int16_t)p[i + 1] + draw_x, y = (int16_t)(p[i + 1] >> 16) + draw_y;
            unsigned wh = size == 0 ? p[i + words - 1] : size == 1 ? 0x10001u : size == 2 ? 0x80008u : 0x100010u;
            /* The GPU keeps 10 bits of the width and 9 of the height (PSX-SPX,
             * as DuckStation decodes them). The game relies on it: MENU0's LV
             * picker overwrites a "--" sprite's texpage word with 0x64602000
             * (84.c, levelledSpell), so the GPU reads a sprite from there whose
             * size word is the real one's UV/CLUT (0x37FE70C0: 192x510, at
             * y 0x6400, off screen). */
            unsigned w = wh & 0x3ff, h = (wh >> 16) & 0x1ff;
            if (w != (wh & 0xffff) || h != wh >> 16) {
                static int reported;
                if (!reported++)
                    fprintf(stderr, "[gpu] sprite size 0x%08x masked to %ux%u as on hardware\n", wh, w, h);
            }
            unsigned uv = tex ? p[i + 2] : 0;
            if (tex && pc_hw_sync_texture)
                pc_hw_sync_texture(tpage, uv >> 16, texture_window, uv & 255, (uv >> 8) & 255,
                    (int)(uv & 255) + (int)w - 1, (int)((uv >> 8) & 255) + (int)h - 1);
            {
                int clip[4] = { clip_x, clip_y, clip_w, clip_h };
                pc_hw_set_2d(1); /* sprites and tiles are screen-space */
                int texels[4] = { (int)(uv & 255), (int)((uv >> 8) & 255), (int)(uv & 255) + (int)w - 1, (int)((uv >> 8) & 255) + (int)h - 1 };
                pc_hw_set_bounds(x, y, x + (int)w, y + (int)h, code, tpage, tex ? texels : NULL, clip);
                pc_gpu_command = p + i;
                pc_hw_rect(x, y, (int)w, (int)h, uv & 255, (uv >> 8) & 255, p[i] & 0xffffff, code,
                    tpage, uv >> 16, texture_window, clip, mask_set, mask_check);
                pc_gpu_command = NULL;
                pc_hw_mark(x, y, x + (int)w - 1, y + (int)h - 1);
            }
            for (unsigned yy = 0; yy < h; yy++)
                for (unsigned xx = 0; xx < w; xx++) {
                    uint16_t c;
                    if (tex) {
                        c = texture((uv & 255) + xx, ((uv >> 8) & 255) + yy, uv >> 16);
                        if (c == 0)
                            continue;
                        if (!(code & 1)) {
                            unsigned mod = c & 0x8000;
                            for (int ch = 0; ch < 3; ch++)
                                mod |= clamp5(((c >> (ch * 5)) & 31)
                                              * ((p[i] >> (ch * 8)) & 255) / 128)
                                    << (ch * 5);
                            c = mod;
                        }
                    } else
                        c = ((p[i] & 255) >> 3) | ((((p[i] >> 8) & 255) >> 3) << 5)
                          | ((((p[i] >> 16) & 255) >> 3) << 10);
                    pixel(x + xx, y + yy, c, (code & 2) && (!tex || (c & 0x8000)));
                }
            i += words;
            continue;
        }
        fprintf(stderr, "GPU opcode 0x%02x (%u words remain)\n", code, n - i);
        pc_unimplemented("GPU command");
    }
}
/* Native draw lists (render.c) sit in the ordering table as marker packets:
 * GPU no-ops here, drawn by the hardware path at their place. */
extern int render_is_marker(const uint32_t*) __attribute__((weak));
extern void pc_hw_native_marker(uint32_t, int, int, const int*, unsigned, int) __attribute__((weak));
extern int render_translucent_marker(const uint32_t*, uint32_t*, int*, int*) __attribute__((weak));
extern void pc_hw_native_translucent(uint32_t, int, int, int, int, const int*, unsigned, int) __attribute__((weak));
/* Vector talk bubbles (ui_bubbles.c): their packets are drawn here as on the
 * PS1 but left out of the hardware target, which draws the shapes instead. */
extern void pc_ui_bubble_packet(const void*, int, int, const int*) __attribute__((weak));
void pc_draw_packet(void* data, int chain)
{
    uint32_t* p = data;
    for (unsigned links = 0; links < 100000; links++) {
        uint32_t tag = *p;
        if (pc_hw_anchor_packet)
            pc_hw_anchor_packet(p, draw_x, display_w);
        if (pc_ui_bubble_packet) {
            int clip[4] = { clip_x, clip_y, clip_w, clip_h };
            pc_ui_bubble_packet(p, draw_x, draw_y, clip);
        }
        if (render_is_marker && pc_hw_native_marker && render_is_marker(p)) {
            int clip[4] = { clip_x, clip_y, clip_w, clip_h };
            pc_hw_native_marker(p[2], draw_x, draw_y, clip, texture_window, (int)mask_set);
        }
        uint32_t list;
        int first, count;
        if (render_translucent_marker && pc_hw_native_translucent && render_translucent_marker(p, &list, &first, &count)) {
            int clip[4] = { clip_x, clip_y, clip_w, clip_h };
            pc_hw_native_translucent(list, first, count, draw_x, draw_y, clip, texture_window, (int)mask_set);
        }
        commands(p + 1, tag >> 24);
        unsigned next = tag & 0xffffff;
        if (!chain || next == 0xffffff)
            return;
        /* Links are the low 24 bits of a guest RAM address (0x80000000). The
         * 32-bit build also follows native addresses below 16 MB; the 64-bit
         * build links only guest memory (its native code is not placed). */
        if (next < 0x200000)
            p = (void*)(uintptr_t)(next | 0x80000000u);
        else if (sizeof(void*) == 4 && next >= 0x400000 && next < 0x1000000)
            p = (void*)(uintptr_t)next;
        else
            pc_unimplemented("ordering table pointer outside mapped memory");
    }
    pc_unimplemented("ordering table cycle");
}

void pc_title_menu_ready(void)
{
    pc_present();
    pc_log("TITLE MENU READY: original game entered title input loop");
    if (stop_at_title)
        exit(0);
}

static struct timespec last_vsync;

extern void pc_rpc_poll(void);
extern void pc_cd_tick(void) __attribute__((weak));
extern void pc_fog_vsync(void) __attribute__((weak)); /* atmosphere.cpp: the interactive fog steps */
static unsigned vsyncs;
unsigned pc_vsync_count(void) { return vsyncs; }
unsigned pc_battle_frame_count(void) { return battle_frames; }
/* Developer RPC screenshot: the displayed frame as a binary PPM. */
int pc_capture_display(const char* path, int* width, int* height)
{
    if (pc_hw_sync)
        pc_hw_sync(display_x, display_y, display_w, display_h);
    FILE* f = fopen(path, "wb");
    if (!f)
        return -1;
    fprintf(f, "P6\n%d %d\n255\n", display_w, display_h);
    for (int y = 0; y < display_h; y++)
        for (int x = 0; x < display_w; x++) {
            unsigned char rgb[3];
            display_pixel(x, y, rgb);
            fwrite(rgb, 1, 3, f);
        }
    *width = display_w;
    *height = display_h;
    return fclose(f) ? -1 : 0;
}
/* Present the current frame again (paused developer view). */
void pc_window_redraw(void) { pc_window_frame(vram, display_x, display_y, display_w, display_h); }
extern int pc_present_rate __attribute__((weak));
extern float pc_present_phase __attribute__((weak));
extern int pc_hw_interpolate __attribute__((weak));
extern int pc_hw_active __attribute__((weak));
extern void pc_window_wait_fraction(int k, int n) __attribute__((weak));
void pc_tick(void)
{
    ++vsyncs;
    pc_rpc_poll();
    if (pc_fog_vsync)
        pc_fog_vsync(); /* once per game vsync, after the RPC's changes, before the frame */
    pc_window_tick();
    if (pc_cd_tick)
        pc_cd_tick(); /* before the audio: this field's XA sectors */
    pc_audio_frame();
    clock_gettime(CLOCK_MONOTONIC, &last_vsync);
    pc_window_frame(vram, display_x, display_y, display_w, display_h);
    /* More images per vsync (frame interpolation at its fractions). */
    if (&pc_present_rate && &pc_present_phase && pc_window_wait_fraction && &pc_hw_interpolate && &pc_hw_active
        && pc_present_rate > 1 && pc_hw_interpolate && pc_hw_active) {
        int rate = pc_present_rate > 8 ? 8 : pc_present_rate;
        for (int k = 1; k < rate; k++) {
            pc_window_wait_fraction(k, rate);
            pc_present_phase = (float)k / (float)rate;
            pc_window_frame(vram, display_x, display_y, display_w, display_h);
        }
        pc_present_phase = 0;
    }
}

/* What the game is in, for host features that only belong in play (post.h:
 * depth of field): 1 the title (from boot, and again on every return to it,
 * boot_sdk.c vs_overlay_jumpToTitle), 2 an attract demo (the title's idle
 * cutscenes, modes 3 and 4), 0 a game started from the title. */
int pc_game_phase = 1;
void pc_title_start(int state)
{
    fprintf(stderr, "[title] starting mode %d (0=new game, 3/4=idle cutscene)\n", state);
    pc_game_phase = state == 3 || state == 4 ? 2 : 0;
}
void pc_title_selection(int item)
{
    fprintf(stderr, "[title] selected menu item %d\n", item);
}

void pc_finish_room_checkpoint(void)
{
    display_x = clip_x;
    display_y = clip_y;
    display_w = clip_w;
    display_h = clip_h;
    pc_present();
    if (pc_hw_sync) {
        pc_hw_sync(display_x, display_y, display_w, display_h);
        pc_present();
    }
    if (rename(pc_out_path("loading.ppm"), pc_out_path("room-checkpoint.ppm"))) {
        perror("room checkpoint");
        exit(1);
    }
    pc_log(
        "ROOM RENDER CHECKPOINT: loaded geometry preview, ignores scripted hide; not gameplay");
    exit(0);
}

/* Native equivalent of the elapsed scanline profiler. This is an estimate from
 * monotonic time since VSync, not emulated PS1 hardware-counter precision.
 * The AI reads it as a per-frame time budget, and VSync(1) waits poll it: input
 * recording and replay (input_record.c) read virtual scanlines instead, a fixed
 * step per read from each vblank, so a replay does what the recording did.
 * ROOD_DETERMINISTIC=1 reads them too without a recording: two runs given
 * the same vsync-exact input then reach the same game state (the save-state
 * equivalence test, pc/tests/snapshot_equivalence.py, compares them). */
extern int pc_input_deterministic(void) __attribute__((weak));
static int deterministic_scanlines(void)
{
    /* Host configuration, not game state: outside the saved sections. */
    static int env __attribute__((section(".data.rood_host"))) = -1;
    if (env < 0) {
        const char* value = getenv("ROOD_DETERMINISTIC");
        env = value && *value && strcmp(value, "0");
    }
    return env || (pc_input_deterministic && pc_input_deterministic());
}
int func_800E45B4(void)
{
    if (deterministic_scanlines()) {
        static unsigned lines, vblank;
        if (vblank != vsyncs) {
            vblank = vsyncs;
            lines = 0;
        }
        lines += 8;
        return lines < 263 ? (int)lines : 262; /* an NTSC field's lines */
    }
    if (!last_vsync.tv_sec && !last_vsync.tv_nsec)
        return 0;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int64_t elapsed = (int64_t)(now.tv_sec - last_vsync.tv_sec) * 1000000000 + now.tv_nsec
                    - last_vsync.tv_nsec;
    return (unsigned)((elapsed * 15734) / 1000000000) & 65535;
}

void pc_require_native_callback(unsigned address, const char* context, unsigned opcode)
{
    if (address < 0x00400000 || address >= 0x01000000) {
        fprintf(stderr, "[callback] %s: opcode 0x%02x, target 0x%08x\n", context, opcode,
            address);
        pc_unimplemented(context);
    }
}

/* EFFPURGE's _configurePlg calls this for every 0 entry. The original table
 * has NULL entries on purpose (the PS1 stores NULL for that slot): only an
 * effect with an entry that has no native function stops here. */
extern unsigned char pc_plg_effect_unresolved[] __attribute__((weak));
extern unsigned pc_plg_effect_count __attribute__((weak));
void pc_effect_plugin_unimplemented(unsigned effect)
{
    if (&pc_plg_effect_count && effect < pc_plg_effect_count && !pc_plg_effect_unresolved[effect])
        return;
    fprintf(stderr, "[effect] native PLG renderer required for effect %u\n", effect);
    pc_unimplemented("effect-specific PLG callback relocation");
}

/* Observe the original opening card (GIM 26) at full brightness. No script,
 * visibility, camera or timing state is changed to reach this checkpoint. */
void pc_opening_image_ready(int image)
{
    if (image == 26)
        opening_ready = 1;
}
void pc_battle_frame_complete(void)
{
    ++battle_frames;
    if (battle_frames % 300 == 0)
        fprintf(
            stderr, "[boot] original battle loop completed %u frames\n", battle_frames);
    if (stop_at_battle_frame && battle_frames >= stop_at_battle_frame) {
        display_x = clip_x;
        display_y = clip_y;
        display_w = clip_w;
        display_h = clip_h;
        pc_present();
        if (pc_hw_sync) {
        pc_hw_sync(display_x, display_y, display_w, display_h);
        pc_present();
    }
    if (rename(pc_out_path("loading.ppm"), pc_out_path("battle-checkpoint.ppm"))) {
            perror("battle checkpoint");
            exit(1);
        }
        fprintf(stderr,
            "[boot] BATTLE FRAME CHECKPOINT: %u original frames rendered; not a gameplay completion claim\n",
            battle_frames);
        exit(0);
    }
    if (!stop_at_opening || !opening_ready)
        return;
    /* Allow the original caption fade to complete over normal battle frames. */
    if (++opening_frames < 60)
        return;
    display_x = clip_x;
    display_y = clip_y;
    display_w = clip_w;
    display_h = clip_h;
    pc_present();
    if (pc_hw_sync) {
        pc_hw_sync(display_x, display_y, display_w, display_h);
        pc_present();
    }
    if (rename(pc_out_path("loading.ppm"), pc_out_path("opening-checkpoint.ppm"))) {
        perror("opening checkpoint");
        exit(1);
    }
    pc_log(
        "OPENING SCRIPT CHECKPOINT: original VKP Headquarters card rendered; not gameplay");
    exit(0);
}
