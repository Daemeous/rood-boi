/* 2D / UI texture magnification with MMPX (ui_upscale.h).
 *
 * MMPX: Morgan McGuire and Mara Gagiu, "MMPX Style-Preserving Pixel Art
 * Magnification", Journal of Computer Graphics Techniques 10(2), 2021. The
 * kernel (pc_ui_mmpx2x here, pc/shaders/vulkan/ui_mmpx.comp on the GPU) is a
 * port of the authors' reference implementation, MIT licence, Copyright 2020
 * Morgan McGuire & Mara Gagiu (THIRD_PARTY.md). MMPX magnifies by 2;
 * 4x and 8x run it twice and three times, as the paper does for 4x.
 *
 * Nothing is shipped: every texture is the game's own VRAM, decoded through
 * its CLUT when a 2D primitive samples it, as PS1 colours (5:5:5 + the
 * semi-transparency bit, 0 = the transparent colour key). MMPX only ever
 * copies input texels, so the palette, the colour key and the STP bit (and
 * so the PS1's opaque / translucent texel passes) are exactly the original's.
 *
 * Cache. A region (page, CLUT, texel rectangle) is hashed by content; an
 * entry per distinct content holds its place in the atlas. A location table
 * remembers each region's last content and the VRAM tile generations it was
 * read at, so regions in tiles nothing wrote since are hits without hashing.
 * New content is magnified before the batch that samples it draws (GPU:
 * upload of the decoded source texels, then one MMPX pass per factor of 2,
 * one dispatch per entry; CPU: the same kernel, result uploaded). Each
 * entry is magnified clamped to its own rectangle, so sprites packed next to
 * each other in a page never bleed into one another. When the atlas is
 * full everything is dropped and rebuilt from what is drawn next.
 *
 * Not UI: regions inside a drawing area used this frame or the last
 * (render-to-texture: screen effects sample the framebuffer) and regions
 * whose content changes on most frames (bypassed for a while) stay nearest. */
#define _POSIX_C_SOURCE 200809L
#include "gl_api.h"
#include "ui_upscale.h"
#include "ui_text.h" /* ui_text font: glyph regions drawn from a font instead of magnified */
#include "button_prompts.h" /* button pictographs and sprites drawn from icon fonts */
#include "ui_bubbles.h" /* emote bubbles drawn as shapes and fonts (ui_emotes.c) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "gl_shaders.h" /* pc_glsl_ui_mmpx (pc/shaders/vulkan/ui_mmpx.comp) */
#ifdef ROOD_VULKAN
#include "vk_core.h"
#include "vk_hooks.h"
#include "vk_shaders.h"
#endif

int pc_ui_upscale = PC_UI_NEAREST;
int pc_ui_subpixel = 0;
const char* const pc_ui_upscale_names[2] = { "nearest", "mmpx" };

#define MAX_LEVELS 3 /* 8x */
static int atlas_w, atlas_h, atlas_factor, atlas_levels; /* source texel space; factor = 1 << levels */
static uint16_t* shadow;                                  /* atlas_w x atlas_h source texels (verify, CPU path) */

/* ---- the MMPX kernel (CPU reference and fallback) ---- */

static inline uint32_t luma(uint32_t c)
{
    uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    uint32_t sum = ((r << 3) | (r >> 2)) + ((g << 3) | (g >> 2)) + ((b << 3) | (b >> 2));
    return (sum + 1) * (c == 0 ? 256u : 1u);
}
static inline int all_eq2(uint32_t B, uint32_t A0, uint32_t A1) { return ((B ^ A0) | (B ^ A1)) == 0; }
static inline int all_eq3(uint32_t B, uint32_t A0, uint32_t A1, uint32_t A2)
{
    return ((B ^ A0) | (B ^ A1) | (B ^ A2)) == 0;
}
static inline int all_eq4(uint32_t B, uint32_t A0, uint32_t A1, uint32_t A2, uint32_t A3)
{
    return ((B ^ A0) | (B ^ A1) | (B ^ A2) | (B ^ A3)) == 0;
}
static inline int any_eq3(uint32_t B, uint32_t A0, uint32_t A1, uint32_t A2) { return B == A0 || B == A1 || B == A2; }
static inline int none_eq2(uint32_t B, uint32_t A0, uint32_t A1) { return B != A0 && B != A1; }
static inline int none_eq4(uint32_t B, uint32_t A0, uint32_t A1, uint32_t A2, uint32_t A3)
{
    return B != A0 && B != A1 && B != A2 && B != A3;
}
void pc_ui_mmpx2x(const uint32_t* source, int w, int h, uint32_t* dst)
{
#define S(x, y) source[((y) < 0 ? 0 : (y) >= h ? h - 1 : (y)) * w + ((x) < 0 ? 0 : (x) >= w ? w - 1 : (x))]
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t A = S(x - 1, y - 1), B = S(x, y - 1), C = S(x + 1, y - 1);
            uint32_t D = S(x - 1, y), E = S(x, y), F = S(x + 1, y);
            uint32_t G = S(x - 1, y + 1), H = S(x, y + 1), I = S(x + 1, y + 1);
            uint32_t J = E, K = E, L = E, M = E;
            if (((A ^ E) | (B ^ E) | (C ^ E) | (D ^ E) | (F ^ E) | (G ^ E) | (H ^ E) | (I ^ E)) != 0) {
                uint32_t P = S(x, y - 2), Sv = S(x, y + 2), Q = S(x - 2, y), R = S(x + 2, y);
                uint32_t Bl = luma(B), Dl = luma(D), El = luma(E), Fl = luma(F), Hl = luma(H);
                /* 1:1 slope rules */
                if ((D == B && D != H && D != F) && (El >= Dl || E == A) && any_eq3(E, A, C, G)
                    && ((El < Dl) || A != D || E != P || E != Q))
                    J = D;
                if ((B == F && B != D && B != H) && (El >= Bl || E == C) && any_eq3(E, A, C, I)
                    && ((El < Bl) || C != B || E != P || E != R))
                    K = B;
                if ((H == D && H != F && H != B) && (El >= Hl || E == G) && any_eq3(E, A, G, I)
                    && ((El < Hl) || G != H || E != Sv || E != Q))
                    L = H;
                if ((F == H && F != B && F != D) && (El >= Fl || E == I) && any_eq3(E, C, G, I)
                    && ((El < Fl) || I != H || E != R || E != Sv))
                    M = F;
                /* Intersection rules */
                if ((E != F && all_eq4(E, C, I, D, Q) && all_eq2(F, B, H)) && (F != S(x + 3, y)))
                    K = M = F;
                if ((E != D && all_eq4(E, A, G, F, R) && all_eq2(D, B, H)) && (D != S(x - 3, y)))
                    J = L = D;
                if ((E != H && all_eq4(E, G, I, B, P) && all_eq2(H, D, F)) && (H != S(x, y + 3)))
                    L = M = H;
                if ((E != B && all_eq4(E, A, C, H, Sv) && all_eq2(B, D, F)) && (B != S(x, y - 3)))
                    J = K = B;
                if (Bl < El && all_eq4(E, G, H, I, Sv) && none_eq4(E, A, D, C, F))
                    J = K = B;
                if (Hl < El && all_eq4(E, A, B, C, P) && none_eq4(E, D, G, I, F))
                    L = M = H;
                if (Fl < El && all_eq4(E, A, D, G, Q) && none_eq4(E, B, C, I, H))
                    K = M = F;
                if (Dl < El && all_eq4(E, C, F, I, R) && none_eq4(E, B, A, G, H))
                    J = L = D;
                /* 2:1 slope rules */
                if (H != B) {
                    if (H != A && H != E && H != C) {
                        if (all_eq3(H, G, F, R) && none_eq2(H, D, S(x + 2, y - 1)))
                            L = M;
                        if (all_eq3(H, I, D, Q) && none_eq2(H, F, S(x - 2, y - 1)))
                            M = L;
                    }
                    if (B != I && B != G && B != E) {
                        if (all_eq3(B, A, F, R) && none_eq2(B, D, S(x + 2, y + 1)))
                            J = K;
                        if (all_eq3(B, C, D, Q) && none_eq2(B, F, S(x - 2, y + 1)))
                            K = J;
                    }
                }
                if (F != D) {
                    if (D != I && D != E && D != C) {
                        if (all_eq3(D, A, H, Sv) && none_eq2(D, B, S(x + 1, y + 2)))
                            J = L;
                        if (all_eq3(D, G, B, P) && none_eq2(D, H, S(x + 1, y - 2)))
                            L = J;
                    }
                    if (F != E && F != A && F != G) {
                        if (all_eq3(F, C, H, Sv) && none_eq2(F, B, S(x - 1, y + 2)))
                            K = M;
                        if (all_eq3(F, I, B, P) && none_eq2(F, H, S(x - 1, y - 2)))
                            M = K;
                    }
                }
            }
            uint32_t* o = dst + (size_t)(2 * y) * (2 * w) + 2 * x;
            o[0] = J, o[1] = K, o[2 * w] = L, o[2 * w + 1] = M;
        }
