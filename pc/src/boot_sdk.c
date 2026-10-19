#include <sys/types.h>
#include <libgte.h>
#include <inline_c.h>
#include <libgpu.h>
#include <libetc.h>
#include <libpad.h>
extern void pc_log(const char*);
extern void pc_present(void);
extern void pc_vram_transfer(int, int, int, int, void*, int);
extern void pc_store_image_debug(int, int, int, int, const void*);
extern void pc_vram_clear(int, int, int, int, int, int, int);
extern void pc_unimplemented(const char*);
extern void pc_tick(void);
#include "audio.h"
#include "widescreen.h"
/* Weak so the standalone GTE/GPU probes link without the audio objects. */
#pragma weak pc_audio_set_tick
#pragma weak pc_audio_enable_tick
extern int func_800E45B4(void);
extern unsigned pc_window_buttons(void);
extern int pc_window_enabled;
static unsigned char* pad0;
static long video_mode;
static void (*vsync_callback)(void);
static void (*draw_callback)(void);
static unsigned int rng = 1;
void __main(void) { }
psx_long SetVideoMode(psx_long mode)
{
    long old = video_mode;
    video_mode = mode;
    return old;
}
int ResetCallback(void)
{
    vsync_callback = 0;
    draw_callback = 0;
    return 0;
}
int ResetGraph(int mode)
{
    (void)mode;
    return 0;
}
int SetGraphDebug(int mode) { return mode; }
/* SetDispMask(0) blanks the display until SetDispMask(1): the PS1 shows
 * black, not VRAM, while the game loads or rebuilds its frame buffers
 * (skipping the title's idle cutscene). The presenters draw black then;
 * ROOD_DISPLAY_MASK=0 ignores the mask (as before). */
int pc_display_masked;
extern char* getenv(const char*);
void SetDispMask(int mask)
{
    static int honour __attribute__((section(".data.rood_host"))) = -1; /* host setting: not saved state */
    if (honour < 0)
        honour = !getenv("ROOD_DISPLAY_MASK") || *getenv("ROOD_DISPLAY_MASK") != '0';
    pc_display_masked = honour && !mask;
    if (mask)
        pc_present();
}
int ClearImage(RECT* r, u_char red, u_char green, u_char blue)
{
    pc_vram_clear(r->x, r->y, r->w, r->h, red, green, blue);
    return 0;
}
int ClearImage2(RECT* r, u_char red, u_char green, u_char blue)
{
    return ClearImage(r, red, green, blue);
}
int LoadImage(RECT* r, u_long* p)
{
    pc_vram_transfer(r->x, r->y, r->w, r->h, p, 1);
    return 0;
}
/* Synchronous variant (waits for the GPU first); host transfers already are.
 * MENUE (Quick Manual) loads its pages with it. */
int LoadImage2(RECT* r, u_long* p) { return LoadImage(r, p); }
int StoreImage(RECT* r, u_long* p)
{
    pc_store_image_debug(r->x, r->y, r->w, r->h, __builtin_return_address(0));
    pc_vram_transfer(r->x, r->y, r->w, r->h, p, 0);
    return 0;
}
int DrawSync(int mode)
{
    (void)mode;
    return 0;
}
u_long DrawSyncCallback(void (*fn)(void))
{
    u_long old = psx_to_int(u_long, draw_callback);
    draw_callback = fn;
    return old;
}
static int ticks;
/* snapshot.c: pc_tick through the vsync boundary, where a save state cuts
 * the stack (the game's frames below, the host's above). Weak: the GTE probe
 * links this unit with its own pc_tick. */
extern void pc_vsync_boundary(void) __attribute__((weak));
/* One vertical blank: the field is presented and the VSync handler runs. */
void pc_vblank_wait(void)
{
    ++ticks;
    if (pc_vsync_boundary)
        pc_vsync_boundary();
    else
        pc_tick();
    if (pad0 && pc_window_enabled) {
        unsigned b = ~pc_window_buttons();
        pad0[0] = 0;
        pad0[1] = 0x41;
        pad0[2] = b >> 8;
        pad0[3] = b;
    }
    if (vsync_callback)
        vsync_callback();
}
int VSync(int mode)
{
    if (mode < 0)
        return ticks;
    int elapsed = func_800E45B4();
    if (mode == 1)
        return elapsed; /* Original mode 1 queries elapsed scanlines without waiting. */
    int count = mode > 1 ? mode : 1;
    while (count--)
        pc_vblank_wait();
    return elapsed;
}
int VSyncCallback(void (*fn)(void))
{
    int old = psx_to_int(int, vsync_callback);
    vsync_callback = fn;
    return old;
}
DISPENV* SetDefDispEnv(DISPENV* env, int x, int y, int w, int h)
{
    unsigned char* p = (void*)env;
    unsigned int i;
    for (i = 0; i < sizeof(*env); i++)
        p[i] = 0;
    env->disp.x = x;
    env->disp.y = y;
    env->disp.w = w;
    env->disp.h = h;
    return env;
}
extern void pc_display_env(int, int, int, int);
extern int pc_display_rgb24;
/* disp.w counts screen pixels; in 24-bit mode they take 1.5 VRAM pixels each
 * (the movie shows 320 pixels from 480 VRAM columns). */
DISPENV* PutDispEnv(DISPENV* env)
{
    pc_display_rgb24 = env->isrgb24 != 0;
    pc_display_env(env->disp.x, env->disp.y, env->disp.w,
        (env->screen.h > 0 && env->screen.h * (env->isinter ? 2 : 1) < env->disp.h)
            ? env->screen.h * (env->isinter ? 2 : 1)
            : env->disp.h);
    return env;
}
void InitCARD(int mode) { (void)mode; }
void StartCARD(void) { }
void _bu_init(void) { }
void PadInitDirect(unsigned char* a, unsigned char* b)
{
    pad0 = a;
    a[0] = b[0] = 0xff;
}
void PadStartCom(void) { }
int PadInfoMode(int port, int info, int index)
{
    (void)index;
    return port == 0 && pc_window_enabled && info == InfoModeCurID ? 4 : 0;
}
int PadGetState(int port)
{
    return port == 0 && pc_window_enabled ? PadStateStable : PadStateDiscon;
}
void PadSetAct(int port, unsigned char* data, int size)
{
    (void)port;
    (void)data;
    (void)size;
}
int PadSetActAlign(int port, unsigned char* data)
{
    (void)data;
    return port == 0 && pc_window_enabled;
}
int PadSetMainMode(int port, int mode, int lock)
{
    (void)port;
    (void)mode;
    (void)lock;
    return 0;
}
/* The CD drive (libds, streaming, XA audio) is pc/src/cd_drive.c. */
void srand(unsigned int seed) { rng = seed; }
int rand(void)
{
    rng = rng * 1103515245u + 12345u;
    return (rng >> 16) & 32767;
}
void vs_overlay_getSp(int* sp) { *sp = 0; }
/* overlay.c's jumps reset $sp to a frame saved by vs_overlay_getSp and jump
 * to the title/battle entry; on the host they longjmp to restart points that
 * vs_main_exec / vs_main_execTitle record with __builtin_setjmp (main.c,
 * ROOD_PC). The GCC builtins avoid the SDK's smaller jmp_buf. */
void* pc_title_jump[5];
void* pc_battle_jump[5];
/* ui/menu_bridge.c (battle builds): the modern menus end a session the jump
 * leaves behind (a soft reset during the menu), weapon_slots.c forgets the
 * battle it was in. */
extern void pc_ui_menu_reset(void) __attribute__((weak));
extern int pc_game_phase __attribute__((weak)); /* boot_runtime.c (saved in save states) */
/* room_catalog.c: the Room Browser's warp (its savePointStates snapshot too)
 * belongs to the game being left: both jumps replace the game's state (the
 * title, then New Game or Continue; MENU7's load after a game over jumps
 * into BATTLE). */
extern void pc_room_warp_reset(void) __attribute__((weak));
/* fast_travel.c: the old game's room and actors are stale until a room loads
 * (the RPC report reads none meanwhile) */
extern void pc_fast_travel_reset(void) __attribute__((weak));
void vs_overlay_jumpToTitle(int* sp)
{
    (void)sp;
    if (&pc_game_phase)
        pc_game_phase = 1; /* back to the title */
    if (pc_ui_menu_reset)
        pc_ui_menu_reset();
    if (pc_room_warp_reset)
        pc_room_warp_reset();
    if (pc_fast_travel_reset)
        pc_fast_travel_reset();
    __builtin_longjmp(pc_title_jump, 1);
}
void vs_overlay_jumpToBattle(int* sp)
{
    (void)sp;
    if (pc_ui_menu_reset)
        pc_ui_menu_reset();
    if (pc_room_warp_reset)
        pc_room_warp_reset();
    if (pc_fast_travel_reset)
        pc_fast_travel_reset();
    __builtin_longjmp(pc_battle_jump, 1);
}
void vs_overlay_wait(void) { }
/* Original game register defaults, used with the software GTE. */
extern void CTC2(unsigned int, int);
void InitGeom(void)
{
    CTC2(0x155, 29);
    CTC2(0x100, 30);
    CTC2(1000, 26);
    CTC2((unsigned)-0x1062, 27);
    CTC2(0x1400000, 28);
    CTC2(0, 24);
    CTC2(0, 25);
}
extern void pc_draw_env(int, int, int, int, int, int, int);
extern void pc_display_env(int, int, int, int);
extern void pc_draw_packet(void*, int);
psx_long GetVideoMode(void) { return video_mode; }
DRAWENV* SetDefDrawEnv(DRAWENV* e, int x, int y, int w, int h)
{
    e->clip.x = x;
    e->clip.y = y;
    e->clip.w = w;
    e->clip.h = h;
    e->ofs[0] = x;
    e->ofs[1] = y;
    e->tw.x = e->tw.y = e->tw.w = e->tw.h = 0;
    e->r0 = e->g0 = e->b0 = 0;
    e->dtd = 1;
    e->dfe = video_mode ? h < 289 : h < 257;
    e->tpage = 10;
    e->isbg = 0;
    return e;
}
/* libgpu SetDrawArea: a two-word packet setting the drawing area's top-left
 * (GP0 0xE3) and bottom-right (GP0 0xE4), 10-bit x and y fields. The SDK's
 * encoders (func_80029924/func_800299BC) are not matched in the decomp. */
