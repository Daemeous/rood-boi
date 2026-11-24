/* The game side's calls into the renderer, as requests (hw_calls.h). Host state: nothing here is game state. */
#include "hw_calls.h"
#include "hw_cmd.h"
#include "hw_registry.h"
#include "render_thread.h"
#include <stdlib.h>
#include <string.h>

/* The renderer's own functions (gpu_hw.c). */
extern int pc_hw_dump_now(const char*, int);
extern int pc_hw_redraw_now(int*);
extern int pc_hw_redraw_present_now(void);
extern int pc_hw_set_scale_now(int);
extern void pc_hw_text_overlay_stats_now(long*, long*, long*);
extern void pc_hw_display_rect_now(int*);
extern int pc_hw_pick_now(float, float, float*, float*);
extern float pc_hw_native_per_output_now(void);
extern int pc_hw_present_all_lists_now(uint32_t*, int);
extern int pc_hw_present_lists_now(uint32_t*, int, float*);
extern unsigned pc_hw_native_texture_now(void);
extern const float* pc_hw_casters_now(int*, int*);
extern const void* pc_hw_caster_surfaces_now(void);
extern size_t pc_hw_caster_surface_size(void);
extern int pc_hw_photo_prepare_now(void);
extern void pc_hw_photo_drop_now(void);
extern int pc_hw_photo_mark_now(void);
extern int pc_hw_photo_restore_now(void);
extern void pc_hw_forget_timeline_now(void);
extern void pc_hw_supersample_now(int, int);
extern void pc_hw_detail_view_now(int);
extern int pc_hw_present_world_now(void);
extern int pc_hw_present_menu_model_now(void);
extern const uint16_t* pc_hw_renderer_vram_now(void);
extern void pc_hw_vram_copy_now(uint16_t* dst, pc_tex_versions* versions);
extern pc_tex_view pc_hw_emissive_tex_view_now(void);
extern const uint16_t* pc_hw_emissive_vram_now(void);
extern int pc_hw_active;
extern const uint16_t* pc_native_vram(void);
extern void pc_hw_init_now(const uint16_t*);
extern int pc_hw_view_camera_now(float[3][3], float[3], float*, float*, float*);
extern int pc_hw_replay_camera_now(float[3][3], float[3]);
extern int pc_hw_replay_projection_now(float*, float*, float*);
extern void pc_hw_stale_get(pc_stale_set* out);
extern void pc_ui_bubbles_forget_pending(void) __attribute__((weak));

/* Every record this thread made is applied first, then fn runs where the renderer lives. */
static void call(void (*fn)(void*), void* arg)
{
    if (pc_rt_on_thread()) {
        fn(arg);
        return;
    }
    pc_hwc_flush();
    pc_rt_call(fn, arg);
}
typedef struct {
    const char* path;
    int replay, result;
} dump_args;
static void dump_call(void* p) { dump_args* a = p; a->result = pc_hw_dump_now(a->path, a->replay); }
int pc_hw_dump(const char* path, int replay_target)
{
    dump_args a = { path, replay_target, 0 };
    call(dump_call, &a);
    return a.result;
}
typedef struct {
    int* area;
    int result;
} area_args;
static void redraw_call(void* p) { area_args* a = p; a->result = pc_hw_redraw_now(a->area); }
int pc_hw_redraw(int area[4])
{
    area_args a = { area, 0 };
    call(redraw_call, &a);
    return a.result;
}
typedef struct {
    int value, result;
} int_args;
static void redraw_present_call(void* p) { ((int_args*)p)->result = pc_hw_redraw_present_now(); }
int pc_hw_redraw_present(void)
{
    int_args a = { 0, 0 };
    call(redraw_present_call, &a);
    return a.result;
}
static void set_scale_call(void* p) { int_args* a = p; a->result = pc_hw_set_scale_now(a->value); }
int pc_hw_set_scale(int scale)
{
    int_args a = { scale, 0 };
    call(set_scale_call, &a);
    return a.result;
}
typedef struct {
    long *held, *baked, *presented;
} overlay_args;
static void overlay_call(void* p) { overlay_args* a = p; pc_hw_text_overlay_stats_now(a->held, a->baked, a->presented); }
void pc_hw_text_overlay_stats(long* held, long* baked, long* presented)
{
    overlay_args a = { held, baked, presented };
    call(overlay_call, &a);
}
static void display_call(void* p) { pc_hw_display_rect_now(p); }
void pc_hw_display_rect(int r[4]) { call(display_call, r); }
typedef struct {
    float x, y, *sz, *normal;
    int result;
} pick_args;
static void pick_call(void* p) { pick_args* a = p; a->result = pc_hw_pick_now(a->x, a->y, a->sz, a->normal); }
int pc_hw_pick(float vram_x, float vram_y, float* sz, float normal[3])
{
    pick_args a = { vram_x, vram_y, sz, normal, 0 };
    call(pick_call, &a);
    return a.result;
}
typedef struct {
    float result;
} float_args;
static void per_output_call(void* p) { ((float_args*)p)->result = pc_hw_native_per_output_now(); }
float pc_hw_native_per_output(void)
{
    float_args a = { 0 };
    call(per_output_call, &a);
    return a.result;
}
typedef struct {
    uint32_t* lists;
    int max, result;
    float* t;
} lists_args;
static void all_lists_call(void* p) { lists_args* a = p; a->result = pc_hw_present_all_lists_now(a->lists, a->max); }
int pc_hw_present_all_lists(uint32_t* lists, int max)
{
    lists_args a = { lists, max, 0, NULL };
    call(all_lists_call, &a);
    return a.result;
}
static void lists_call(void* p) { lists_args* a = p; a->result = pc_hw_present_lists_now(a->lists, a->max, a->t); }
int pc_hw_present_lists(uint32_t* lists, int max, float* t)
{
    lists_args a = { lists, max, 0, t };
    call(lists_call, &a);
    return a.result;
}