#undef S
}
/* `levels` passes of 2x over a w x h region of PS1 colours. */
static uint32_t* magnify_cpu(const uint16_t* texels, int w, int h, int levels)
{
    uint32_t* a = malloc((size_t)w * h * sizeof *a);
    if (!a)
        return NULL;
    for (int i = 0; i < w * h; i++)
        a[i] = texels[i];
    for (int l = 0; l < levels; l++) {
        uint32_t* b = malloc((size_t)w * h * 4 * sizeof *b);
        if (!b) {
            free(a);
            return NULL;
        }
        pc_ui_mmpx2x(a, w, h, b);
        free(a);
        a = b, w *= 2, h *= 2;
    }
    return a;
}

/* ---- regions, entries and the atlas ---- */

typedef struct {
    uint64_t hash; /* content (0: empty slot) */
    uint16_t x, y, w, h; /* atlas origin and size (source texels) */
    int text; /* drawn from a font (ui_text.c), not magnified */
} entry;
#define ENTRY_SLOTS 16384
static entry entries[ENTRY_SLOTS];
static long entry_count, entry_texels;

typedef struct {
    uint64_t key; /* page, CLUT, rectangle (0: empty slot) */
    uint64_t hash;   /* its last content */
    uint32_t stamp;  /* tile generation it was read at */
    uint32_t tiles;  /* tiles it reads (texels and CLUT) */
    int changes, last_change, bypass_until, last_used;
    int16_t x, y; /* its offset: entry's atlas origin - the decoded rectangle's origin (in texels) */
} location;
#define LOCATION_SLOTS 8192
static location locations[LOCATION_SLOTS];
static long location_count;

static uint32_t tile_generation[32], generation = 1;
static int frame;
static int debug = -1; /* ROOD_UI_DEBUG: 1 events, 2 every new region and decision */
static int areas[8][5]; /* drawing areas: x, y, w, h, last frame used */

/* Shelf allocator over the atlas's source texel space. */
#define MAX_SHELVES 256
static struct {
    int y, h, x;
} shelves[MAX_SHELVES];
static int shelf_count, shelf_bottom;

/* Regions waiting for their magnification (pc_ui_upscale_prepare). */
typedef struct {
    int x, y, w, h;
} pending_entry;
#define MAX_PENDING 1024
static pending_entry pending[MAX_PENDING];
static int pending_count;
/* Glyph regions drawn from a font (ui_text): their finished texels, uploaded
 * into the last level as they are. */
typedef struct {
    int x, y, w, h;
    uint32_t* texels;
} pending_text;
static pending_text pending_texts[MAX_PENDING];
static int pending_text_count;
static long frame_new_texels;

static pc_ui_upscale_stats stats;
static double frame_ms_acc, miss_ms_acc;
static long frame_lookups_acc, frame_misses_acc;

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
static uint32_t tiles_for(int x0, int y0, int x1, int y1)
{
    uint32_t mask = 0;
    if (x1 - x0 >= 1023)
        x0 = 0, x1 = 1023;
    for (int y = y0 & ~255; y <= y1; y += 256)
        for (int x = x0 & ~63; x <= x1; x += 64)
            mask |= 1u << (((y & 511) >> 8) * 16 + ((x & 1023) >> 6));
    return mask;
}
void pc_ui_upscale_tiles_changed(uint32_t tiles)
{
    if (!tiles)
        return;
    ++generation;
    for (int t = 0; t < 32; t++)
        if (tiles & (1u << t))
            tile_generation[t] = generation;
}
void pc_ui_upscale_draw_area(const int* clip)
{
    int oldest = 0;
    for (int i = 0; i < 8; i++) {
        if (areas[i][0] == clip[0] && areas[i][1] == clip[1] && areas[i][2] == clip[2] && areas[i][3] == clip[3]) {
            areas[i][4] = frame;
            return;
        }
        if (areas[i][4] < areas[oldest][4])
            oldest = i;
    }
    memcpy(areas[oldest], clip, 4 * sizeof(int));
    areas[oldest][4] = frame;
    if (debug > 1)
        fprintf(stderr, "[ui] drawing area %d,%d %dx%d\n", clip[0], clip[1], clip[2], clip[3]);
}
void pc_ui_upscale_frame(void)
{
    if (debug < 0)
        debug = getenv("ROOD_UI_DEBUG") ? atoi(getenv("ROOD_UI_DEBUG")) : 0;
    ++frame;
    if (pc_ui_text)
        pc_ui_text_warm(2.0); /* glyph fields ahead of use, a little each frame */
    pc_ui_text_frame();
    frame_new_texels = 0;
    if (!frame_lookups_acc)
        return; /* an image without 2D: keep the last numbers */
    stats.frame_lookups = frame_lookups_acc;
    stats.frame_misses = frame_misses_acc;
    stats.frame_ms = frame_ms_acc;
    stats.miss_ms = miss_ms_acc;
    frame_lookups_acc = frame_misses_acc = 0;
    frame_ms_acc = miss_ms_acc = 0;
}
/* The atlas factor for internal resolution `scale`, whatever uses it: the
 * next power of two at or above the scale (3x samples a 4x atlas). Text and
 * icons (ui_text, button_prompts: distance texels, drawn anti-aliased at
 * any resolution, the window's too) take at least 4 a texel, at 1x too. */