void SetDrawArea(DR_AREA* p, RECT* r)
{
    int right = r->x + r->w - 1, bottom = r->y + r->h - 1;
    setlen(p, 2);
    p->code[0] = 0xE3000000 | ((r->y & 0x3FF) << 10) | (r->x & 0x3FF);
    p->code[1] = 0xE4000000 | ((bottom & 0x3FF) << 10) | (right & 0x3FF);
}
/* The editor's free camera sees past what the game's camera covers; the game
 * never clears, so clear each new draw area black while it flies. */
extern int pc_live_camera_active(void) __attribute__((weak));
/* Diagnostic (ROOD_UNDRAWN_DEBUG=1, RPC graphics undrawn_debug): each new
 * drawing area is filled magenta in the hardware target only (VRAM and the
 * game are untouched), so pixels a frame does not draw show magenta instead
 * of the picture drawn there two frames before. The field redraws its whole
 * screen every frame (the background sphere, pc_background_packets, then
 * the set): any magenta there is a hole (widescreen_undrawn_smoke.py).
 * Screens that draw over a held picture (menus) show magenta by design. */
int pc_undrawn_debug = -1;
extern void pc_hw_fill(int x, int y, int w, int h, int r, int g, int b) __attribute__((weak));
DRAWENV* PutDrawEnv(DRAWENV* e)
{
    pc_draw_env(
        e->clip.x, e->clip.y, e->clip.w, e->clip.h, e->ofs[0], e->ofs[1], e->tpage);
    if (pc_undrawn_debug < 0)
        pc_undrawn_debug = getenv("ROOD_UNDRAWN_DEBUG") && *getenv("ROOD_UNDRAWN_DEBUG") == '1';
    if (e->isbg)
        pc_vram_clear(e->clip.x, e->clip.y, e->clip.w, e->clip.h, e->r0, e->g0, e->b0);
    else if (pc_live_camera_active && pc_live_camera_active())
        pc_vram_clear(e->clip.x, e->clip.y, e->clip.w, e->clip.h, 0, 0, 0);
    else if (pc_undrawn_debug && pc_hw_fill)
        pc_hw_fill(e->clip.x, e->clip.y, e->clip.w, e->clip.h, 255, 0, 255);
    return e;
}
void DrawPrim(void* p) { pc_draw_packet(p, 0); }
/* Each drawn ordering table ends a frame for the GTE's projection log
 * (widescreen 2D/3D classification in the hardware rasterizer). */
extern void pc_gte_new_generation(void);
extern void pc_hw_begin_ot(void) __attribute__((weak));
void DrawOTag(u_long* p)
{
    if (pc_hw_begin_ot)
        pc_hw_begin_ot();
    pc_draw_packet(p, 1);
    pc_gte_new_generation();
}
/* Diagnostic font output is disabled; no game UI uses this debug font. */
void FntLoad(int x, int y)
{
    (void)x;
    (void)y;
}
int FntOpen(int x, int y, int w, int h, int bg, int n)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)bg;
    (void)n;
    return 0;
}
void SetDumpFnt(int id) { (void)id; }
/* Single-threaded event queue; memory-card completions are raised immediately. */
static struct {
    psx_ulong event;
    psx_long spec;
    int open, enabled, pending;
} events[32];
static int event_count, critical;
int EnterCriticalSection(void)
{
    int previous = critical;
    critical = 1;
    return !previous;
}
void ExitCriticalSection(void) { critical = 0; }
/* Root counter 2 (0xF2000002) with a handler clocks the sound driver; it is
 * run from the frame loop (pc_audio_frame). Other handlers stay fail-fast. */
