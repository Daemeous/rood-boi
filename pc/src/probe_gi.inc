/* Probe-based global illumination (DDGI: Majercik et al. 2019, "Dynamic
 * Diffuse Global Illumination with Ray-Traced Irradiance Fields"), included
 * at the end of ptgi.c (it shares the scene, BVH, lights, emitters and
 * settings above); pc/docs/Probe_GI.md has the design and measurements.
 *
 * Per room, a grid of probes fitted to the captured room's bounds (cell
 * centres, spacing pc_probe_spacing grown until the count fits
 * pc_probe_budget). Each update traces pc_probe_rays rays from every active
 * probe (spherical Fibonacci, a new random rotation per update), shades the
 * hits like a path vertex of the path tracer (dynamic lights with shadow
 * rays, glows, baked bounce) plus the field itself (multi-bounce, with
 * bounces >= 2), and blends them into two octahedral atlases with
 * hysteresis: irradiance (8 x 8 per probe) and the distance moments for
 * Chebyshev visibility (16 x 16). The first updates in a room relocate
 * probes out of walls; then probes inside geometry, or with nothing near,
 * become inactive. The lookup (eight probes, trilinear x wrapped cosine x
 * Chebyshev visibility, normal and view bias) gives the indirect irradiance
 * the path tracer's result would (the present multiplies albedo): on Vulkan
 * at GI resolution, into the traced result's rgb (probe_apply.comp) or, with
 * no traced result this frame, into its own images from the G-buffer
 * (probe_surface.comp), both upsampled like the traced light; on OpenGL
 * (and Vulkan with ROOD_PROBE_PER_PIXEL) per display pixel in the present.
 *
 * This file: settings, the grid, resets, the per-update parameters (shared
 * by every backend), and the OpenGL 4.3 compute backend over ptgi.c's BVH.
 * vk_probes.inc (end of vk_rt.inc) is the Vulkan backend: ray queries
 * against the room's BLASes, or compute over the same BVH without ray
 * tracing hardware. The shaders (pc/shaders/vulkan/probe_*.comp/.glsl) are
 * one source for both APIs (pc/tools/gl_shaders.py embeds them for GL). */
#include "probe_gi.h"
#include "gl_shaders.h"

float pc_probe_spacing = 96.0f;
int pc_probe_rays = 64;
float pc_probe_hysteresis = 0.97f;
int pc_probe_budget = 4096;
int pc_probe_show;
/* Fractions of the smallest spacing: lookups move the shaded point this far
 * along its normal and toward the eye (fewer self-shadowing artefacts on
 * flat surfaces; DDGI's bias). */
float pc_probe_normal_bias = 0.2f, pc_probe_view_bias = 0.3f;
/* Back-face test of the room's winding (NCLIP-culled, so consistent): the
 * front faces' cross(b - a, c - a) points along the ray that sees them
 * (world Y is down: a left-handed frame). 1 flips it (PV_FRONT_CCW: front
 * when it points against the ray); ROOD_PROBE_FRONT overrides. */
static int probe_front_ccw = -1;

/* Probe GI runs wherever there is a renderer with compute: Vulkan (ray
 * queries, or the compute BVH without them) and OpenGL 4.3. */
int pc_probe_gi_available(void)
{
#ifdef ROOD_VULKAN
    if (pc_vk_on())
        return 1;
#endif
    return pc_gl_compute;
}

#define PROBE_MAX_RAYS 256 /* probe_blend.comp MAX_RAYS: the ray image's width */
#define PROBE_RELOCATE_UPDATES 8 /* updates of relocation after a room fit; classification after */
#define PROBE_IRRADIANCE_TILE 10 /* 8 x 8 + border */
#define PROBE_DEPTH_TILE 18 /* 16 x 16 + border */
enum { PV_RELOCATE = 1, PV_CLASSIFY = 2, PV_RESET = 4, PV_FRONT_CCW = 8 };

/* probe_volume.glsl (std140). */
typedef struct {
    float origin[4], spacing[4];
    int counts[4], info[4];
    float bias[4], blend[4], rotation[3][4], flicker[16][4];
    int bvh[4];
    float relocate[4], eye[4];
} probe_volume_ubo;
/* rt_lights.glsl's Trace block (vk_rt.inc vrt_trace_ubo), filled with the
 * lights at their base intensities. */
typedef struct {
    float eye[4];
    float light_pos[16][4], light_col[16][4], light_dir[16][4], light_right[16][4], light_up[16][4];
    int light_shadow[16];
    float emitter_pos[32][4], emitter_col[32][4];
    float room_lo[4], room_hi[4];
    int i0[4], i1[4], i2[4];
    float f0[4], f1[4], f2[4]; /* f2: traced AO (off for the probes) */
    int i3[4]; /* glows.c pc_glow_casts */
    pc_env_gpu env; /* the room's sun and sky (environment.h) */
} probe_trace_ubo;