static int factor_for(int scale)
{
    int f = scale < 2 ? 0 : scale <= 2 ? 2 : scale <= 4 ? 4 : 8;
    if ((pc_ui_text || pc_button_prompts_active() || pc_ui_bubbles) && f < 4)
        f = 4;
    return f;
}
int pc_ui_upscale_factor(int scale)
{
    if (pc_ui_upscale != PC_UI_MMPX || scale < 2)
        return 0;
    return factor_for(scale);
}

static void drop_all(void)
{
    memset(entries, 0, sizeof entries);
    memset(locations, 0, sizeof locations);
    entry_count = entry_texels = location_count = 0;
    shelf_count = shelf_bottom = 0;
    pending_count = 0;
    for (int i = 0; i < pending_text_count; i++)
        free(pending_texts[i].texels);
    pending_text_count = 0;
}
static const char* full_reason = "requested";
void pc_ui_upscale_reset(void)
{
    if (debug > 0)
        fprintf(stderr, "[ui] atlas reset (%s full): %ld entries, %ld texels, %ld locations, %d shelves\n", full_reason,
            entry_count, entry_texels, location_count, shelf_count);
    full_reason = "requested";
    drop_all();
    ++stats.resets;
}
static int allocate(int w, int h, int* x, int* y)
{
    int best = -1;
    for (int i = 0; i < shelf_count; i++)
        if (shelves[i].h >= h && shelves[i].h <= h + h / 2 + 4 && shelves[i].x + w <= atlas_w
            && (best < 0 || shelves[i].h < shelves[best].h))
            best = i;
    if (best < 0) {
        int sh = (h + 3) & ~3;
        if (shelf_count == MAX_SHELVES || shelf_bottom + sh > atlas_h || w > atlas_w)
            return 0;
        best = shelf_count++;
        shelves[best].y = shelf_bottom, shelves[best].h = sh, shelves[best].x = 0;
        shelf_bottom += sh;
    }
    *x = shelves[best].x, *y = shelves[best].y;
    shelves[best].x += w;
    return 1;
}

/* ---- GPU backends ---- */

static int backend = -1; /* 1 Vulkan compute, 2 GL compute, 0 CPU (uploads only), -2 unavailable */
static int cpu_only = -1;
static GLuint gl_levels[MAX_LEVELS + 1], gl_program, gl_verify_fbo;
static GLint gl_rect;
#ifdef ROOD_VULKAN
static vk_image vk_levels[MAX_LEVELS + 1];
static VkDescriptorSetLayout vk_set;
static VkPipelineLayout vk_layout;
static VkPipeline vk_pipe;
#endif

static void destroy_atlas(void)
{
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        for (int l = 0; l <= MAX_LEVELS; l++)
            if (vk_levels[l].image)
                vk_image_destroy_later(&vk_levels[l]);
        memset(vk_levels, 0, sizeof vk_levels);
    } else
#endif
    {
        for (int l = 0; l <= MAX_LEVELS; l++)
            if (gl_levels[l])
                gl_DeleteTextures(1, &gl_levels[l]);
        memset(gl_levels, 0, sizeof gl_levels);
    }
    free(shadow);
    shadow = NULL;
    atlas_factor = 0;
}
static GLuint gl_level_texture(int w, int h, GLenum format)
{
    GLuint t;
    gl_GenTextures(1, &t);
    gl_BindTexture(GL_TEXTURE_2D, t);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_TexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, GL_RED_INTEGER,
        format == GL_R16UI ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, NULL);
    gl_BindTexture(GL_TEXTURE_2D, 0);
    return t;
}
/* The atlas for magnification `factor`; 0 when it cannot be made. */
static int make_atlas(int factor)
{
    if (atlas_factor == factor)
        return 1;
    destroy_atlas();
    drop_all();
    if (debug < 0)
        debug = getenv("ROOD_UI_DEBUG") ? atoi(getenv("ROOD_UI_DEBUG")) : 0;
    if (cpu_only < 0)
        cpu_only = getenv("ROOD_UI_UPSCALE_CPU") && atoi(getenv("ROOD_UI_UPSCALE_CPU"));
    atlas_levels = factor == 2 ? 1 : factor == 4 ? 2 : 3;
    atlas_w = factor == 8 ? 512 : 1024;
    atlas_h = 512;
    shadow = calloc((size_t)atlas_w * atlas_h, sizeof *shadow);
    if (!shadow)
        return 0;
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        if (!vk_pipe && !cpu_only) {
            VkDescriptorType types[2] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE };
            vk_set = vk_push_layout(types, 2);
            VkPushConstantRange range = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 16 };
            VkPipelineLayoutCreateInfo info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = 1,
                .pSetLayouts = &vk_set,
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &range };
            if (vk_check(vkCreatePipelineLayout(vk.device, &info, NULL, &vk_layout), "vkCreatePipelineLayout")) {
                VkShaderModule cs = vk_shader(vk_spv_ui_mmpx_comp, vk_spv_ui_mmpx_comp_size);
                vk_pipe = vk_compute_pipeline(cs, vk_layout);
                vkDestroyShaderModule(vk.device, cs, NULL);
            }
        }
        backend = vk_pipe && !cpu_only ? 1 : 0;
        VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                                | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        for (int l = backend ? 0 : atlas_levels; l <= atlas_levels; l++)
            if (!vk_image_create(&vk_levels[l], atlas_w << l, atlas_h << l, l ? VK_FORMAT_R32_UINT : VK_FORMAT_R16_UINT,
                    usage | (l ? VK_IMAGE_USAGE_STORAGE_BIT : 0))) {
                backend = -2;
                return 0;
            }
    } else