static psx_long sound_event;
psx_long OpenEvent(psx_ulong event, psx_long spec, psx_long mode, psx_long (*fn)())
{
    (void)mode;
    /* Closed descriptors are reused, as in the BIOS event table: the
     * memory-card screens open and close 8 events on every visit. */
    int slot = 0;
    while (slot < event_count && events[slot].open)
        ++slot;
    if (slot == 32 || (fn && event != 0xF2000002))
        pc_unimplemented("event callback/table full");
    if (slot == event_count)
        ++event_count;
    events[slot].event = event;
    events[slot].spec = spec;
    events[slot].open = 1;
    events[slot].enabled = events[slot].pending = 0;
    if (fn) {
        if (!pc_audio_set_tick)
            pc_unimplemented("sound clock without the audio objects");
        pc_audio_set_tick((psx_long (*)(void))fn);
        sound_event = slot + 1;
    }
    return slot + 1;
}
psx_long EnableEvent(psx_long id)
{
    if (id < 1 || id > event_count)
        return 0;
    events[id - 1].enabled = 1;
    if (id == sound_event && pc_audio_enable_tick)
        pc_audio_enable_tick(1);
    return 1;
}
psx_long DisableEvent(psx_long id)
{
    if (id < 1 || id > event_count)
        return 0;
    events[id - 1].enabled = 0;
    if (id == sound_event && pc_audio_enable_tick)
        pc_audio_enable_tick(0);
    return 1;
}
psx_long CloseEvent(psx_long id)
{
    if (!DisableEvent(id))
        return 0;
    events[id - 1].open = 0;
    if (id == sound_event)
        sound_event = 0;
    return 1;
}
/* Clears pending deliveries of an event class; only the sound clock uses it
 * (at sound shutdown), and host events never queue deliveries for it. */
psx_long UnDeliverEvent(psx_ulong event, psx_long spec)
{
    (void)event;
    (void)spec;
    return 1;
}
psx_long TestEvent(psx_long id)
{
    if (id < 1 || id > event_count)
        return 0;
    int result = events[id - 1].enabled && events[id - 1].pending;
    events[id - 1].pending = 0;
    return result;
}
/* Raise every open event of class `event` with descriptor `spec`, as the BIOS
 * does when a memory-card operation completes (pc/src/memory_card.c). */
void pc_deliver_event(psx_ulong event, psx_long spec)
{
    for (int i = 0; i < event_count; i++)
        if (events[i].open && events[i].event == event && events[i].spec == spec)
            events[i].pending = 1;
}

void pc_insert_tpage(int after, int page)
{
    unsigned int *ot = psx_load(unsigned int*, 0x1f800004);
    unsigned int *packet = psx_load(unsigned int*, 0x1f800000);
    packet[1] = 0xe1000200 | (page & 0x1ff);
    packet[2] = 0;
    packet[0] = 0x02000000 | (ot[after] & 0xffffff);
    ot[after] = (ot[after] & 0xff000000) | (psx_to_int(unsigned int, packet) & 0xffffff);
    psx_store(0x1f800000, packet + 3);
}

/* Preserve the original SDK's terminating primitive and forward/reverse links. */
extern u_long D_800334F0[];
extern u_long D_80033504;
u_long* ClearOTag(u_long* ot, int n)
{
    if (n <= 0)
        pc_unimplemented("empty ordering table");
    while (--n) {
        *ot = psx_to_int(u_long, (ot + 1)) & 0xffffff;
        ++ot;
    }
    D_80033504 = (psx_to_int(u_long, D_800334F0) & 0xffffff) | 0x04000000;
    *ot = psx_to_int(u_long, &D_80033504) & 0xffffff;
    return ot;
}
u_long* ClearOTagR(u_long* ot, int n)
{
    if (n <= 0)
        pc_unimplemented("empty reverse ordering table");
    for (int i = n - 1; i > 0; --i)
        ot[i] = psx_to_int(u_long, &ot[i - 1]) & 0xffffff;
    D_80033504 = (psx_to_int(u_long, D_800334F0) & 0xffffff) | 0x04000000;
    *ot = psx_to_int(u_long, &D_80033504) & 0xffffff;
    return ot;
}

/* src/SLUS_010.40/32154.s: func_80041AF4, shared by VectorNormal,
 * VectorNormalS and VectorNormalSS. Normalizes (x, y, z) in place to 4096
 * and returns the squared length. Keeps the original reciprocal-square-root
 * table and GTE register effects. */