/* The native VRAM texture's handle (the editors' and the path tracers' texturing): fully up to date when the request runs. */
static void texture_call(void* p) { *(unsigned*)p = pc_hw_native_texture_now(); }
unsigned pc_hw_native_texture(void)
{
    unsigned result = 0;
    call(texture_call, &result);
    return result;
}

/* The renderer's start (the presenter's context just made): its own state is built on its thread. */
static void init_call(void* p) { pc_hw_init_now(p); }
void pc_hw_init(void) { call(init_call, (void*)pc_native_vram()); } /* (the game's array, copied into the renderer's at its start) */

/* The frame state the presenter reads (whether a 3D picture / the menu's model view is shown) and the detail view's supersampling: requests. */
static void present_world_call(void* p) { ((int_args*)p)->result = pc_hw_present_world_now(); }
int pc_hw_present_world(void)
{
    int_args a = { 0, 0 };
    call(present_world_call, &a);
    return a.result;
}
static void present_menu_model_call(void* p) { ((int_args*)p)->result = pc_hw_present_menu_model_now(); }
int pc_hw_present_menu_model(void)
{
    int_args a = { 0, 0 };
    call(present_menu_model_call, &a);
    return a.result;
}
static void detail_view_call(void* p) { pc_hw_detail_view_now(((int_args*)p)->value); }
void pc_hw_detail_view(int on)
{
    int_args a = { on, 0 };
    call(detail_view_call, &a);
}
static void supersample_call(void* p) { int* v = p; pc_hw_supersample_now(v[0], v[1]); }
void pc_hw_supersample(int view_h, int display_h)
{
    int v[2] = { view_h, display_h };
    call(supersample_call, v);
}

/* The renderer's VRAM, for the readers on the game thread (lighting's environment, the emitters' texels, emissive, the RPC's prompt tools). The array
 * is the renderer thread's (its records write it as they apply), so the game thread reads a copy that a request made after every record the caller
 * had made was applied: the same VRAM in every mode (a copy published at the renderer's frame boundary would be the VRAM of whichever frame the
 * thread had reached). The copy stays right until something more is posted (pc_rt_pushed), so the many reads of one moment are one copy. On the
 * renderer's own thread, or with the thread off, the array itself. */
static uint16_t vram_copy[1024 * 512];
static pc_tex_versions vram_copy_versions; /* (the versions of the cells, as of the same request) */
static unsigned long long vram_key = ~0ull;
static void vram_copy_call(void* p) { pc_hw_vram_copy_now(p, &vram_copy_versions); }
static const uint16_t* game_side_vram(void)
{
    pc_hwc_flush();
    if (vram_key != pc_rt_pushed()) {
        pc_rt_call(vram_copy_call, vram_copy);
        vram_key = pc_rt_pushed();
    }
    return vram_copy;
}
const uint16_t* pc_hw_renderer_vram(void)
{
    if (pc_rt_on_thread() || !pc_rt_active())
        return pc_hw_renderer_vram_now();
    return game_side_vram();
}
/* The same with its versions (tex_provider.h): where the game side copies the array, it copies the versions of the same request. */
pc_tex_view pc_hw_emissive_tex_view(int renderer_side)
{
    if (renderer_side || pc_rt_on_thread() || !pc_rt_active()) {
        if (!renderer_side)
            pc_hwc_flush();
        return pc_hw_emissive_tex_view_now();
    }
    pc_tex_view view = { pc_hw_active ? game_side_vram() : pc_native_vram(), pc_hw_active ? &vram_copy_versions : NULL };
    return view;
}
/* emissive.cpp's VRAM (T1 R4d): the renderer's copy, current (the commands pending are applied); renderer_side: the caller is the renderer's. */
const uint16_t* pc_hw_emissive_vram(int renderer_side) { return pc_hw_emissive_tex_view(renderer_side).image; }

/* The picture's camera (what the presented picture's surfaces were drawn with, or the interpolated frame's): asked for per query by the lights,
 * the editors and the pickers. The renderer answers after everything posted before; the answer holds until something more is posted, so the
 * calls of one moment are one request. (Off, or not started: each is a direct call, nothing is posted.) */