#endif
    {
        if (pc_gl_compute && !gl_program && !cpu_only) {
            gl_program = pc_gl_compute_program(pc_glsl_ui_mmpx);
            if (gl_program) {
                gl_rect = gl_GetUniformLocation(gl_program, "rect");
                gl_UseProgram(gl_program);
                gl_Uniform1i(gl_GetUniformLocation(gl_program, "src_level"), 0);
                gl_UseProgram(0);
            }
        }
        backend = gl_program && !cpu_only ? 2 : 0;
        for (int l = backend ? 0 : atlas_levels; l <= atlas_levels; l++)
            gl_levels[l] = gl_level_texture(atlas_w << l, atlas_h << l, l ? GL_R32UI : GL_R16UI);
    }
    atlas_factor = factor;
    stats.factor = factor;
    stats.gpu = backend > 0 ? backend : 0;
    fprintf(stderr, "[ui] MMPX %dx atlas (%d x %d), %s\n", factor, atlas_w * factor, atlas_h * factor,
        backend == 1 ? "Vulkan compute" : backend == 2 ? "OpenGL compute" : "CPU");
    return 1;
}
int pc_ui_upscale_draw_factor(void)
{
    return pc_ui_upscale == PC_UI_MMPX || pc_ui_text || pc_button_prompts_active() || pc_ui_bubbles ? atlas_factor : 0;
}
unsigned pc_ui_upscale_gl_texture(void) { return atlas_factor ? gl_levels[atlas_levels] : 0; }
#ifdef ROOD_VULKAN
void* pc_ui_upscale_vk_view(void) { return atlas_factor ? (void*)vk_levels[atlas_levels].view : NULL; }
#endif

/* ---- lookups ---- */