extern short D_80040D98[];
static unsigned normalize3(int* x, int* y, int* z)
{
    MTC2(*x, 9);
    MTC2(*y, 10);
    MTC2(*z, 11);
    gte_sqr0();
    unsigned sum = MFC2(25) + MFC2(26) + MFC2(27);
    MTC2(sum, 30);
    unsigned leading = MFC2(31) & ~1u;
    unsigned scaled = leading >= 24 ? sum << ((leading - 24) & 31)
                                    : (unsigned)((int)sum >> ((24 - leading) & 31));
    int reciprocal = D_80040D98[(int)scaled - 64];
    MTC2(*x, 9);
    MTC2(*y, 10);
    MTC2(*z, 11);
    MTC2(reciprocal, 8);
    gte_gpf0();
    unsigned shift = ((31 - (int)leading) >> 1) & 31;
    *x = (int)MFC2(25) >> shift;
    *y = (int)MFC2(26) >> shift;
    *z = (int)MFC2(27) >> shift;
    return sum;
}
psx_long VectorNormal(VECTOR* input, VECTOR* output)
{
    int x = input->vx, y = input->vy, z = input->vz;
    long sum = (long)normalize3(&x, &y, &z);
    output->vx = x;
    output->vy = y;
    output->vz = z;
    return sum;
}
/* VECTOR in, SVECTOR out (pad untouched). */
psx_long VectorNormalS(VECTOR* input, SVECTOR* output)
{
    int x = input->vx, y = input->vy, z = input->vz;
    long sum = (long)normalize3(&x, &y, &z);
    output->vx = x;
    output->vy = y;
    output->vz = z;
    return sum;
}
/* SVECTOR in and out (pad untouched). */
psx_long VectorNormalSS(SVECTOR* input, SVECTOR* output)
{
    int x = input->vx, y = input->vy, z = input->vz;
    long sum = (long)normalize3(&x, &y, &z);
    output->vx = x;
    output->vy = y;
    output->vz = z;
    return sum;
}

/* 32154.s composes rotation only, columns 2,1,0, retaining translation.
 * This game's CompMatrixLV is an in-place two-argument entry, unlike PsyQ's
 * normal three-argument API. Give that ABI its own C name and linker symbol. */
static MATRIX* compose_rotation(MATRIX* a, MATRIX* b, MATRIX* result)
{
    MATRIX right = *b;
    SetRotMatrix(a);
    for (int column = 2; column >= 0; --column) {
        SVECTOR v = { right.m[0][column], right.m[1][column], right.m[2][column], 0 };
        gte_ldv0(&v);
        gte_rtv0();
        result->m[0][column] = MFC2(9);
        result->m[1][column] = MFC2(10);
        result->m[2][column] = MFC2(11);
    }
    return result;
}
MATRIX* func_80041C68(MATRIX* a, MATRIX* b) { return compose_rotation(a, b, b); }
MATRIX* pc_comp_matrix(MATRIX*, MATRIX*) __asm__("CompMatrixLV");
MATRIX* pc_comp_matrix(MATRIX* a, MATRIX* b) { return compose_rotation(a, b, a); }

/* 31724.s: retain the original three GPF stages and their rounding order. */
MATRIX* RotMatrixYXZ_gte(SVECTOR* angles, MATRIX* result)
{
    int sx = rsin(angles->vx), cx = rcos(angles->vx);
    int sy2 = rsin(angles->vy) * 2, cy2 = rcos(angles->vy) * 2;
    int sz2 = rsin(angles->vz) * 2, cz2 = rcos(angles->vz) * 2;
    MTC2(sz2, 8);
    MTC2(sy2, 9);
    MTC2(cy2, 10);
    MTC2(cx, 11);
    gte_gpf12();
    int a0 = (int)MFC2(9), a1 = (int)MFC2(10), m10 = (int)MFC2(11) >> 1;
    MTC2(cz2, 8);
    MTC2(sy2, 9);
    MTC2(cy2, 10);
    MTC2(cx, 11);
    gte_gpf12();
    int a2 = (int)MFC2(9), a3 = (int)MFC2(10), m11 = (int)MFC2(11) >> 1;
    MTC2(sx, 8);
    MTC2(a0, 9);
    MTC2(a2, 10);
    MTC2(a1, 11);
    gte_gpf12();
    result->m[0][0] = ((int)MFC2(9) + a3) >> 2;
    result->m[0][1] = ((int)MFC2(10) - a1) >> 2;
    result->m[0][2] = (cx * sy2) >> 13;
    result->m[1][0] = m10;
    result->m[1][1] = m11;
    result->m[1][2] = -sx;
    result->m[2][0] = ((int)MFC2(11) - a2) >> 2;
    result->m[2][1] = (((sx * a3) >> 12) + a0) >> 2;
    result->m[2][2] = (cx * cy2) >> 13;
    return result;
}

/* 31724.s RotMatrix_gte: XYZ order, preserving GPF intermediate rounding. */
MATRIX* RotMatrix_gte(SVECTOR* angles, MATRIX* result)
{
    int sy = rsin(angles->vy), cy = rcos(angles->vy);
    int sx2 = rsin(angles->vx) * 2, cx2 = rcos(angles->vx) * 2;
    int sz2 = rsin(angles->vz) * 2, cz2 = rcos(angles->vz) * 2;
    MTC2(sz2, 8);
    MTC2(sx2, 9);
    MTC2(cx2, 10);
    MTC2(cy, 11);
    gte_gpf12();
    int a0 = (int)MFC2(9), a1 = (int)MFC2(10), m01 = -((int)MFC2(11) >> 1);
    MTC2(cz2, 8);
    MTC2(sx2, 9);
    MTC2(cx2, 10);
    MTC2(cy, 11);
    gte_gpf12();
    int a2 = (int)MFC2(9), a3 = (int)MFC2(10), m00 = (int)MFC2(11) >> 1;
    MTC2(sy, 8);
    MTC2(a2, 9);
    MTC2(a0, 10);
    MTC2(a3, 11);
    gte_gpf12();
    result->m[0][0] = m00;
    result->m[0][1] = m01;
    result->m[0][2] = sy;
    result->m[1][0] = ((int)MFC2(9) + a1) >> 2;
    result->m[1][1] = (a3 - (int)MFC2(10)) >> 2;
    result->m[1][2] = -((cy * sx2) >> 13);
    result->m[2][0] = (a0 - (int)MFC2(11)) >> 2;
    result->m[2][1] = (((sy * a1) >> 12) + a2) >> 2;
    result->m[2][2] = (cy * cx2) >> 13;
    return result;
}

/* BATTLE/48C7C.s: actor rotation has a different order from RotMatrixYXZ_gte. */
MATRIX* func_800B17F0(SVECTOR* angles, MATRIX* result)
{
    int sy = rsin(angles->vy), cy = rcos(angles->vy);
    int sx2 = rsin(angles->vx) * 2, cx2 = rcos(angles->vx) * 2;
    int sz2 = rsin(angles->vz) * 2, cz2 = rcos(angles->vz) * 2;
    MTC2(sz2, 8);
    MTC2(sx2, 9);
    MTC2(cx2, 10);
    MTC2(cy, 11);
    gte_gpf12();
    int a0 = (int)MFC2(9), a1 = (int)MFC2(10), m10 = (int)MFC2(11) >> 1;
    MTC2(cz2, 8);
    MTC2(sx2, 9);
    MTC2(cx2, 10);
    MTC2(cy, 11);
    gte_gpf12();
    int a2 = (int)MFC2(9), a3 = (int)MFC2(10), m00 = (int)MFC2(11) >> 1;
    MTC2(sy, 8);
    MTC2(a2, 9);
    MTC2(a0, 10);
    MTC2(a3, 11);
    gte_gpf12();
    result->m[0][0] = m00;
    result->m[0][1] = ((int)MFC2(9) - a1) >> 2;
    result->m[0][2] = ((int)MFC2(11) + a0) >> 2;
    result->m[1][0] = m10;
    result->m[1][1] = ((int)MFC2(10) + a3) >> 2;
    result->m[1][2] = (((sy * a1) >> 12) - a2) >> 2;
    result->m[2][0] = -sy;
    result->m[2][1] = (cy * sx2) >> 13;
    result->m[2][2] = (cy * cx2) >> 13;
    return result;
}
MATRIX* func_800B196C(MATRIX* a, MATRIX* b) { return compose_rotation(a, b, b); }
/* BATTLE/4B16C.s: scale matrix columns through GPF, preserving translation. */
MATRIX* func_800B396C(MATRIX* matrix, VECTOR* scale)
{
    int factors[3] = { scale->vx, scale->vy, scale->vz };
    for (int column = 0; column < 3; ++column) {
        MTC2(factors[column], 8);
        MTC2(matrix->m[0][column], 9);
        MTC2(matrix->m[1][column], 10);
        MTC2(matrix->m[2][column], 11);
        gte_gpf12();
        matrix->m[0][column] = MFC2(9);
        matrix->m[1][column] = MFC2(10);
        matrix->m[2][column] = MFC2(11);
    }
    return matrix;
}

extern short D_80040C18[];
/* 32154.s: table-based square roots, with the original leading-zero scaling. */
psx_long SquareRoot12(psx_long value)
{
    MTC2(value, 30);
    unsigned leading = MFC2(31);
    if (leading >> 5)
        return 0;
    leading &= 30;
    unsigned scaled = leading >= 24 ? (unsigned)value << ((leading - 24) & 31)
                                    : (unsigned)(value >> ((24 - leading) & 31));
    unsigned root = (int)D_80040C18[(int)scaled - 64];
    int exponent = (19 - (int)leading) >> 1;
    return exponent > 0 ? root << (exponent & 31) : root >> ((-exponent) & 31);
}
int vs_gte_rsqrt(int value)
{
    MTC2(value, 30);
    unsigned leading = MFC2(31);
    if (leading >> 5)
        return 0;
    leading &= 30;
    unsigned scaled = leading >= 24 ? (unsigned)value << ((leading - 24) & 31)
                                    : (unsigned)(value >> ((24 - leading) & 31));
    unsigned root = (int)D_80040C18[(int)scaled - 64];
    return (root << (((31 - leading) >> 1) & 31)) >> 12;
}

