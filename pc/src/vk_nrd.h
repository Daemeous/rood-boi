/* NVIDIA NRD (RELAX diffuse) for the Vulkan path tracer's indirect light
 * (pc/docs/Vulkan_Renderer.md, "NRD"). NRD is fetched and built at build
 * time (pc/tools/nrd.py, pinned; never vendored); without it (ROOD_NRD
 * undefined) these report "unavailable" and the tracer keeps its own
 * filter. Everything stays on the GPU: NRD's dispatches are recorded into
 * the frame's command buffer (vk_cmd()), its images are vk_image's in
 * GENERAL layout on the same device. */
#ifndef ROOD_VK_NRD_H
#define ROOD_VK_NRD_H
#include "vk_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int width, height; /* the GI images */
    /* Inputs (sampled) and output (storage), GENERAL layout:
     * normal_roughness: NRD_FrontEnd_PackNormalAndRoughness (R10G10B10A2 encoding),
     * view_z: linear view depth (> denoising_range: no surface),
     * motion: world-space motion, previous - current,
     * radiance: noisy diffuse radiance + hit distance,
     * output: denoised radiance (.w history length). */
    VkImageView normal_roughness, view_z, motion, radiance, output;
    /* Column-major, non-jittered; previous = this frame's on a reset. */
    float world_to_view[16], view_to_clip[16], world_to_view_prev[16], view_to_clip_prev[16];
    float denoising_range;
    unsigned frame_index;
    int reset; /* history cleared (camera cut, room, lights changed, resize) */
    /* RELAX diffuse: maximum accumulated frames (main and fast history). */
    int max_frames, max_fast_frames;
} vk_nrd_frame;

/* Built with NRD (the library is linked)? */
int vk_nrd_built(void);
/* Built and usable on this device (creates nothing; the instance is made
 * on the first vk_nrd_denoise). */
int vk_nrd_available(void);
/* Record RELAX diffuse for one frame; 0 on failure (NRD then stays off). */
int vk_nrd_denoise(const vk_nrd_frame* frame);
/* "NRD 4.18.0" or "" */
const char* vk_nrd_version(void);
void vk_nrd_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif
