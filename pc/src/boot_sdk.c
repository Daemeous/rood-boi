#include <string.h>
#include <sys/types.h>
#include <libgte.h>
#include <inline_c.h>
#include "gte_value.h"
#include <libgpu.h>
#include <libetc.h>
#include <libpad.h>
extern void pc_log(const char*);
extern void pc_present(void);
extern void pc_vram_transfer(int, int, int, int, void*, int);
extern void pc_store_image_debug(int, int, int, int, const void*);
extern void pc_vram_clear(int, int, int, int, int, int, int);
extern void pc_vram_rect(int, int, int, int, int, int, int);
extern void pc_unimplemented(const char*);
extern void pc_tick(void);
#include "audio.h"
#include "view_state.h"
#include "widescreen.h"
#include "draw_items.h"
/* Weak so the standalone GTE/GPU probes link without the audio objects. */
#pragma weak pc_audio_set_tick
#pragma weak pc_audio_enable_tick
extern int vs_core_elapsedFrameCounterTicks(void);
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
/* libgpu's ClearImage (func_80029BE8 in SLUS_010.40) clips the size to
 * 1023 x 511, then uses the GPU fill only when x and w are both multiples of
 * 64 and draws a flat rectangle otherwise. Unclipped, the loading screen's
 * 1024 x 512 clear wraps to an empty fill and old VRAM shows behind
 * "NOW LOADING". */
int ClearImage(RECT* r, u_char red, u_char green, u_char blue)
{
    int w = r->w < 0 ? 0 : r->w > 1023 ? 1023 : r->w;
    int h = r->h < 0 ? 0 : r->h > 511 ? 511 : r->h;
    if ((r->x & 0x3f) || (w & 0x3f))
        pc_vram_rect(r->x, r->y, w, h, red, green, blue);
    else
        pc_vram_clear(r->x, r->y, w, h, red, green, blue);
    return 0;
}
int ClearImage2(RECT* r, u_char red, u_char green, u_char blue)
{
    return ClearImage(r, red, green, blue);
}
/* The classic layer's immediate-draw recorder (classic_parity.c, PC_CLASSIC_IMMEDIATE in classic_hook.h): inside an
 * open range the game's DrawPrim / LoadImage are recorded and, while a classic claim may draw instead, held back
 * (1: not done now); a StoreImage is recorded and done. Weak: absent outside the battle builds. */
extern int pc_classic_immediate_draw(const void* packet) __attribute__((weak));
extern int pc_classic_immediate_load(int x, int y, int w, int h, const void* pixels) __attribute__((weak));
extern void pc_classic_immediate_store(int x, int y, int w, int h) __attribute__((weak));
int LoadImage(RECT* r, u_long* p)
{
    if (pc_classic_immediate_load && pc_classic_immediate_load(r->x, r->y, r->w, r->h, p))
        return 0;
    pc_vram_transfer(r->x, r->y, r->w, r->h, p, 1);
    return 0;
}
/* Synchronous variant (waits for the GPU first); host transfers already are.
 * MENUE (Quick Manual) loads its pages with it. */
int LoadImage2(RECT* r, u_long* p) { return LoadImage(r, p); }
/* The game's two kinds of VRAM read (what StoreImage was, split by what is read): a transfer area, which the game's array holds exactly, and the drawn frame, which the renderer
 * reads back into it first. The three frame readers (TITLE's two title-picture rectangles, MENU5's map-background backup) call pc_frame_read; every other read is pc_vram_read. */