void OuterProduct0(VECTOR* a, VECTOR* b, VECTOR* result)
{
    unsigned old0 = CFC2(0), old2 = CFC2(2), old4 = CFC2(4);
    CTC2(a->vx, 0);
    CTC2(a->vy, 2);
    CTC2(a->vz, 4);
    MTC2(b->vx, 9);
    MTC2(b->vy, 10);
    MTC2(b->vz, 11);
    gte_op12();
    result->vx = MFC2(25);
    result->vy = MFC2(26);
    result->vz = MFC2(27);
    CTC2(old0, 0);
    CTC2(old2, 2);
    CTC2(old4, 4);
}

/* 2EA3C.s: local YXZ rotation followed by the scratchpad camera rotation.
 * Translation uses the GTE's existing TR registers, not camera->t. */
void pc_room_transform(VECTOR* position, VECTOR* angles, MATRIX* camera, MATRIX* result)
{
    SVECTOR rotation = { angles->vx, angles->vy, angles->vz, 0 };
    RotMatrixYXZ_gte(&rotation, result);
    compose_rotation(camera, result, result);
    MTC2((unsigned short)position->vx | ((unsigned)position->vy << 16), 0);
    MTC2(position->vz, 1);
    gte_mvmva(1, 0, 0, 0, 0);
    result->t[0] = MFC2(25);
    result->t[1] = MFC2(26);
    result->t[2] = MFC2(27);
}
void func_800985AC(VECTOR* position, VECTOR* angles, MATRIX* result)
{
    pc_room_transform(position, angles, (MATRIX*)0x1f800014, result);
}

/* 2EA3C.s 98648: inverse far-clip scale precedes camera/local rotation.
 * Keep the original signed-halfword scale and GTE translation registers. */
void pc_scaled_room_transform(
    VECTOR* position, VECTOR* angles, MATRIX* camera, int far_clip, MATRIX* result)
{
    MATRIX scale = { 0 }, scaled_camera = *camera;
    /* `div $zero` has no zero check: the R3000 leaves LO = -1 for the
     * positive dividend. */
    short factor = far_clip ? 0x1000000 / far_clip : -1;
    scale.m[0][0] = scale.m[1][1] = scale.m[2][2] = factor;
    compose_rotation(&scale, &scaled_camera, &scaled_camera);
    pc_room_transform(position, angles, &scaled_camera, result);
}
void func_80098648(VECTOR* position, VECTOR* angles, MATRIX* result)
{
    pc_scaled_room_transform(
        position, angles, (MATRIX*)0x1f800014, *(int*)0x1f800064, result);
}

/* 2EA3C.s background mesh: 34 packed vertices, then 64 indexed G3 triangles.
 * These coordinates are transformed without perspective, as in the original.
 *
 * It is the field's only clear: a gradient sphere about the camera (radius
 * ~410 screen pixels, so it covers the whole screen) drawn as 2D behind
 * everything; the game never clears the frame buffer (PutDrawEnv isbg 0).
 * Widescreen (port): its triangles are kept as far past the 4:3 sides as the
 * 16:9 view reaches and drawn as 2D of the 3D view (PC_ANCHOR_SCENE:
 * compressed like the narrowed 3D, clipped to the whole view). Before, they
 * were culled at the 4:3 sides and drawn compressed into the middle (or, the
 * ones reaching past the top or bottom, uncompressed as screen effects), so
 * parts of the view, mostly its sides, were never drawn where no set
 * reaches: they kept the picture drawn there two frames before, which
 * smeared as the camera moved ("trails"). 4:3 is unchanged. */