static struct {
    int serial, fitted_budget;
    float fitted_spacing;
    int counts[3], probes, columns, rows;
    float origin[3], spacing[3];
    unsigned updates, converged, rebuilds, light_resets;
    unsigned light_hash;
    int reset; /* the backend clears its atlases and state before the next update */
    int backend, has_data;
    int fresh; /* updated for the coming present (the present's bind consumes it) */
    int sampled; /* the last present sampled the field */
    unsigned rng;
    float rotation[3][3];
    /* This update's lights at their base intensities, and the flicker. */
    float pos[16][4], col[16][4], dir[16][4], right[16][4], up[16][4], flicker[16][4];
    int shadows[16], count;
} probe = { .serial = -1, .rng = 0x9e3779b9u };

/* Lights with animation off (probe_overlay.cpp; light actors' base values). */
extern int pc_probe_lights_base(float position[][4], float color[][4], float direction[][4], float right[][4],
    float up[][4], int shadows[], int max) __attribute__((weak));

/* Is the probe field wanted this frame? */
static int probe_wanted(void)
{
    return pc_gi_method == PC_GI_PROBES && pc_lights_enabled;
}
/* Fit the grid to the room (room_lo/hi already set); 1 when it changed
 * (new resources and a reset). */
static int probe_fit(void)
{
    float spacing = pc_probe_spacing < 16 ? 16 : pc_probe_spacing > 4096 ? 4096 : pc_probe_spacing;
    int budget = pc_probe_budget < 8 ? 8 : pc_probe_budget > 16384 ? 16384 : pc_probe_budget;
    int serial = pc_scene_serial();
    if (serial == probe.serial && spacing == probe.fitted_spacing && budget == probe.fitted_budget)
        return 0;
    float extent[3];
    int n[3];
    for (int k = 0; k < 3; k++)
        extent[k] = room_hi[k] - room_lo[k] > 1 ? room_hi[k] - room_lo[k] : 1;
    float s = spacing;
    for (int iteration = 0; iteration < 400; iteration++) {
        long total = 1;
        for (int k = 0; k < 3; k++) {
            n[k] = (int)floorf(extent[k] / s + 0.5f);
            n[k] = n[k] < 2 ? 2 : n[k];
            total *= n[k];
        }
        if (total <= budget)
            break;
        s *= 1.05f;
    }
    probe.probes = n[0] * n[1] * n[2];
    for (int k = 0; k < 3; k++) {
        probe.counts[k] = n[k];
        probe.spacing[k] = extent[k] / (float)n[k];
        probe.origin[k] = room_lo[k] + probe.spacing[k] * 0.5f; /* cell centres: none on the outer walls */
    }
    probe.columns = (int)ceilf(sqrtf((float)probe.probes));
    probe.rows = (probe.probes + probe.columns - 1) / probe.columns;
    probe.serial = serial;
    probe.fitted_spacing = spacing;
    probe.fitted_budget = budget;
    probe.updates = probe.converged = 0;
    probe.reset = 1;
    probe.has_data = 0;
    probe.rebuilds++;
    fprintf(stderr, "[gi] probe grid %dx%dx%d = %d probes, spacing %.0f %.0f %.0f (room %.0f x %.0f x %.0f)\n", n[0],
        n[1], n[2], probe.probes, probe.spacing[0], probe.spacing[1], probe.spacing[2], extent[0], extent[1],
        extent[2]);
    return 1;
}
static unsigned probe_hash(unsigned h, const void* data, size_t bytes)
{
    const unsigned char* b = data;
    for (size_t i = 0; i < bytes; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}
static float probe_luminance(const float c[3]) { return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]; }
/* This update's lights for the probes: the base intensities (animation off)
 * traced, the current ones (gather_lights: lp, lc) as a per-light scale. */