static uint64_t mix64(uint64_t h)
{
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return h;
}
/* The region's texels as PS1 colours (row by row) and their content hash. */
static uint64_t decode(const uint16_t* vram, unsigned tpage, unsigned clut, int u0, int v0, int w, int h,
    uint16_t* out)
{
    int tx = (int)(tpage & 15) * 64, ty = (int)((tpage >> 4) & 1) * 256, depth = (int)((tpage >> 7) & 3);
    const uint16_t* lut = vram + ((clut >> 6) & 511) * 1024 + (clut & 63) * 16;
    uint64_t hash = 0x9e3779b97f4a7c15ull ^ ((uint64_t)w << 32 | (uint64_t)h);
    for (int y = 0; y < h; y++) {
        const uint16_t* row = vram + ((ty + v0 + y) & 511) * 1024;
        uint16_t* o = out + y * w;
        for (int x = 0; x < w; x++) {
            int u = u0 + x;
            uint16_t t;
            if (depth == 0)
                t = lut[(row[(tx + (u >> 2)) & 1023] >> ((u & 3) * 4)) & 15];
            else if (depth == 1)
                t = lut[(row[(tx + (u >> 1)) & 1023] >> ((u & 1) * 8)) & 255];
            else
                t = row[(tx + u) & 1023];
            o[x] = t;
        }
        /* Four texels at a time into the hash. */
        for (int x = 0; x < w; x += 4) {
            uint64_t word = 0;
            for (int k = 0; k < 4 && x + k < w; k++)
                word |= (uint64_t)o[x + k] << (16 * k);
            hash = mix64(hash ^ word) + 0x2545f4914f6cdd1dull;
        }
    }
    return hash | 1;
}
static entry* find_entry(uint64_t hash, int w, int h, int* found)
{
    unsigned s = (unsigned)(hash >> 20) & (ENTRY_SLOTS - 1);
    for (int probe = 0; probe < 64; probe++, s = (s + 1) & (ENTRY_SLOTS - 1)) {
        if (!entries[s].hash) {
            *found = 0;
            return &entries[s];
        }
        if (entries[s].hash == hash && entries[s].w == w && entries[s].h == h) {
            *found = 1;
            return &entries[s];
        }
    }
    *found = 0;
    return NULL;
}
static location* find_location(uint64_t key)
{
    unsigned s = (unsigned)(mix64(key) & (LOCATION_SLOTS - 1));
    for (int probe = 0; probe < 64; probe++, s = (s + 1) & (LOCATION_SLOTS - 1))
        if (!locations[s].key || locations[s].key == key)
            return &locations[s];
    return NULL;
}
static int overlaps_drawing(int x0, int y0, int x1, int y1)
{
    for (int i = 0; i < 8; i++) {
        const int* a = areas[i];
        if (a[2] <= 0 || a[3] <= 0 || frame - a[4] > 4) /* presents: 1, 2 or 4 a game frame */
            continue;
        if (x0 < a[0] + a[2] && a[0] <= x1 && y0 < a[1] + a[3] && a[1] <= y1)
            return 1;
    }
    return 0;
}
#define CONTEXT 3 /* texels of page context around strips (MMPX reads 3 away) */
static uint16_t decode_buffer[256 * 256];
int pc_ui_upscale_lookup(const uint16_t* vram, unsigned tpage, unsigned clut, unsigned window, int u0, int v0,
    int u1, int v1, int scale, int16_t offset[2])
{
    if (!vram || (window & 0x3ff) || u0 < 0 || v0 < 0 || u1 > 255 || v1 > 255 || u1 < u0 || v1 < v0)
        return 0;
    /* ui_text: a glyph the font draws (whatever ui_upscale is). */
    uint64_t glyph_key = 0;
    /* button_prompts: a PS1 button drawn as the bound key or gamepad button
     * (at internal resolution 1 too: a 2x atlas whose blocks are uniform). */
    int prompt = pc_button_prompts_classify(vram, tpage, clut, u0, v0, u1, v1, &glyph_key);
    /* ui_bubbles vector, ui_text font: the emote bubbles over actors. */
    int emote = !prompt && (pc_ui_bubbles || pc_ui_text)
             && pc_ui_emote_classify(vram, tpage, clut, u0, v0, u1, v1, &glyph_key);
    int text = prompt || emote
            || (pc_ui_text && pc_ui_text_classify(vram, tpage, clut, u0, v0, u1, v1, &glyph_key));
    int factor = text ? factor_for(scale) : pc_ui_upscale_factor(scale);
    if (!factor)
        return 0;
    if (atlas_factor != factor && !make_atlas(factor))
        return 0;
    if (backend == -2)
        return 0;
    double start = now_ms();
    ++stats.lookups;
    ++frame_lookups_acc;
    int depth = (int)((tpage >> 7) & 3);
    if (depth == 3)
        depth = 2;
    if (depth == 2)
        clut = 0;
    uint64_t key = (uint64_t)((tpage & 0x1f) | (unsigned)depth << 5) << 48 | (uint64_t)(clut & 0xffff) << 32
                 | (uint64_t)u0 << 24 | (uint64_t)v0 << 16 | (uint64_t)u1 << 8 | (uint64_t)v1;
    key |= 1ull << 63;
    if (prompt) {
        /* What the prompts show changed (device, mode, bindings): every
         * region's content is checked again (its hash names the icon). */
        static unsigned prompt_generation;
        if (prompt_generation != pc_button_prompts_generation()) {
            prompt_generation = pc_button_prompts_generation();
            pc_ui_upscale_tiles_changed(0xffffffffu);
        }
        key |= 1ull << 62 | 1ull << 61; /* page bits stop at 55; ui_text's generation below 61 */
    } else if (text) {
        key |= 1ull << 62 | (uint64_t)(pc_ui_text_generation & 0x1f) << 56;
        /* A sign ui_text draws in one style or another by where it is
         * printed (beside the digits, or in the info style's words) keeps a
         * place for each: the style's low bit (bit 55 is free above the page). */
        if (!emote)
            key |= (glyph_key >> 36 & 1) << 55;
    }
    /* Strips (an image drawn a few rows or columns at a time: the title
     * menu's waving labels) take their neighbours from the page as context:
     * clamped at a one-texel edge MMPX would see no shapes at all. */
    int region[4] = { u0, v0, u1, v1 };
    if (!text) { /* drawn from the font: no context (either way: the digits' colon is 3 wide) */
        if (v1 - v0 < 3)
            v0 = v0 < CONTEXT ? 0 : v0 - CONTEXT, v1 = v1 + CONTEXT > 255 ? 255 : v1 + CONTEXT;
        if (u1 - u0 < 3)
            u0 = u0 < CONTEXT ? 0 : u0 - CONTEXT, u1 = u1 + CONTEXT > 255 ? 255 : u1 + CONTEXT;
    }
    if (u0 != region[0] || v0 != region[1] || u1 != region[2] || v1 != region[3]) {
        /* No context from a framebuffer next to the page. */
        int tx = (int)(tpage & 15) * 64, ty = (int)((tpage >> 4) & 1) * 256;
        int shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
        if (overlaps_drawing(tx + (u0 >> shift), ty + v0, tx + (u1 >> shift), ty + v1))
            u0 = region[0], v0 = region[1], u1 = region[2], v1 = region[3];
    }
    int w = u1 - u0 + 1, h = v1 - v0 + 1;
    location* loc = find_location(key);
    if (!loc) {
        frame_ms_acc += now_ms() - start;
        full_reason = "locations";
        return -1; /* the table is full */
    }
    int fresh = !loc->key;
    if (fresh) {
        memset(loc, 0, sizeof *loc);
        loc->key = key;
        ++location_count;
        int tx = (int)(tpage & 15) * 64, ty = (int)((tpage >> 4) & 1) * 256;
        int shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
        loc->tiles = tiles_for(tx + (u0 >> shift), ty + v0, tx + (u1 >> shift), ty + v1);
        if (depth < 2) {
            int cx = (int)(clut & 63) * 16, cy = (int)((clut >> 6) & 511);
            loc->tiles |= tiles_for(cx, cy, cx + (depth ? 255 : 15), cy);
        }
    }
    if (loc->bypass_until > frame) {
        ++stats.bypassed;
        frame_ms_acc += now_ms() - start;
        return 0;
    }
    /* Unchanged tiles since it was read: the same content. */
    int current = !fresh && loc->hash;
    for (int t = 0; t < 32 && current; t++)
        if ((loc->tiles & (1u << t)) && tile_generation[t] > loc->stamp)
            current = 0;
    loc->last_used = frame;
    if (current) {
        ++stats.hits;
        offset[0] = loc->x, offset[1] = loc->y;
        frame_ms_acc += now_ms() - start;
        return text ? 2 : 1;
    }
    /* Render-to-texture (the framebuffer as a texture) is not UI art. */
    {
        int tx = (int)(tpage & 15) * 64, ty = (int)((tpage >> 4) & 1) * 256;
        int shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
        if (overlaps_drawing(tx + (u0 >> shift), ty + v0, tx + (u1 >> shift), ty + v1)) {
            if (debug > 1)
                fprintf(stderr, "[ui] page %x clut %x uv %d,%d..%d,%d is in a drawing area: nearest\n", tpage, clut, u0,
                    v0, u1, v1);
            ++stats.bypassed;
            frame_ms_acc += now_ms() - start;
            return 0;
        }
    }
    uint64_t hash = decode(vram, tpage, clut, u0, v0, w, h, decode_buffer);
    if (text) /* the glyph, where it sits in the page (strips of different glyphs can match) and the fonts */
        hash = mix64(hash ^ glyph_key ^ ((uint64_t)u0 | (uint64_t)v0 << 8 | (uint64_t)u1 << 16 | (uint64_t)v1 << 24
                                            | (uint64_t)(clut & 0xffff) << 32)) | 1;
    loc->stamp = generation;
    if (loc->hash && loc->hash != hash) {
        /* Content that changes on most frames (animations drawn into VRAM,
         * streamed images) is not cached: nearest for a while. */
        loc->changes = frame - loc->last_change <= 4 ? loc->changes + 1 : 1;
        loc->last_change = frame;
        if (loc->changes >= 4) {
            loc->bypass_until = frame + 120;
            loc->changes = 0;
            loc->hash = 0;
            ++stats.bypassed;
            if (debug > 0)
                fprintf(stderr, "[ui] region page %x clut %x %d,%d..%d,%d changes every frame: nearest\n", tpage,
                    clut, u0, v0, u1, v1);
            frame_ms_acc += now_ms() - start;
            return 0;
        }
    }
    int found;
    entry* e = find_entry(hash, w, h, &found);
    if (!e) {
        frame_ms_acc += now_ms() - start;
        full_reason = "entries";
        return -1;
    }
    if (!found) {
        long budget = backend > 0 ? (1l << 20) : (1l << 16);
        if (frame_new_texels + (long)w * h > budget || pending_count == MAX_PENDING
            || pending_text_count == MAX_PENDING) {
            ++stats.over_budget;
            loc->hash = 0; /* next frame */
            frame_ms_acc += now_ms() - start;
            return 0;
        }
        int x, y;
        if (!allocate(w, h, &x, &y)) {
            frame_ms_acc += now_ms() - start;
            full_reason = "space";
            return -1;
        }
        uint32_t* texels = NULL;
        if (text) {
            texels = malloc((size_t)w * factor * h * factor * sizeof *texels);
            if (!texels
                || !(prompt  ? pc_button_prompts_render(vram, tpage, clut, u0, v0, u1, v1, factor, scale, texels)
                     : emote ? pc_ui_emote_render(vram, tpage, clut, u0, v0, u1, v1, factor, texels)
                             : pc_ui_text_render(vram, tpage, clut, u0, v0, u1, v1, factor, texels))) {
                free(texels);
                loc->hash = 0;
                frame_ms_acc += now_ms() - start;
                return 0; /* as the original */
            }
        }
        e->hash = hash, e->x = (uint16_t)x, e->y = (uint16_t)y, e->w = (uint16_t)w, e->h = (uint16_t)h;
        e->text = text;
        ++entry_count;
        entry_texels += (long)w * h;
        frame_new_texels += (long)w * h;
        for (int r = 0; r < h; r++)
            memcpy(shadow + (size_t)(y + r) * atlas_w + x, decode_buffer + r * w, (size_t)w * 2);
        if (text)
            pending_texts[pending_text_count++] = (pending_text){ x, y, w, h, texels };
        else
            pending[pending_count++] = (pending_entry){ x, y, w, h };
        ++stats.misses;
        ++frame_misses_acc;
        if (debug > 1)
            fprintf(stderr, "[ui] new %dx%d page %x clut %x uv %d,%d -> atlas %d,%d\n", w, h, tpage, clut, u0, v0, x,
                y);
    } else
        ++stats.hits;
    loc->hash = hash;
    loc->x = (int16_t)(e->x - u0), loc->y = (int16_t)(e->y - v0);
    offset[0] = loc->x, offset[1] = loc->y;
    frame_ms_acc += now_ms() - start;
    return text ? 2 : 1;
}

/* A laid-out glyph's image (ui_text.c, ui_text_layout line): not a VRAM
 * region but texels the font draws, cached by their content key. */
int pc_ui_upscale_lookup_image(int image, int scale, int16_t offset[2])
{
    uint64_t key;
    int w, h, factor = factor_for(scale);
    if (!factor || !pc_ui_text_image_key(image, &key, &w, &h))
        return 0;
    if (atlas_factor != factor && !make_atlas(factor))
        return 0;
    if (backend == -2)
        return 0;
    double start = now_ms();
    ++stats.lookups;
    ++frame_lookups_acc;
    uint64_t hash = mix64(key ^ 0x6c61796f7574ull) | 1;
    int found;
    entry* e = find_entry(hash, w, h, &found);
    if (!e) {
        frame_ms_acc += now_ms() - start;
        full_reason = "entries";
        return -1;
    }
    if (!found) {
        long budget = backend > 0 ? (1l << 20) : (1l << 16);
        if (frame_new_texels + (long)w * h > budget || pending_text_count == MAX_PENDING) {
            ++stats.over_budget;
            frame_ms_acc += now_ms() - start;
            return 0;
        }
        int x, y;
        if (!allocate(w, h, &x, &y)) {
            frame_ms_acc += now_ms() - start;
            full_reason = "space";
            return -1;
        }
        uint32_t* texels = malloc((size_t)w * factor * h * factor * sizeof *texels);
        if (!texels || !pc_ui_text_image_render(image, factor, texels)) {
            free(texels);
            frame_ms_acc += now_ms() - start;
            return 0;
        }
        e->hash = hash, e->x = (uint16_t)x, e->y = (uint16_t)y, e->w = (uint16_t)w, e->h = (uint16_t)h;
        e->text = 1;
        ++entry_count;
        entry_texels += (long)w * h;
        frame_new_texels += (long)w * h;
        for (int r = 0; r < h; r++) /* (the shadow copy: nothing of VRAM) */
            memset(shadow + (size_t)(y + r) * atlas_w + x, 0, (size_t)w * 2);
        pending_texts[pending_text_count++] = (pending_text){ x, y, w, h, texels };
        ++stats.misses;
        ++frame_misses_acc;
    } else
        ++stats.hits;
    offset[0] = (int16_t)e->x, offset[1] = (int16_t)e->y;
    frame_ms_acc += now_ms() - start;
    return 2;
}

/* ---- magnifying what was looked up ---- */

