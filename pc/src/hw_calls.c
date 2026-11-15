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