static void probe_lights(void)
{
    int n = pc_probe_lights_base
        ? pc_probe_lights_base(probe.pos, probe.col, probe.dir, probe.right, probe.up, probe.shadows, 16)
        : -1;
    int matched = n == light_count;
    for (int i = 0; matched && i < n; i++) /* the same lights in the same order (jitter moves them a little) */
        matched = fabsf(probe.pos[i][3] - lp[i][3]) < 1e-3f && fabsf(probe.col[i][3] - lc[i][3]) < 1e-3f;
    if (!matched) {
        n = light_count;
        memcpy(probe.pos, lp, sizeof lp);
        memcpy(probe.col, lc, sizeof lc);
        memcpy(probe.dir, ld, sizeof ld);
        memcpy(probe.right, lr, sizeof lr);
        memcpy(probe.up, lu, sizeof lu);
        memcpy(probe.shadows, light_shadows, sizeof light_shadows);
    }
    probe.count = n;
    memset(probe.flicker, 0, sizeof probe.flicker);
    for (int i = 0; i < n; i++) {
        float base = probe_luminance(probe.col[i]), now = probe_luminance(lc[i]);
        probe.flicker[i][0] = base;
        probe.flicker[i][1] = base > 1e-6f ? now / base : 1.0f;
    }
}
/* A uniformly random rotation (Shoemake's quaternion) for this update's rays. */
static float probe_random(void)
{
    probe.rng ^= probe.rng << 13, probe.rng ^= probe.rng >> 17, probe.rng ^= probe.rng << 5;
    return (float)(probe.rng >> 8) / 16777216.0f;
}
static void probe_rotation(void)
{
    float u1 = probe_random(), u2 = probe_random() * 6.2831853f, u3 = probe_random() * 6.2831853f;
    float a = sqrtf(1 - u1), b = sqrtf(u1);
    float x = a * sinf(u2), y = a * cosf(u2), z = b * sinf(u3), w = b * cosf(u3);
    float (*m)[3] = probe.rotation; /* m[column][row] */
    m[0][0] = 1 - 2 * (y * y + z * z), m[0][1] = 2 * (x * y + z * w), m[0][2] = 2 * (x * z - y * w);
    m[1][0] = 2 * (x * y - z * w), m[1][1] = 1 - 2 * (x * x + z * z), m[1][2] = 2 * (y * z + x * w);
    m[2][0] = 2 * (x * z + y * w), m[2][1] = 2 * (y * z - x * w), m[2][2] = 1 - 2 * (x * x + y * y);
}
/* Start an update (lights gathered, grid fitted): light changes, the
 * relocation phase and the hysteresis. Returns the PV_* flags. */