static void prepare_cpu(void)
{
    for (int i = 0; i < pending_count; i++) {
        const pending_entry* p = &pending[i];
        uint16_t* texels = malloc((size_t)p->w * p->h * 2);
        if (!texels)
            continue;
        for (int r = 0; r < p->h; r++)
            memcpy(texels + r * p->w, shadow + (size_t)(p->y + r) * atlas_w + p->x, (size_t)p->w * 2);
        uint32_t* out = magnify_cpu(texels, p->w, p->h, atlas_levels);
        free(texels);
        if (!out)
            continue;
        int f = atlas_factor;
#ifdef ROOD_VULKAN
        if (pc_vk_on()) {
            size_t bytes = (size_t)p->w * f * p->h * f * 4;
            vk_slice slice = vk_arena(bytes, 16);
            if (slice.data) {
                memcpy(slice.data, out, bytes);
                VkBufferImageCopy region = { .bufferOffset = slice.offset,
                    .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                    .imageOffset = { p->x * f, p->y * f, 0 },
                    .imageExtent = { (uint32_t)(p->w * f), (uint32_t)(p->h * f), 1 } };
                vkCmdCopyBufferToImage(vk_cmd(), slice.buffer, vk_levels[atlas_levels].image, VK_IMAGE_LAYOUT_GENERAL,
                    1, &region);
            }
        } else
#endif
        {
            gl_BindTexture(GL_TEXTURE_2D, gl_levels[atlas_levels]);
            gl_PixelStorei(GL_UNPACK_ALIGNMENT, 4);
            gl_PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            gl_TexSubImage2D(GL_TEXTURE_2D, 0, p->x * f, p->y * f, p->w * f, p->h * f, GL_RED_INTEGER, GL_UNSIGNED_INT,
                out);
            gl_BindTexture(GL_TEXTURE_2D, 0);
        }
        free(out);
    }
}
#ifdef ROOD_VULKAN
static void prepare_vulkan(void)
{
    VkCommandBuffer cmd = vk_cmd();
    /* Source texels into level 0. */
    for (int i = 0; i < pending_count; i++) {
        const pending_entry* p = &pending[i];
        vk_slice slice = vk_arena((VkDeviceSize)p->w * p->h * 2, 16);
        if (!slice.data)
            return;
        for (int r = 0; r < p->h; r++)
            memcpy((uint16_t*)slice.data + r * p->w, shadow + (size_t)(p->y + r) * atlas_w + p->x, (size_t)p->w * 2);
        VkBufferImageCopy region = { .bufferOffset = slice.offset,
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .imageOffset = { p->x, p->y, 0 },
            .imageExtent = { (uint32_t)p->w, (uint32_t)p->h, 1 } };
        vkCmdCopyBufferToImage(cmd, slice.buffer, vk_levels[0].image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    }
    vk_barrier();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk_pipe);
    for (int l = 0; l < atlas_levels; l++) {
        vk_descriptors d = { 0 };
        vk_descriptors_image(&d, 0, vk_levels[l].view, vk.nearest);
        vk_descriptors_storage_image(&d, 1, vk_levels[l + 1].view);
        vk_descriptors_push(&d, VK_PIPELINE_BIND_POINT_COMPUTE, vk_layout);
        for (int i = 0; i < pending_count; i++) {
            const pending_entry* p = &pending[i];
            int rect[4] = { p->x << l, p->y << l, p->w << l, p->h << l };
            vkCmdPushConstants(cmd, vk_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof rect, rect);
            vkCmdDispatch(cmd, (uint32_t)(rect[2] + 7) / 8, (uint32_t)(rect[3] + 7) / 8, 1);
        }
        vk_barrier();
    }
}
#endif
static void prepare_gl(void)
{
    gl_BindTexture(GL_TEXTURE_2D, gl_levels[0]);
    gl_PixelStorei(GL_UNPACK_ALIGNMENT, 2);
    gl_PixelStorei(GL_UNPACK_ROW_LENGTH, atlas_w);
    for (int i = 0; i < pending_count; i++) {
        const pending_entry* p = &pending[i];
        gl_TexSubImage2D(GL_TEXTURE_2D, 0, p->x, p->y, p->w, p->h, GL_RED_INTEGER, GL_UNSIGNED_SHORT,
            shadow + (size_t)p->y * atlas_w + p->x);
    }
    gl_PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl_UseProgram(gl_program);
    gl_ActiveTexture(GL_TEXTURE0);
    for (int l = 0; l < atlas_levels; l++) {
        gl_BindTexture(GL_TEXTURE_2D, gl_levels[l]);
        gl_BindImageTexture(1, gl_levels[l + 1], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32UI);
        for (int i = 0; i < pending_count; i++) {
            const pending_entry* p = &pending[i];
            gl_Uniform4i(gl_rect, p->x << l, p->y << l, p->w << l, p->h << l);
            gl_DispatchCompute((GLuint)((p->w << l) + 7) / 8, (GLuint)((p->h << l) + 7) / 8, 1);
        }
        gl_MemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }
    gl_BindTexture(GL_TEXTURE_2D, 0);
    gl_UseProgram(0);
}
/* Font-drawn glyph regions: their texels into the last level. */
static void upload_texts(void)
{
    int f = atlas_factor;
    for (int i = 0; i < pending_text_count; i++) {
        const pending_text* p = &pending_texts[i];
#ifdef ROOD_VULKAN
        if (pc_vk_on()) {
            size_t bytes = (size_t)p->w * f * p->h * f * 4;
            vk_slice slice = vk_arena(bytes, 16);
            if (slice.data) {
                memcpy(slice.data, p->texels, bytes);
                VkBufferImageCopy region = { .bufferOffset = slice.offset,
                    .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                    .imageOffset = { p->x * f, p->y * f, 0 },
                    .imageExtent = { (uint32_t)(p->w * f), (uint32_t)(p->h * f), 1 } };
                vkCmdCopyBufferToImage(vk_cmd(), slice.buffer, vk_levels[atlas_levels].image, VK_IMAGE_LAYOUT_GENERAL,
                    1, &region);
            }
        } else
#endif
        {
            gl_BindTexture(GL_TEXTURE_2D, gl_levels[atlas_levels]);
            gl_PixelStorei(GL_UNPACK_ALIGNMENT, 4);
            gl_PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            gl_TexSubImage2D(GL_TEXTURE_2D, 0, p->x * f, p->y * f, p->w * f, p->h * f, GL_RED_INTEGER, GL_UNSIGNED_INT,
                p->texels);
            gl_BindTexture(GL_TEXTURE_2D, 0);
        }
        free(p->texels);
    }
    pending_text_count = 0;
}
void pc_ui_upscale_prepare(void)
{
    if (!pending_count && !pending_text_count)
        return;
    double start = now_ms();
#ifdef ROOD_VULKAN
    if (pc_vk_on()) {
        vk_end_rendering();
        if (pending_count && backend == 1)
            prepare_vulkan();
        else if (pending_count)
            prepare_cpu();
        upload_texts();
        vk_barrier();
    } else
#endif
    {
        if (pending_count && backend == 2)
            prepare_gl();
        else if (pending_count)
            prepare_cpu();
        upload_texts();
    }
    pending_count = 0;
    miss_ms_acc += now_ms() - start;
}

/* ---- statistics and self-check ---- */

void pc_ui_upscale_get_stats(pc_ui_upscale_stats* s)
{
    *s = stats;
    s->entries = entry_count;
    s->texels = entry_texels;
    s->factor = atlas_factor;
}
static int read_level(int x, int y, int w, int h, uint32_t* out)
{
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        return vk_read_image(&vk_levels[atlas_levels], VK_IMAGE_ASPECT_COLOR_BIT, x, y, w, h, out, 4);
#endif
    if (!gl_verify_fbo)
        gl_GenFramebuffers(1, &gl_verify_fbo);
    gl_BindFramebuffer(GL_FRAMEBUFFER, gl_verify_fbo);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl_levels[atlas_levels], 0);
    gl_ReadBuffer(GL_COLOR_ATTACHMENT0);
    gl_PixelStorei(GL_PACK_ALIGNMENT, 4);
    gl_ReadPixels(x, y, w, h, GL_RED_INTEGER, GL_UNSIGNED_INT, out);
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
    return 1;
}
int pc_ui_upscale_verify(long* mismatches, long* foreign)
{
    *mismatches = *foreign = 0;
    if (!atlas_factor)
        return 0;
    pc_ui_upscale_prepare();
    int checked = 0, f = atlas_factor;
    for (int s = 0; s < ENTRY_SLOTS; s++) {
        const entry* e = &entries[s];
        if (!e->hash || e->text) /* font-drawn glyphs are not MMPX */
            continue;
        uint16_t* texels = malloc((size_t)e->w * e->h * 2);
        uint32_t* gpu = malloc((size_t)e->w * f * e->h * f * 4);
        uint32_t* cpu = NULL;
        if (texels && gpu) {
            for (int r = 0; r < e->h; r++)
                memcpy(texels + r * e->w, shadow + (size_t)(e->y + r) * atlas_w + e->x, (size_t)e->w * 2);
            cpu = magnify_cpu(texels, e->w, e->h, atlas_levels);
        }
        if (cpu && read_level(e->x * f, e->y * f, e->w * f, e->h * f, gpu)) {
            ++checked;
            static uint8_t present[65536 / 8];
            memset(present, 0, sizeof present);
            for (int k = 0; k < e->w * e->h; k++)
                present[texels[k] >> 3] |= (uint8_t)(1 << (texels[k] & 7));
            for (long i = 0; i < (long)e->w * f * e->h * f; i++) {
                if (gpu[i] != cpu[i])
                    ++*mismatches;
                if (gpu[i] > 0xffff || !(present[gpu[i] >> 3] & (1 << (gpu[i] & 7))))
                    ++*foreign;
            }
        }
        free(texels);
        free(gpu);
        free(cpu);
    }
    return checked;
}