extern void pc_vram_read_area(int, int, int, int, void*);
int pc_vram_read(RECT* r, u_long* p)
{
    pc_store_image_debug(r->x, r->y, r->w, r->h, __builtin_return_address(0));
    if (pc_classic_immediate_store)
        pc_classic_immediate_store(r->x, r->y, r->w, r->h);
    pc_vram_read_area(r->x, r->y, r->w, r->h, p);
    return 0;
}
int pc_frame_read(RECT* r, u_long* p)
{
    pc_store_image_debug(r->x, r->y, r->w, r->h, __builtin_return_address(0));
    if (pc_classic_immediate_store)
        pc_classic_immediate_store(r->x, r->y, r->w, r->h);
    pc_vram_transfer(r->x, r->y, r->w, r->h, p, 0);
    return 0;
}
/* The SDK's StoreImage, for the lifted PS1 code that still calls it: the frame read (the read-back first), as always. */
int StoreImage(RECT* r, u_long* p) { return pc_frame_read(r, p); }
int DrawSync(int mode)
{
    (void)mode;
    return 0;
}
u_long DrawSyncCallback(void (*fn)(void))
{
    u_long old = (u_long)(__UINTPTR_TYPE__)draw_callback; /* a code address: the executable is below 4 GB */
    draw_callback = fn;
    return old;
}
static int ticks;
/* PadSetAct's actuator buffer (below), given to the gamepad's motors each vblank. */
static unsigned char* act0;
static int act0_size;
extern void pc_controller_rumble(int small, int large) __attribute__((weak)); /* boot_gamepad.c */
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
    if (act0 && act0_size >= 2 && pc_controller_rumble)
        pc_controller_rumble(act0[0], act0[1]);
    if (vsync_callback)
        vsync_callback();
}
int VSync(int mode)
{
    if (mode < 0)
        return ticks;
    int elapsed = vs_core_elapsedFrameCounterTicks();
    if (mode == 1)
        return elapsed; /* Original mode 1 queries elapsed scanlines without waiting. */
    int count = mode > 1 ? mode : 1;
    while (count--)
        pc_vblank_wait();
    return elapsed;
}
int VSyncCallback(void (*fn)(void))
{
    int old = (int)(__UINTPTR_TYPE__)vsync_callback; /* a code address: the executable is below 4 GB */
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
    /* A digital pad (4) whose actuators the game drives as a DualShock's (an extended ID: the small motor's
     * byte carries its on/off, not the Dual Analog's constant 0x40; PadSetAct). */
    if (port != 0 || !pc_window_enabled)
        return 0;
    return info == InfoModeCurID ? 4 : info == InfoModeCurExID ? 1 : 0;
}
int PadGetState(int port)
{
    return port == 0 && pc_window_enabled ? PadStateStable : PadStateDiscon;
}
/* The actuator buffer the game registered: the pad library reads it every field (the game rewrites it in
 * vs_main_setVibrateParams), so it is kept and handed to the gamepad's motors each vblank. */
void PadSetAct(int port, unsigned char* data, int size)
{
    if (port == 0)
        act0 = data, act0_size = size;
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
extern void pc_hwc_vram_fill(int x, int y, int w, int h, int r, int g, int b); /* hw_cmd.c */
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
    else if (pc_undrawn_debug)
        pc_hwc_vram_fill(e->clip.x, e->clip.y, e->clip.w, e->clip.h, 255, 0, 255);
    return e;
}
void DrawPrim(void* p)
{
    if (pc_classic_immediate_draw && pc_classic_immediate_draw(p))
        return;
    pc_draw_packet(p, 0);
}
/* Each drawn ordering table ends a frame for the GTE's projection log
 * (widescreen 2D/3D classification in the hardware rasterizer). */
extern void pc_gte_new_generation(void);
extern void pc_hwc_frame_room(void); /* hw_cmd.c */
extern void pc_hwc_begin_ot(void); /* hw_cmd.c */
extern void pc_hwc_end_ot(void);
/* The frame's table drawn: the item runs placed in the table `p` lies in, from `p` on. */
void pc_frame_table_draw(u_long* p)
{
    pc_hwc_frame_room(); /* (the room this frame is drawn in: emissive scopes) */
    pc_hwc_begin_ot();
    pc_draw_packet(p, 1);
    pc_hwc_end_ot();
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
    /* the draw mode (GP0 E1) for what draws after it, as an item at slot `after` of the ordering table */
    unsigned int* ot = PC_OT_BASE;
    unsigned int* cursor = PC_PACKET_CURSOR;
    pc_item* item = pc_frame_poly_at(after, PC_ITEM_MODE_SET, 0, 0, ot, &cursor);
    if (item)
        item->mode = (unsigned short)(0x200 | (page & 0x1ff));
    PC_PACKET_CURSOR = cursor;
}