static int probe_begin(probe_volume_ubo* v, probe_trace_ubo* t, int root)
{
    probe_lights();
    unsigned h = probe_hash(2166136261u, &probe.count, sizeof probe.count);
    h = probe_hash(h, probe.pos, sizeof probe.pos);
    h = probe_hash(h, probe.col, sizeof probe.col);
    h = probe_hash(h, probe.dir, sizeof probe.dir);
    h = probe_hash(h, probe.right, sizeof probe.right);
    h = probe_hash(h, probe.up, sizeof probe.up);
    h = probe_hash(h, probe.shadows, sizeof probe.shadows);
    float amounts[5] = { pc_gi_baked_bounce, pc_emitter_light, (float)pc_gi_bounces, (float)pc_glow_shadows, pc_light_size };
    h = probe_hash(h, amounts, sizeof amounts);
    gather_env();
    h = probe_hash(h, &env_now, sizeof env_now); /* the sun and sky */
    if (h != probe.light_hash && probe.updates) {
        /* A light moved, changed or was switched: converge again from a short
         * history (not from nothing: a dragged light stays smooth). */
        if (probe.converged > 3)
            probe.converged = 3;
        probe.light_resets++;
    }
    probe.light_hash = h;
    int flags = probe.updates < PROBE_RELOCATE_UPDATES ? PV_RELOCATE : probe.updates == PROBE_RELOCATE_UPDATES ? PV_CLASSIFY : 0;
    if (flags & PV_CLASSIFY && probe.converged > 3)
        probe.converged = 3; /* probes moved: their history is from elsewhere */
    if (probe.reset)
        flags |= PV_RESET;
    if (probe_front_ccw < 0) {
        const char* e = getenv("ROOD_PROBE_FRONT");
        probe_front_ccw = e ? atoi(e) != 0 : 0;
    }
    if (probe_front_ccw)
        flags |= PV_FRONT_CCW;
    probe_rotation();
    /* Hysteresis: a running mean until the history is as long as the setting's. */
    float h_setting = pc_probe_hysteresis < 0 ? 0 : pc_probe_hysteresis > 0.995f ? 0.995f : pc_probe_hysteresis;
    float h_mean = 1.0f - 1.0f / (float)(probe.converged + 1);
    float hysteresis = h_mean < h_setting ? h_mean : h_setting;
    int rays = pc_probe_rays < 8 ? 8 : pc_probe_rays > PROBE_MAX_RAYS ? PROBE_MAX_RAYS : pc_probe_rays;
    float smallest = fminf(probe.spacing[0], fminf(probe.spacing[1], probe.spacing[2]));
    float largest = fmaxf(probe.spacing[0], fmaxf(probe.spacing[1], probe.spacing[2]));
    float diagonal = sqrtf((room_hi[0] - room_lo[0]) * (room_hi[0] - room_lo[0])
        + (room_hi[1] - room_lo[1]) * (room_hi[1] - room_lo[1]) + (room_hi[2] - room_lo[2]) * (room_hi[2] - room_lo[2]));
    memset(v, 0, sizeof *v);
    for (int k = 0; k < 3; k++) {
        v->origin[k] = probe.origin[k];
        v->spacing[k] = probe.spacing[k];
        v->counts[k] = probe.counts[k];
        for (int r = 0; r < 3; r++)
            v->rotation[k][r] = probe.rotation[k][r];
    }
    v->origin[3] = probe.has_data ? 1.0f : 0.0f;
    v->spacing[3] = largest;
    v->counts[3] = probe.columns;
    v->info[0] = probe.probes, v->info[1] = rays, v->info[2] = (int)probe.updates, v->info[3] = flags;
    v->bias[0] = pc_probe_normal_bias * smallest;
    v->bias[1] = pc_probe_view_bias * smallest;
    v->bias[2] = fminf(diagonal * 1.5f + 64.0f, 16384.0f);
    v->bias[3] = 50.0f; /* depth sharpness */
    v->blend[0] = hysteresis;
    v->blend[1] = hysteresis;
    /* Faster adaptation where a texel's new estimate differs by more than
     * this fraction (RTXGI's threshold): off, as with a few dozen rays the
     * noise of dim texels crosses any useful threshold (frame-to-frame
     * flicker); light changes reset the convergence instead (light hash). */
    v->blend[2] = 0.0f;
    v->blend[3] = pc_gi_bounces >= 2 ? 1.0f : 0.0f; /* multi-bounce through the field */
    memcpy(v->flicker, probe.flicker, sizeof v->flicker);
    v->bvh[0] = root, v->bvh[1] = -1, v->bvh[2] = probe.count;
    v->relocate[0] = 0.3f * smallest; /* closest front face */
    v->relocate[1] = 0.25f; /* back-face fraction: inside geometry */
    v->relocate[2] = 0.45f; /* offset limit (fraction of the spacing) */
    /* Lights and emitters for the trace. */
    memset(t, 0, sizeof *t);
    memcpy(t->light_pos, probe.pos, sizeof probe.pos);
    memcpy(t->light_col, probe.col, sizeof probe.col);
    memcpy(t->light_dir, probe.dir, sizeof probe.dir);
    memcpy(t->light_right, probe.right, sizeof probe.right);
    memcpy(t->light_up, probe.up, sizeof probe.up);
    memcpy(t->light_shadow, probe.shadows, sizeof probe.shadows);
    memcpy(t->emitter_pos, emitter_pos, sizeof emitter_pos);
    memcpy(t->emitter_col, emitter_col, sizeof emitter_col);
    memcpy(t->room_lo, room_lo, sizeof room_lo);
    memcpy(t->room_hi, room_hi, sizeof room_hi);
    t->i0[0] = probe.count, t->i0[1] = 1, t->i0[3] = 1;
    t->i1[0] = 1, t->i1[1] = emitter_count, t->i1[3] = 1;
    t->i2[0] = pc_glow_shadows, t->i2[1] = 2, t->i2[3] = (int)probe.updates;
    t->i3[0] = (int)pc_glow_casts;
    t->f0[0] = pc_light_size, t->f0[1] = pc_gi_baked_bounce, t->f0[2] = pc_emitter_light, t->f0[3] = pc_contact_radius;
    t->f1[0] = pc_baked_ambient;
    t->env = env_now;
    return flags;
}
static void probe_end(void)
{
    probe.updates++;
    probe.converged++;
    probe.reset = 0;
    probe.has_data = 1;
}
/* Something to bounce: dynamic lights, glow light or baked bounce. */
static int probe_has_light(void)
{
    return light_count || (emitter_count && pc_emitter_light > 0) || pc_gi_baked_bounce > 0 || gather_env();
}

/* ---- OpenGL 4.3 compute (ptgi.c's BVH: the room part, root room_root) ---- */

static struct {
    int state; /* 0 untried, 1 ready, -1 unavailable */
    GLuint trace, blend, status;
    GLuint irradiance, depth, data, rays, state_buffer, volume, lights;
    int probes, columns, rows;
    probe_volume_ubo last; /* the present samples with this update's volume */
} probe_gl;

