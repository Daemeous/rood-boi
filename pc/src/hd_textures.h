/* Beetle PSX HW upload-rectangle PNG packs. Native-only; originals stay in VRAM. */
#ifndef ROOD_HD_TEXTURES_H
#define ROOD_HD_TEXTURES_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
extern int pc_hd_enabled;
void pc_hd_upload(int x, int y, int w, int h, const uint16_t* vram);
void pc_hd_clear(int x, int y, int w, int h);
void pc_hd_copy(int sx, int sy, int dx, int dy, int w, int h, int masked);
void pc_hd_use(unsigned tpage, unsigned clut);
/* Sync before rendering, outside a Vulkan render pass. GL binds units 8..11. */
void pc_hd_gl_bind(unsigned program);
#ifdef ROOD_VULKAN
void pc_hd_vk_sync(void);
void* pc_hd_vk_view(int which);
#endif
void pc_hd_frame(void);
void pc_hd_status(char* out, int size);
#ifdef __cplusplus
}
#endif
#endif