/* An ordering table made for the frame (what ClearOTag and ClearOTagR made): registered (pc_frame_table: the runs placed in it are ordered by their
 * slots; `reverse` is a table drawn from its last slot) and empty: its entries hold no link, and a link found in one at the draw is a packet written
 * into it by hand (pc_frame_table_check). */
void pc_frame_table_clear(u_long* ot, int n, int reverse)
{
    if (n <= 0)
        pc_unimplemented("empty ordering table");
    pc_frame_table(ot, n, reverse);
    memset(ot, 0, (size_t)n * sizeof *ot);
}

/* pc/src/game/SLUS_010.40/32154.s: vs_core_mathNormalizeVector, shared by VectorNormal,
 * VectorNormalS and VectorNormalSS. Normalizes (x, y, z) in place to 4096
 * and returns the squared length. Keeps the original reciprocal-square-root
 * table and GTE register effects. */
extern short D_80040D98[192];
extern short D_80040C18[192];
extern u_char vs_core_mathArcTangentTable[516];
/* The square-root tables of 32154.s are 192 entries each (the argument is normalized to 64..255 and indexed minus 64), laid side by side:
 * the arctangent table (516 bytes), the root table D_80040C18, the reciprocal table D_80040D98. The original's index goes to -64 when the
 * normalized argument is 0 (a zero vector: normalize3 has no early out; screff2_warp's tiny radii shift by a masked count), and reads the
 * 64 entries before the table: the end of the table before it. Those reads are named here; any other index is refused and logged. */
int pc_root_table(int index)
{
    if (index >= 0 && index < 192)
        return (unsigned short)D_80040C18[index];
    if (index >= -64 && index < 0) {
        const u_char* tail = vs_core_mathArcTangentTable + 516 + 2 * index;
        return tail[0] | tail[1] << 8;
    }
    pc_table_index_ok("square root table", index, 192, "normalized argument", 0, 0);
    return 0;
}
int pc_reciprocal_table(int index)
{
    if (index >= 0 && index < 192)
        return D_80040D98[index];
    if (index >= -64 && index < 0)
        return (unsigned short)D_80040C18[192 + index];
    pc_table_index_ok("reciprocal table", index, 192, "normalized argument", 0, 0);
    return 0;
}
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
    int reciprocal = pc_reciprocal_table((int)scaled - 64);
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
MATRIX* vs_core_mathMultiplyMatrixIntoRight(MATRIX* a, MATRIX* b) { return compose_rotation(a, b, b); }
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

MATRIX* vs_core_mathBuildEulerRotationMatrixQuadrant(SVECTOR* angles, MATRIX* result);

MATRIX* vs_core_mathPremultiplyRotationMatrix(MATRIX* a, MATRIX* b);

MATRIX* vs_core_mathScaleMatrixColumns(MATRIX* matrix, VECTOR* scale);


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
    unsigned root = (unsigned)pc_root_table((int)scaled - 64);
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
    unsigned root = (unsigned)pc_root_table((int)scaled - 64);
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
 * Translation is the camera's rotation of the position plus `translation` (the GTE's TR registers in the original:
 * the frame view's, passed), not camera->t. */
void pc_room_transform(VECTOR* position, VECTOR* angles, MATRIX* camera, MATRIX* result, const int translation[3])
{
    SVECTOR rotation = { angles->vx, angles->vy, angles->vz, 0 };
    RotMatrixYXZ_gte(&rotation, result);
    compose_rotation(camera, result, result);
    vs_gte_view view = vs_gte_view_of(camera, VS_GTE_NO_SCREEN);
    view.t[0] = translation[0];
    view.t[1] = translation[1];
    view.t[2] = translation[2];
    SVECTOR vector = { (short)position->vx, (short)position->vy, (short)position->vz, 0 };
    vs_gte_result r = vs_gte_transform(&view, &vector);
    result->t[0] = r.mac[1];
    result->t[1] = r.mac[2];
    result->t[2] = r.mac[3];
}
void vs_render_buildRoomGeometryViewMatrix(VECTOR* position, VECTOR* angles, MATRIX* result); /* with the frame view's translation (room_transform.zig) */