unsigned* pc_background_packets(
    unsigned* mesh, unsigned* packet, unsigned* ot, int transformed[34][4])
{
    int margin = PC_WIDESCREEN_MARGIN;
    unsigned* first = packet;
    for (int i = 0; i < 34; ++i) {
        MTC2(mesh[0], 0);
        MTC2(mesh[1], 1);
        gte_mvmva(1, 0, 0, 0, 0);
        transformed[i][0] = MFC2(25);
        transformed[i][1] = MFC2(26);
        transformed[i][2] = MFC2(27);
        transformed[i][3] = mesh[2];
        mesh += 3;
    }
    unsigned head = *ot;
    for (int i = 0; i < 64; ++i) {
        unsigned indices = mesh[i];
        int* v[3];
        int x[3], y[3];
        for (int j = 0; j < 3; ++j) {
            unsigned index = (indices >> (j * 8)) & 255;
            if (index >= 34)
                pc_unimplemented("background vertex index");
            v[j] = transformed[index];
            x[j] = v[j][0] + 160;
            y[j] = v[j][1] + 120;
        }
        if (v[0][2] <= 0 && v[1][2] <= 0 && v[2][2] <= 0)
            continue;
        if ((x[0] < -margin && x[1] < -margin && x[2] < -margin)
            || (x[0] >= 320 + margin && x[1] >= 320 + margin && x[2] >= 320 + margin)
            || (y[0] < 0 && y[1] < 0 && y[2] < 0)
            || (y[0] >= 224 && y[1] >= 224 && y[2] >= 224))
            continue;
        packet[0] = 0x06000000 | (head & 0xffffff);
        for (int j = 0; j < 3; ++j) {
            packet[1 + j * 2] = v[j][3];
            packet[2 + j * 2] = (unsigned short)x[j] | ((unsigned)y[j] << 16);
        }
        head = (head & 0xff000000) | (psx_to_int(unsigned, packet) & 0xffffff);
        packet += 7;
    }
    *ot = head;
    if (pc_gte_widescreen && pc_hw_anchor_packets && packet > first)
        pc_hw_anchor_packets(first, packet, PC_ANCHOR_SCENE);
    return packet;
}
void func_8009820C(void* mesh)
{
    psx_store(0x1f800000, pc_background_packets(mesh, psx_load(unsigned*, 0x1f800000),
        psx_load(unsigned*, 0x1f800004) + 2047, (int (*)[4])0x1f800100));
}

/* Per-group view matrices, including camera-facing room planes (flag 0x400). */
void func_800983F8(void* geometry)
{
    unsigned* scratch = (unsigned*)0x1f800000;
    unsigned count = *(unsigned*)geometry;
    unsigned char* group = (unsigned char*)geometry + 4;
    int angle = (int)scratch[0x5c / 4], scale = scratch[0x64 / 4];
    int sine = (int)((unsigned)rsin(angle) * (unsigned)scale) >> 12;
    int cosine = (int)((unsigned)rcos(angle) * (unsigned)scale) >> 12;
    if (count > 4096)
        pc_unimplemented("room group count");
    for (unsigned i = 0; i < count; ++i, group += 0x40) {
        unsigned flags = *(unsigned*)group;
        if (flags & 0x100)
            continue;
        MATRIX* result = (MATRIX*)(group + 0x20);
        if (!(flags & 0x400)) {
            func_800985AC((VECTOR*)(group + 4), (VECTOR*)(group + 0x10), result);
        } else {
            VECTOR zero = { 0, 0, 0, 0 };
            MATRIX camera;
            func_800985AC(&zero, &zero, &camera);
            SetRotMatrix(&camera);
            SetTransMatrix(&camera);
            MTC2(*(unsigned*)(group + 4) & 0xffff | (*(unsigned*)(group + 8) << 16), 0);
            MTC2(*(unsigned*)(group + 12), 1);
            gte_mvmva(1, 0, 0, 0, 0);
            /* Preserve the matrix entries the original does not overwrite. */
            result->m[0][0] = cosine;
            result->m[0][1] = -sine;
            result->m[1][0] = sine;
            result->m[1][1] = cosine;
            result->m[2][2] = scale;
            result->t[0] = MFC2(25);
            result->t[1] = MFC2(26);
            result->t[2] = MFC2(27);
            SetRotMatrix((MATRIX*)0x1f800014);
            SetTransMatrix((MATRIX*)0x1f800014);
        }
    }
}

/* 7BDD4.s: SQR consumes signed 16-bit IR components, even for int callers. */
int func_800E4660(int x, int y, int z)
{
    MTC2(x, 9);
    MTC2(y, 10);
    MTC2(z, 11);
    gte_sqr0();
    return MFC2(25) + MFC2(26) + MFC2(27);
}

/* Pair with disabled FntLoad/FntOpen above; this is Sony's debug font only. */
u_long* FntFlush(int id)
{
    (void)id;
    return 0;
}

/* Synchronous host counterpart: use the same VRAM-copy decoder as DR_MOVE. */
int MoveImage(RECT* rect, int x, int y)
{
    if (rect->w <= 0 || rect->h <= 0)
        return -1;
    unsigned packet[5] = { 0x04ffffff, 0x80000000,
        (unsigned short)rect->x | ((unsigned)(unsigned short)rect->y << 16),
        (unsigned short)x | ((unsigned)(unsigned short)y << 16),
        (unsigned short)rect->w | ((unsigned)(unsigned short)rect->h << 16) };
    pc_draw_packet(packet, 0);
    return 0;
}
int MoveImage2(RECT* rect, int x, int y) { return MoveImage(rect, x, y); }