typedef struct {
    float r[3][3], t[3], h, ofx, ofy;
    int result;
} camera_args;
static camera_args view_cache, replay_cache, projection_cache;
static unsigned long long view_key = ~0ull, replay_key = ~0ull, projection_key = ~0ull;
static void view_camera_call(void* p) { camera_args* a = p; a->result = pc_hw_view_camera_now(a->r, a->t, &a->h, &a->ofx, &a->ofy); }
static void replay_camera_call(void* p) { camera_args* a = p; a->result = pc_hw_replay_camera_now(a->r, a->t); }
static void replay_projection_call(void* p) { camera_args* a = p; a->result = pc_hw_replay_projection_now(&a->h, &a->ofx, &a->ofy); }
static void ask_camera(void (*fn)(void*), camera_args* cache, unsigned long long* key, int id)
{
    if (pc_rt_on_thread() || !pc_rt_active()) {
        fn(cache);
        *key = ~0ull;
        return;
    }
    pc_hwc_flush();
    if (*key == pc_rt_pushed())
        return;
    pc_rt_call(fn, cache);
    *key = pc_rt_pushed();
    (void)id;
}
int pc_hw_view_camera(float r[3][3], float t[3], float* h, float* ofx, float* ofy)
{
    ask_camera(view_camera_call, &view_cache, &view_key, 0);
    if (view_cache.result)
        memcpy(r, view_cache.r, sizeof view_cache.r), memcpy(t, view_cache.t, sizeof view_cache.t), *h = view_cache.h, *ofx = view_cache.ofx, *ofy = view_cache.ofy;
    return view_cache.result;
}
int pc_hw_replay_camera(float r[3][3], float t[3])
{
    ask_camera(replay_camera_call, &replay_cache, &replay_key, 1);
    if (replay_cache.result)
        memcpy(r, replay_cache.r, sizeof replay_cache.r), memcpy(t, replay_cache.t, sizeof replay_cache.t);
    return replay_cache.result;
}
int pc_hw_replay_projection(float* h, float* ofx, float* ofy)
{
    ask_camera(replay_projection_call, &projection_cache, &projection_key, 2);
    if (projection_cache.result)
        *h = projection_cache.h, *ofx = projection_cache.ofx, *ofy = projection_cache.ofy;
    return projection_cache.result;
}

/* The casters: copies on this side, refreshed when the renderer's serial moved. */
enum { CASTER_MAX = 32768 };
static float caster_copy[CASTER_MAX * 9];
static unsigned char* surface_copy;
static int caster_count_copy, caster_serial_copy = -1;
typedef struct {
    int count, serial;
} caster_args;
static void casters_call(void* p)
{
    caster_args* a = p;
    int count, serial;
    const float* now = pc_hw_casters_now(&count, &serial);
    if (serial != caster_serial_copy) {
        size_t size = pc_hw_caster_surface_size();
        if (!surface_copy)
            surface_copy = malloc(size * CASTER_MAX);
        if (count > CASTER_MAX)
            count = CASTER_MAX;
        memcpy(caster_copy, now, (size_t)count * 9 * sizeof(float));
        if (surface_copy)
            memcpy(surface_copy, pc_hw_caster_surfaces_now(), (size_t)count * size);
        caster_count_copy = count, caster_serial_copy = serial;
    }
    a->count = caster_count_copy, a->serial = caster_serial_copy;
}
const float* pc_hw_casters(int* count, int* serial)
{
    caster_args a;
    call(casters_call, &a);
    *count = a.count, *serial = a.serial;
    return caster_copy;
}
const void* pc_hw_caster_surfaces(void) { return surface_copy; }

/* The photo mode's hooks change the renderer's stale areas (a restore brings back the picture's): the reply hands the producer's copy the set. */
typedef struct {
    int result, op;
    pc_stale_set stale;
} photo_args;
static void photo_call(void* p)
{
    photo_args* a = p;
    switch (a->op) {
    case 0: a->result = pc_hw_photo_prepare_now(); break;
    case 1: pc_hw_photo_drop_now(); break;
    case 2: a->result = pc_hw_photo_mark_now(); break;
    case 3: a->result = pc_hw_photo_restore_now(); break;
    }
    pc_hw_stale_get(&a->stale);
}
static int photo(int op)
{
    photo_args a = { 0, op, { { { 0 } } } };
    call(photo_call, &a);
    pc_hwr_stale = a.stale;
    return a.result;
}
int pc_hw_photo_prepare(void) { return photo(0); }
void pc_hw_photo_drop(void) { photo(1); }
int pc_hw_photo_mark(void) { return photo(2); }
int pc_hw_photo_restore(void) { return photo(3); }

/* A restored timeline forgets the registrations the abandoned one made (the producer's: here) and the text the renderer held back. */
static void forget_call(void* p) { (void)p; pc_hw_forget_timeline_now(); }
void pc_hw_forget_timeline(void)
{
    pc_hwr_forget_pending();
    if (pc_ui_bubbles_forget_pending)
        pc_ui_bubbles_forget_pending();
    call(forget_call, NULL);
}