/* 2EA3C.s 98648: inverse far-clip scale precedes camera/local rotation.
 * Keep the original signed-halfword scale and GTE translation registers. */
void pc_scaled_room_transform(
    VECTOR* position, VECTOR* angles, MATRIX* camera, int far_clip, MATRIX* result, const int translation[3])
{
    MATRIX scale = { 0 }, scaled_camera = *camera;
    /* `div $zero` has no zero check: the R3000 leaves LO = -1 for the
     * positive dividend. */
    short factor = far_clip ? 0x1000000 / far_clip : -1;
    scale.m[0][0] = scale.m[1][1] = scale.m[2][2] = factor;
    compose_rotation(&scale, &scaled_camera, &scaled_camera);
    pc_room_transform(position, angles, &scaled_camera, result, translation);
}
void vs_render_buildScaleCompensatedRoomView(VECTOR* position, VECTOR* angles, MATRIX* result);


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
    unsigned* mesh, unsigned* packet, unsigned* ot, int transformed[34][4], const vs_gte_view* camera)
{
    vs_gte_view view = *camera;
    int margin = PC_WIDESCREEN_MARGIN;
    /* The sphere is about the eye: vs_field_drawRoomBackground places it at the game
     * camera's position through the view matrix, which leaves no
     * translation for the game's camera. The free camera's view matrix is
     * built from its own eye (vs_render_buildCameraViewMatrix's hook), so that left the sky
     * offset by the two eyes' distance, off the screen (black where the sky
     * belongs): about the free camera's eye instead, as the game's is. */
    if (pc_live_camera_active && pc_live_camera_active())
        view.t[0] = view.t[1] = view.t[2] = 0;
    for (int i = 0; i < 34; ++i) {
        SVECTOR vector = { (short)mesh[0], (short)(mesh[0] >> 16), (short)mesh[1], 0 };
        vs_gte_result r = vs_gte_transform(&view, &vector);
        transformed[i][0] = r.mac[1];
        transformed[i][1] = r.mac[2];
        transformed[i][2] = r.mac[3];
        transformed[i][3] = mesh[2];
        mesh += 3;
    }
    /* The triangles as draw items in one run, placed at the table entry by a marker (a later one draws first, as a packet
     * linked at the head did). */
    int first_item = pc_frame_count();
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
        pc_item* item = pc_frame_add(PC_ITEM_POLY, 0);
        if (!item)
            break;
        item->verts = 3, item->flags = PC_ITEM_SHADED, item->mode = PC_ITEM_INHERIT;
        for (int j = 0; j < 3; ++j) {
            item->x[j] = (short)x[j], item->y[j] = (short)y[j];
            item->rgb[j][0] = (unsigned char)v[j][3], item->rgb[j][1] = (unsigned char)(v[j][3] >> 8),
            item->rgb[j][2] = (unsigned char)(v[j][3] >> 16);
        }
    }
    int count = pc_frame_count() - first_item;
    if (count > 0) {
        packet = pc_frame_place(first_item, count, ot, packet);
        if (pc_gte_widescreen)
            pc_frame_anchor(first_item, first_item + count, PC_ANCHOR_SCENE);
    }
    return packet;
}
void vs_render_drawSkyDome(void* mesh, const vs_gte_view* camera);

void vs_render_updateRoomGroupTransforms(void* geometry);

int vs_core_mathSquaredVectorLength(int x, int y, int z);


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
    unsigned packet[5] = { 0x04ffffff, 0x80u << 24, /* GPU command 0x80: copy rectangle VRAM to VRAM */
        (unsigned short)rect->x | ((unsigned)(unsigned short)rect->y << 16),
        (unsigned short)x | ((unsigned)(unsigned short)y << 16),
        (unsigned short)rect->w | ((unsigned)(unsigned short)rect->h << 16) };
    pc_draw_packet(packet, 0);
    return 0;
}
int MoveImage2(RECT* rect, int x, int y) { return MoveImage(rect, x, y); }
