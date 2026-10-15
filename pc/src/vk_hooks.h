/* Hooks from the OpenGL path's files (gpu_hw.c, render.c, boot_window.c,
 * dev_overlay.cpp) into the Vulkan backend (ROOD_RENDERER=vulkan, built with
 * -DROOD_VULKAN by build_boot.py --arch 64; pc/docs/Vulkan_Renderer.md).
 *
 * gpu_hw.c keeps all of its logic (packet batching, native ranges, frame
 * recording and interpolation, statistics) and only hands its GL calls to
 * vk_hw.c when Vulkan is active. Its GL object names become tokens that
 * name the Vulkan targets, so the logic that passes them around is
 * unchanged. */
#ifndef ROOD_VK_HOOKS_H
#define ROOD_VK_HOOKS_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int pc_vk_active;
static inline int pc_vk_on(void) { return pc_vk_active; }

/* Tokens standing for gpu_hw.c's GL framebuffers and textures. */
enum {
    VK_TOKEN_LIVE = 1, /* target_fbo / target_texture: the live target (colour, G-buffer, depth) */
    VK_TOKEN_REPLAY = 2, /* replay.fbo / replay.color: the interpolated frame's target */
    VK_TOKEN_ROOM_DEPTH = 3, /* room_depth_fbo / room_depth_texture: the native surfaces' depth */
    VK_TOKEN_BEFORE = 4, /* before_fbo / before_texture: room depth before an object list */
    VK_TOKEN_OBJECT = 5, /* object_fbo: live colours + room depth */
    VK_TOKEN_OBJECT_REPLAY = 6, /* object_replay_fbo: replay colours + room depth */
    VK_TOKEN_VRAM = 7, /* native_texture: native VRAM (R16_UINT) */
    /* MSAA (antialias.h): the multisampled twins gpu_hw.c draws 3D into. */
    VK_TOKEN_MS = 8, /* multisampled colours, G-buffer and depth */
    VK_TOKEN_MS_OBJECT = 9, /* multisampled colours + the multisampled native surfaces' depth */
    VK_TOKEN_MS_ROOM_DEPTH = 10, /* the multisampled native surfaces' depth */
    VK_TOKEN_MS_BEFORE = 11, /* its copy from before an object list */
};

/* ---- gpu_hw.c -> vk_hw.c ---- */
int pc_vk_hw_init(int scale, size_t vertex_size);
void pc_vk_hw_resize(int scale);
void pc_vk_hw_upload_tiles(const uint16_t* vram, uint32_t tiles);
/* Emissive texels' slot map (emissive.h): rows [row0, row1) of the CPU copy. */
void pc_vk_hw_upload_emissive(const uint8_t* map, int row0, int row1);
void pc_vk_hw_draw_batch(unsigned fbo, const void* vertices, int count, int blend, const int* clip, int occlusion,
    const int* occlusion_rect);
void pc_vk_hw_fill(unsigned fbo, int x, int y, int w, int h, int r, int g, int b);
void pc_vk_hw_upload_rect(int x, int y, int w, int h);
void pc_vk_hw_copy(int sx, int sy, int dx, int dy, int w, int h);
/* Depth rectangle (scaled pixels, x1/y1 exclusive) between depth tokens. */
void pc_vk_hw_copy_depth(unsigned from, unsigned to, int x0, int y0, int x1, int y1);
/* A depth token's rectangle (scaled pixels, x1/y1 exclusive) to far (1). */
void pc_vk_hw_clear_depth(unsigned token, int x0, int y0, int x1, int y1);
/* The live target's colours and depth over a rectangle (scaled pixels) into
 * the replay target. */
void pc_vk_hw_replay_copy(int x0, int y0, int x1, int y1);
int pc_vk_hw_dump(const char* path, int replay);
/* The text overlay (gpu_hw.c pc_hw_text_overlay): `count` vertices of
 * distance-texel text, PS1 mode `mode` (4: opaque), over the presented
 * picture in the rendering open now: the view at (view_x, view_y) of the
 * window image, or of the brightness input (to_view, 16-bit float, from 0,
 * 0); rect: the presented VRAM area; clip: the drawing area in pixels from
 * the view's top-left (x0, y0, x1, y1). */
void pc_vk_hw_text_overlay(const void* vertices, int count, int mode, const float* rect, const float* clip, int view_x,
    int view_y, int to_view);
/* Normal (RGBA8) and depth (0..1) of the live target at scaled pixel x, y. */
int pc_vk_hw_pick(int x, int y, float* depth, unsigned char normal[4]);
/* The live target over native rectangle x0..x1, y0..y1 at native resolution
 * (RGBA8, tightly packed), for VRAM readers (gpu_hw.c pc_hw_sync). */
int pc_vk_hw_sync_read(int x0, int y0, int x1, int y1, unsigned char* pixels);
/* MSAA (rectangles in scaled pixels, x1/y1 exclusive): the most samples the
 * device runs (0: none); make the twins (`samples`, covering width x height
 * from (0, 0)); load the live or replay target (`fbo`) into them (`room`:
 * the native surfaces' depth too); resolve them back into it; and copy the
 * multisampled surfaces' depth into the single-sampled one (sample 0). */
int pc_vk_hw_ms_max(void);
int pc_vk_hw_ms_make(int samples, int width, int height);
void pc_vk_hw_ms_load(unsigned fbo, int x0, int y0, int x1, int y1, int room);
void pc_vk_hw_ms_resolve(unsigned fbo, int x0, int y0, int x1, int y1);
void pc_vk_hw_ms_sync_room(int x0, int y0, int x1, int y1);

/* ---- render.c -> vk_render.inc ---- */
uint32_t pc_vk_mesh_create(const void* vertices, size_t bytes);
void pc_vk_mesh_destroy(uint32_t mesh);

/* ---- boot_window.c -> vk_present.c ---- */
struct SDL_Window;
struct SDL_Window* pc_vk_create_window(const char* title, int width, int height);
void pc_vk_resize(int width, int height);
void pc_vk_present(const uint16_t* vram, int x, int y, int w, int h);

/* ---- dev_overlay.cpp -> vk_overlay.cpp ---- */
int pc_vk_overlay_init(void);
void pc_vk_overlay_new_frame(void);

#ifdef __cplusplus
}
#endif
#endif
