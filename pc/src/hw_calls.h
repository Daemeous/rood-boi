/* The game side's calls into the renderer, as requests (T1 R4e): each keeps the name it always had, runs the renderer's own function (its _now
 * form in gpu_hw.c) on the renderer's thread after every record the caller made is applied, and returns when it has run. Called on the renderer's
 * own thread (the renderer's internal uses) they just run. */
#ifndef PC_HW_CALLS_H
#define PC_HW_CALLS_H
#include <stddef.h>
#include <stdint.h>
#include "tex_provider.h"
#ifdef __cplusplus
extern "C" {
#endif
void pc_hw_init(void);
pc_tex_view pc_hw_emissive_tex_view(int renderer_side); /* emissive.cpp: the renderer's copy with its versions (none: no versions to key on) */
int pc_hw_dump(const char* path, int replay_target);
int pc_hw_redraw(int area[4]);
int pc_hw_redraw_present(void);
int pc_hw_set_scale(int scale);
void pc_hw_text_overlay_stats(long* held, long* baked, long* presented);
void pc_hw_display_rect(int r[4]);
int pc_hw_pick(float vram_x, float vram_y, float* sz, float normal[3]);
float pc_hw_native_per_output(void);
int pc_hw_present_all_lists(uint32_t* lists, int max);
int pc_hw_present_lists(uint32_t* lists, int max, float* t);
/* The shadow casters' world triangles and their surfaces as of the renderer's last table: copies (refreshed when the serial changed), the editors read
 * them without touching the renderer's arrays. */
const float* pc_hw_casters(int* count, int* serial);
const void* pc_hw_caster_surfaces(void);
unsigned pc_hw_native_texture(void); /* GLuint: the native VRAM as an R16UI texture, fully up to date */
const uint16_t* pc_hw_renderer_vram(void); /* the renderer's VRAM: on the game thread a copy as of every record made so far */
const uint16_t* pc_hw_emissive_vram(int renderer_side);
int pc_hw_present_world(void);
int pc_hw_present_menu_model(void);
void pc_hw_detail_view(int on);
void pc_hw_supersample(int view_h, int display_h);
int pc_hw_view_camera(float r[3][3], float t[3], float* h, float* ofx, float* ofy);
int pc_hw_replay_camera(float r[3][3], float t[3]);
int pc_hw_replay_projection(float* h, float* ofx, float* ofy);
int pc_hw_photo_prepare(void);
void pc_hw_photo_drop(void);
int pc_hw_photo_mark(void);
int pc_hw_photo_restore(void);
void pc_hw_forget_timeline(void);
#ifdef __cplusplus
}
#endif
#endif