static int probe_gl_init(void)
{
    if (probe_gl.state)
        return probe_gl.state > 0;
    probe_gl.state = -1;
    if (!pc_gl_compute)
        return 0;
    probe_gl.trace = pc_gl_compute_program(pc_glsl_probe_trace_bvh);
    probe_gl.blend = pc_gl_compute_program(pc_glsl_probe_blend);
    probe_gl.status = pc_gl_compute_program(pc_glsl_probe_state);
    if (!probe_gl.trace || !probe_gl.blend || !probe_gl.status) {
        fprintf(stderr, "[gi] probe GI: OpenGL compute shaders failed\n");
        return 0;
    }
    gl_GenBuffers(1, &probe_gl.state_buffer);
    gl_GenBuffers(1, &probe_gl.volume);
    gl_GenBuffers(1, &probe_gl.lights);
    probe_gl.state = 1;
    fprintf(stderr, "[gi] probe GI ready (OpenGL compute over the room's BVH)\n");
    return 1;
}
static GLuint probe_gl_texture(GLuint old, int w, int h, GLenum format, GLenum filter)
{
    if (old)
        gl_DeleteTextures(1, &old);
    GLuint t;
    gl_GenTextures(1, &t);
    gl_BindTexture(GL_TEXTURE_2D, t);
    gl_TexStorage2D(GL_TEXTURE_2D, 1, format, w, h);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* Zeros (TexStorage leaves the contents undefined). */
    size_t bytes = (size_t)w * h * 16;
    void* zero = calloc(1, bytes);
    if (zero) {
        gl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_FLOAT, zero);
        free(zero);
    }
    gl_BindTexture(GL_TEXTURE_2D, 0);
    return t;
}
static void probe_gl_resources(void)
{
    if (probe_gl.probes != probe.probes || probe_gl.columns != probe.columns || probe_gl.rows != probe.rows) {
        probe_gl.probes = probe.probes, probe_gl.columns = probe.columns, probe_gl.rows = probe.rows;
        probe_gl.irradiance = probe_gl_texture(probe_gl.irradiance, probe.columns * PROBE_IRRADIANCE_TILE,
            probe.rows * PROBE_IRRADIANCE_TILE, GL_RGBA16F, GL_LINEAR);
        probe_gl.depth = probe_gl_texture(probe_gl.depth, probe.columns * PROBE_DEPTH_TILE, probe.rows * PROBE_DEPTH_TILE,
            GL_RGBA16F, GL_LINEAR);
        probe_gl.data = probe_gl_texture(probe_gl.data, probe.columns, probe.rows, GL_RGBA32F, GL_NEAREST);
        probe_gl.rays = probe_gl_texture(probe_gl.rays, PROBE_MAX_RAYS, probe.probes, GL_RGBA16F, GL_NEAREST);
        probe.reset = 1;
    }
    if (probe.reset) {
        /* Clear the atlases (a new room) and every probe: in place, active. */
        GLuint textures[3] = { probe_gl.irradiance, probe_gl.depth, probe_gl.data };
        int sizes[3][2] = { { probe.columns * PROBE_IRRADIANCE_TILE, probe.rows * PROBE_IRRADIANCE_TILE },
            { probe.columns * PROBE_DEPTH_TILE, probe.rows * PROBE_DEPTH_TILE }, { probe.columns, probe.rows } };
        void* zero = calloc(1, (size_t)sizes[1][0] * sizes[1][1] * 16);
        for (int i = 0; i < 3 && zero; i++) {
            gl_BindTexture(GL_TEXTURE_2D, textures[i]);
            gl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sizes[i][0], sizes[i][1], GL_RGBA, GL_FLOAT, zero);
        }
        free(zero);
        gl_BindTexture(GL_TEXTURE_2D, 0);
        float* state = malloc((size_t)probe.probes * 16);
        for (int i = 0; state && i < probe.probes; i++)
            state[i * 4] = state[i * 4 + 1] = state[i * 4 + 2] = 0, state[i * 4 + 3] = 1;
        gl_BindBuffer(GL_SHADER_STORAGE_BUFFER, probe_gl.state_buffer);
        gl_BufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)probe.probes * 16, state, GL_DYNAMIC_COPY);
        gl_BindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        free(state);
    }
}
static void probe_gl_dispatch(GLuint x, GLuint y)
{
    gl_DispatchCompute(x, y, 1);
    gl_MemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT
        | GL_UNIFORM_BARRIER_BIT);
}
/* Update the field (GL game view, after ptgi.c's passes: the BVH, lights and
 * emitters are this frame's). */