/* ---- sub-pixel 2D placement (pc_ui_subpixel) ----
 *
 * The PS1 draws 2D at whole pixels, and the game's text code positions
 * glyphs in 16.16 fixed point that the packet truncates: while a talk
 * bubble zooms open or shut its text is scaled, and the truncation makes
 * the glyphs' spacing and sizes jump a pixel at a time. The glyph emitter
 * (fixed_font.c) records the unrounded corners by packet address; the
 * polygon path takes them for 2D packets whose whole-pixel corners still
 * match (an address carries another packet the next frame). */
#define PRECISE_SLOTS 4096
static struct {
    uintptr_t packet;
    uint32_t xy[4]; /* the corners as the packet holds them (x | y << 16) */
    float f[8];
} precise[PRECISE_SLOTS];
void pc_ui_precise_quad(const void* packet, const float* corners)
{
    unsigned s = (unsigned)(((uintptr_t)packet >> 2) * 2654435761u) & (PRECISE_SLOTS - 1);
    if (debug > 2)
        fprintf(stderr, "[ui] glyph %p at %.3f,%.3f..%.3f,%.3f\n", packet, corners[0], corners[1], corners[6], corners[7]);
    precise[s].packet = (uintptr_t)packet;
    for (int i = 0; i < 4; i++) {
        precise[s].xy[i] = (uint32_t)(uint16_t)(int16_t)floorf(corners[i * 2])
                         | (uint32_t)(uint16_t)(int16_t)floorf(corners[i * 2 + 1]) << 16;
        precise[s].f[i * 2] = corners[i * 2], precise[s].f[i * 2 + 1] = corners[i * 2 + 1];
    }
}
/* The recorded corners whatever pc_ui_subpixel is (ui_text_layout line
 * places scaled glyphs from them). */
int pc_ui_precise_find(const void* packet, const unsigned* xy, int count, float* corners)
{
    if (count != 4)
        return 0;
    unsigned s = (unsigned)(((uintptr_t)packet >> 2) * 2654435761u) & (PRECISE_SLOTS - 1);
    if (precise[s].packet != (uintptr_t)packet)
        return 0;
    for (int i = 0; i < 4; i++)
        if (precise[s].xy[i] != xy[i])
            return 0;
    memcpy(corners, precise[s].f, sizeof precise[s].f);
    return 1;
}
int pc_ui_precise_lookup(const void* packet, const unsigned* xy, int count, float* corners)
{
    if (!pc_ui_subpixel || count != 4)
        return 0;
    unsigned s = (unsigned)(((uintptr_t)packet >> 2) * 2654435761u) & (PRECISE_SLOTS - 1);
    if (debug > 2)
        fprintf(stderr, "[ui] 2D quad %p: %s\n", packet, precise[s].packet == (uintptr_t)packet ? "recorded" : "not recorded");
    if (precise[s].packet != (uintptr_t)packet)
        return 0;
    for (int i = 0; i < 4; i++)
        if (precise[s].xy[i] != xy[i])
            return 0;
    memcpy(corners, precise[s].f, sizeof precise[s].f);
    ++stats.precise;
    for (int i = 0; i < 8; i++)
        if (corners[i] != floorf(corners[i])) {
            ++stats.fractional;
            break;
        }
    return 1;
}

/* ---- where magnified 2D was drawn (tests: RPC ui_upscale mask) ---- */
static uint8_t drawn[512][1024 / 8];
void pc_ui_upscale_note(float x0, float y0, float x1, float y1)
{
    int a = (int)floorf(x0), b = (int)floorf(y0), c = (int)ceilf(x1), d = (int)ceilf(y1);
    a = a < 0 ? 0 : a, b = b < 0 ? 0 : b, c = c > 1024 ? 1024 : c, d = d > 512 ? 512 : d;
    for (int y = b; y < d; y++)
        for (int x = a; x < c; x++)
            drawn[y][x >> 3] |= (uint8_t)(1 << (x & 7));
}
int pc_ui_upscale_mask(const char* path)
{
    FILE* f = fopen(path, "wb");
    if (!f)
        return -1;
    fprintf(f, "P5\n1024 512\n255\n");
    for (int y = 0; y < 512; y++)
        for (int x = 0; x < 1024; x++)
            fputc(drawn[y][x >> 3] & (1 << (x & 7)) ? 255 : 0, f);
    memset(drawn, 0, sizeof drawn);
    return fclose(f) ? -1 : 0;
}

/* Cached regions as VRAM rectangles of their texels (x0, y0, x1, y1
 * inclusive) and colour depth, up to `max` (tests). */
int pc_ui_upscale_regions(int (*out)[5], int max)
{
    int n = 0;
    for (int i = 0; i < LOCATION_SLOTS && n < max; i++) {
        const location* l = &locations[i];
        if (!l->key || !l->hash || frame - l->last_used > 4)
            continue; /* drawn in the last frames only */
        unsigned page = (unsigned)(l->key >> 48) & 0x1f, depth = (unsigned)(l->key >> 53) & 3;
        int u0 = (int)(l->key >> 24) & 255, v0 = (int)(l->key >> 16) & 255, u1 = (int)(l->key >> 8) & 255,
            v1 = (int)l->key & 255;
        int tx = (int)(page & 15) * 64, ty = (int)((page >> 4) & 1) * 256, shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
        out[n][0] = tx + (u0 >> shift), out[n][1] = ty + v0, out[n][2] = tx + (u1 >> shift), out[n][3] = ty + v1;
        out[n][4] = (int)depth;
        ++n;
    }
    return n;
}