static void probe_gl_update(void)
{
    probe.fresh = 0;
    if (!probe_wanted() || room_root < 0 || !probe_has_light() || !probe_gl_init())
        return;
    probe_fit();
    probe_gl_resources();
    probe_volume_ubo v;
    probe_trace_ubo t;
    int flags = probe_begin(&v, &t, room_root);
    (void)flags;
    gl_BindBuffer(GL_UNIFORM_BUFFER, probe_gl.volume);
    gl_BufferData(GL_UNIFORM_BUFFER, sizeof v, &v, GL_STREAM_DRAW);
    gl_BindBuffer(GL_UNIFORM_BUFFER, probe_gl.lights);
    gl_BufferData(GL_UNIFORM_BUFFER, sizeof t, &t, GL_STREAM_DRAW);
    gl_BindBuffer(GL_UNIFORM_BUFFER, 0);
    gl_BindBufferBase(GL_UNIFORM_BUFFER, 0, probe_gl.lights);
    gl_BindBufferBase(GL_UNIFORM_BUFFER, 12, probe_gl.volume);
    gl_BindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, triangle_buffer);
    gl_BindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, node_buffer);
    gl_BindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, probe_gl.state_buffer);
    GLuint samplers[4][2] = { { 3, pc_hw_native_texture() }, { 13, probe_gl.irradiance }, { 14, probe_gl.depth },
        { 15, probe_gl.data } };
    for (int i = 0; i < 4; i++) {
        gl_ActiveTexture(GL_TEXTURE0 + samplers[i][0]);
        gl_BindTexture(GL_TEXTURE_2D, samplers[i][1]);
    }
    gl_ActiveTexture(GL_TEXTURE0);
    /* 1: trace (reads last update's atlases for the multi-bounce). */
    gl_UseProgram(probe_gl.trace);
    gl_BindImageTexture(4, probe_gl.rays, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    probe_gl_dispatch((GLuint)(v.info[1] + 31) / 32, (GLuint)probe.probes);
    /* 2: state (relocation, classification, light scale). */
    gl_UseProgram(probe_gl.status);
    gl_BindImageTexture(4, probe_gl.rays, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
    gl_BindImageTexture(7, probe_gl.data, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    probe_gl_dispatch((GLuint)(probe.probes + 63) / 64, 1);
    /* 3: blend into the atlases (and their tiles' borders). */
    gl_UseProgram(probe_gl.blend);
    gl_BindImageTexture(5, probe_gl.irradiance, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
    gl_BindImageTexture(6, probe_gl.depth, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
    probe_gl_dispatch((GLuint)probe.probes, 1);
    gl_UseProgram(0);
    for (int i = 0; i < 4; i++) {
        gl_ActiveTexture(GL_TEXTURE0 + samplers[i][0]);
        gl_BindTexture(GL_TEXTURE_2D, 0);
    }
    gl_ActiveTexture(GL_TEXTURE0);
    v.origin[3] = 1;
    probe_gl.last = v;
    probe_end();
    probe.backend = 3;
    probe.fresh = 1;
}
/* gl_present.c, after ptgi.c's update: the field on its own when the path
 * tracer did not run this frame (gi_method probes turns its indirect light
 * off; with shadow maps nothing else may need it): the room's BVH, lights
 * and emitters, then the probe update. GL state: the caller rebinds. */
void pc_probe_gl_update(void)
{
    if (probe.fresh || !probe_wanted() || !init() || !update_scene())
        return;
    update_emitters();
    pc_scene_bounds(room_lo, room_hi);
    gather_lights();
    probe_gl_update();
}
/* gl_present.c: the field for its present shader (GLSL 3.30 uniforms of
 * probe_common.glsl's names; textures on units first..first + 2). Returns 1
 * when the present should add it (gi_on 64). */
int pc_probe_gl_bind(GLuint program, int first)
{
    static GLuint cached;
    static GLint u_origin, u_spacing, u_counts, u_bias, u_irradiance, u_depth, u_data;
    if (program != cached) {
        cached = program;
        u_origin = gl_GetUniformLocation(program, "pv_origin");
        u_spacing = gl_GetUniformLocation(program, "pv_spacing");
        u_counts = gl_GetUniformLocation(program, "pv_counts");
        u_bias = gl_GetUniformLocation(program, "pv_bias");
        u_irradiance = gl_GetUniformLocation(program, "probe_irradiance");
        u_depth = gl_GetUniformLocation(program, "probe_depth");
        u_data = gl_GetUniformLocation(program, "probe_data");
    }
    gl_Uniform1i(u_irradiance, first);
    gl_Uniform1i(u_depth, first + 1);
    gl_Uniform1i(u_data, first + 2);
    int on = probe.fresh && probe.backend == 3 && probe_wanted();
    probe.fresh = 0;
    probe.sampled = on;
    GLuint textures[3] = { on ? probe_gl.irradiance : 0, on ? probe_gl.depth : 0, on ? probe_gl.data : 0 };
    for (int i = 0; i < 3; i++) {
        gl_ActiveTexture(GL_TEXTURE0 + (GLenum)(first + i));
        gl_BindTexture(GL_TEXTURE_2D, textures[i]);
    }
    gl_ActiveTexture(GL_TEXTURE0);
    if (!on)
        return 0;
    const probe_volume_ubo* v = &probe_gl.last;
    gl_Uniform4f(u_origin, v->origin[0], v->origin[1], v->origin[2], v->origin[3]);
    gl_Uniform4f(u_spacing, v->spacing[0], v->spacing[1], v->spacing[2], v->spacing[3]);
    gl_Uniform4i(u_counts, v->counts[0], v->counts[1], v->counts[2], v->counts[3]);
    gl_Uniform4f(u_bias, v->bias[0], v->bias[1], v->bias[2], v->bias[3]);
    return 1;
}
/* Read an RGBA float texture back (GL: through a framebuffer). */
static int probe_gl_read(GLuint texture, int w, int h, float* out)
{
    static GLuint fbo;
    if (!fbo)
        gl_GenFramebuffers(1, &fbo);
    gl_BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    gl_ReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, out);
    gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
    return 1;
}

/* ---- readback (RPC, editor), both APIs ---- */

#ifdef ROOD_VULKAN
static int probe_vk_read(int which, float* out); /* vk_probes.inc */
#endif
/* which: 0 irradiance atlas, 1 depth atlas, 2 probe data, 3 rays (RGBA float). */
static int probe_read(int which, float* out)
{
#ifdef ROOD_VULKAN
    if (probe.backend == 1 || probe.backend == 2)
        return probe_vk_read(which, out);
#endif
    if (probe.backend != 3)
        return 0;
    GLuint textures[4] = { probe_gl.irradiance, probe_gl.depth, probe_gl.data, probe_gl.rays };
    int w[4] = { probe.columns * PROBE_IRRADIANCE_TILE, probe.columns * PROBE_DEPTH_TILE, probe.columns, PROBE_MAX_RAYS };
    int h[4] = { probe.rows * PROBE_IRRADIANCE_TILE, probe.rows * PROBE_DEPTH_TILE, probe.rows, probe.probes };
    return probe_gl_read(textures[which], w[which], h[which], out);
}
static void probe_size(int which, int* w, int* h)
{
    int ws[4] = { probe.columns * PROBE_IRRADIANCE_TILE, probe.columns * PROBE_DEPTH_TILE, probe.columns, PROBE_MAX_RAYS };
    int hs[4] = { probe.rows * PROBE_IRRADIANCE_TILE, probe.rows * PROBE_DEPTH_TILE, probe.rows, probe.probes };
    *w = ws[which], *h = hs[which];
}
void pc_probe_get_stats(pc_probe_stats* out)
{
    memset(out, 0, sizeof *out);
    out->valid = probe.has_data && probe.serial == pc_scene_serial();
    out->backend = probe.backend;
    for (int k = 0; k < 3; k++) {
        out->counts[k] = probe.counts[k];
        out->origin[k] = probe.origin[k];
        out->spacing[k] = probe.spacing[k];
    }
    out->probes = probe.probes;
    out->rays = pc_probe_rays < 8 ? 8 : pc_probe_rays > PROBE_MAX_RAYS ? PROBE_MAX_RAYS : pc_probe_rays;
    out->updates = probe.updates;
    out->converged = probe.converged;
    out->rebuilds = probe.rebuilds;
    out->light_resets = probe.light_resets;
    out->sampled = probe.sampled;
    /* Active probes: read back now and then (a synchronous readback). */
    static int active = -1;
    static unsigned counted, fit;
    if (!probe.has_data || !probe.probes)
        active = -1;
    else if (active < 0 || fit != probe.rebuilds || probe.updates < counted || probe.updates - counted >= 30) {
        float* data = malloc((size_t)probe.columns * probe.rows * 16);
        if (data && probe_read(2, data)) {
            active = 0;
            for (int i = 0; i < probe.probes; i++)
                active += data[i * 4 + 3] > 0;
            fit = probe.rebuilds;
            counted = probe.updates;
        }
        free(data);
    }
    out->active = active;
}
/* RPC probe_gi {dump: path}: header line "rood-probe-dump NX NY NZ
 * probes rays columns rows backend updates converged ox oy oz sx sy sz
 * irradiance_w irradiance_h depth_w depth_h\n", then float32 RGBA images:
 * the probe data (columns x rows: offset, light scale / 0 inactive), the
 * irradiance atlas, the depth atlas, and this update's rays (256 x probes:
 * radiance, signed distance), rows top first in texel order. */
int pc_probe_dump(const char* path)
{
    if (!probe.has_data || !probe.probes)
        return -1;
    int w[4], h[4];
    float* images[4] = { 0 };
    int ok = 1;
    for (int i = 0; i < 4; i++) {
        probe_size(i, &w[i], &h[i]);
        images[i] = malloc((size_t)w[i] * h[i] * 16);
        ok = ok && images[i] && probe_read(i, images[i]);
    }
    FILE* f = ok ? fopen(path, "wb") : NULL;
    if (f) {
        fprintf(f, "rood-probe-dump %d %d %d %d %d %d %d %d %u %u %.3f %.3f %.3f %.3f %.3f %.3f %d %d %d %d\n",
            probe.counts[0], probe.counts[1], probe.counts[2], probe.probes,
            pc_probe_rays < 8 ? 8 : pc_probe_rays > PROBE_MAX_RAYS ? PROBE_MAX_RAYS : pc_probe_rays, probe.columns,
            probe.rows, probe.backend, probe.updates, probe.converged, probe.origin[0], probe.origin[1], probe.origin[2],
            probe.spacing[0], probe.spacing[1], probe.spacing[2], w[0], h[0], w[1], h[1]);
        int order[4] = { 2, 0, 1, 3 };
        for (int i = 0; i < 4; i++)
            fwrite(images[order[i]], 16, (size_t)w[order[i]] * h[order[i]], f);
        ok = !fclose(f);
    } else
        ok = 0;
    for (int i = 0; i < 4; i++)
        free(images[i]);
    return ok ? 0 : -1;
}
/* Editor visualisation (probe_overlay.cpp): each probe's position (w: 1
 * active, 0 inactive) and its irradiance toward `eye` (from a copy of the
 * atlas read back every 20 calls). Returns the count. */
int pc_probe_snapshot(float (*position)[4], float (*color)[4], int max, const float eye[3])
{
    static float *data, *irradiance;
    static int calls, size;
    if (!probe.has_data || !probe.probes || probe.serial != pc_scene_serial())
        return 0;
    int n = probe.probes < max ? probe.probes : max;
    int iw, ih;
    probe_size(0, &iw, &ih);
    if (size != probe.probes || calls++ % 20 == 0) {
        free(data), free(irradiance);
        data = malloc((size_t)probe.columns * probe.rows * 16);
        irradiance = malloc((size_t)iw * ih * 16);
        size = probe.probes;
        if (!data || !irradiance || !probe_read(2, data) || !probe_read(0, irradiance)) {
            size = 0;
            return 0;
        }
    }
    for (int p = 0; p < n; p++) {
        int c[3] = { p % probe.counts[0], (p / probe.counts[0]) % probe.counts[1], p / (probe.counts[0] * probe.counts[1]) };
        int tx = p % probe.columns, ty = p / probe.columns;
        const float* d = data + ((size_t)ty * probe.columns + tx) * 4;
        float to[3], l = 0;
        for (int k = 0; k < 3; k++) {
            position[p][k] = probe.origin[k] + c[k] * probe.spacing[k] + d[k];
            to[k] = eye[k] - position[p][k];
            l += to[k] * to[k];
        }
        position[p][3] = d[3] > 0;
        /* Octahedral texel toward the eye (probe_common.glsl's encoding). */
        l = sqrtf(l) > 1e-6f ? fabsf(to[0]) + fabsf(to[1]) + fabsf(to[2]) : 1;
        float e[2] = { to[0] / l, to[1] / l };
        if (to[2] < 0) {
            float ex = (1 - fabsf(e[1])) * (e[0] >= 0 ? 1 : -1), ey = (1 - fabsf(e[0])) * (e[1] >= 0 ? 1 : -1);
            e[0] = ex, e[1] = ey;
        }
        int u = (int)((e[0] * 0.5f + 0.5f) * 8), v = (int)((e[1] * 0.5f + 0.5f) * 8);
        u = u < 0 ? 0 : u > 7 ? 7 : u, v = v < 0 ? 0 : v > 7 ? 7 : v;
        const float* texel = irradiance + ((size_t)(ty * PROBE_IRRADIANCE_TILE + 1 + v) * iw + tx * PROBE_IRRADIANCE_TILE + 1 + u) * 4;
        for (int k = 0; k < 3; k++)
            color[p][k] = texel[k] * (d[3] > 0 ? d[3] : 1);
        color[p][3] = 1;
    }
    return n;
}
