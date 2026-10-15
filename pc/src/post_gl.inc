/* The post stack on OpenGL (gl_present.c includes this; post.h,
 * pc/docs/Post_Processing.md): the grade pass (post_grade.glsl), the
 * Gaussian bloom pyramid and the auto exposure meter (post_bloom.glsl).
 * The convolution (FFT) bloom is Vulkan's (vk_present.c); OpenGL takes the
 * Gaussian one whatever the profile asks. Made on first use: with every
 * effect neutral nothing here runs. */

static const char* grade_fragment_source =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "uniform sampler2D image;\n"
    "uniform sampler2D bloom_tex;\n"
    "uniform sampler2D lut_tex;\n"
    "uniform vec4 pg_[40];\n" /* post.h pc_post_gpu */
    "uniform ivec2 origin;\n" /* the output's corner (the window's view, or 0) */
    "uniform int last_;\n" /* writing the window: alpha 1 */
    "#define PG(i) pg_[i]\n"
    "vec4 pg_input(vec2 st) { return texture(image, st); }\n"
    "vec3 pg_bloom(vec2 uv) { return texture(bloom_tex, vec2(uv.x, 1.0 - uv.y)).rgb; }\n"
    "vec3 pg_lut(vec3 c) {\n"
    "    float n = PG(8).w, b = c.b * (n - 1.0), b0 = floor(b);\n"
    "    vec2 p = vec2((c.r * (n - 1.0) + 0.5 + b0 * n) / (n * n), (c.g * (n - 1.0) + 0.5) / n);\n"
    "    vec3 lo = texture(lut_tex, p).rgb, hi = texture(lut_tex, p + vec2(b0 < n - 1.0 ? 1.0 / n : 0.0, 0.0)).rgb;\n"
    "    return mix(lo, hi, b - b0);\n"
    "}\n" PC_GLSL_POST_GRADE
    "void main() {\n"
    "    ivec2 p = ivec2(gl_FragCoord.xy) - origin;\n"
    "    vec4 c0 = texelFetch(image, p, 0);\n"
    "    if (c0.a > 9000.0) { color = vec4(c0.rgb, last_ != 0 ? 1.0 : c0.a); return; }\n"
    "    vec2 st = (vec2(p) + 0.5) / PG_LIT;\n" /* GL images count rows from the bottom */
    "    color = vec4(pg_pixel(c0, vec2(st.x, 1.0 - st.y), st, vec2(1.0, -1.0)), last_ != 0 ? 1.0 : 0.0);\n"
    "}\n";
static const char* bloom_fragment_source =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "out vec4 color;\n"
    "uniform sampler2D source;\n"
    "uniform sampler2D level;\n"
    "uniform vec4 p0, p1;\n" /* bloom.frag's push constants */
    PC_GLSL_POST_BLOOM
    "void main() {\n"
    "    vec2 st = vec2(uv.x, 1.0 - uv.y);\n"
    "    int mode = int(p0.z);\n"
    "    if (mode == 0 || mode == 1) color = vec4(pb_down(source, st, p0.xy, mode == 0, p1.xyz), 1.0);\n"
    "    else if (mode == 2) color = vec4((texture(level, st).rgb + pb_up(source, st, p0.xy, p1.w)) * p0.w, 1.0);\n"
    "    else color = vec4(pb_meter(source, st, p0.xy), 0.0, 1.0);\n"
    "}\n";
static GLuint grade_program, bloom_program;
static GLint u_grade_pg, u_grade_origin, u_grade_last, u_bloom_p0, u_bloom_p1;
static GLuint post_black, post_lut;
static unsigned post_lut_serial = ~0u;
#define BLOOM_LEVELS 8
static view_target bloom_down[BLOOM_LEVELS], bloom_up[BLOOM_LEVELS], meter_target;
static GLuint meter_buffers[3];
static int meter_pending[3], meter_next;

static int post_ready(void)
{
    static int state; /* 0 not yet, 1 ready, -1 failed */
    if (state)
        return state > 0;
    state = -1;
    grade_program = pc_gl_program(vertex_source, grade_fragment_source);
    bloom_program = pc_gl_program(vertex_source, bloom_fragment_source);
    if (!grade_program || !bloom_program) {
        fprintf(stderr, "[gl] post stack unavailable (its shaders did not build)\n");
        return 0;
    }
    u_grade_pg = gl_GetUniformLocation(grade_program, "pg_");
    u_grade_origin = gl_GetUniformLocation(grade_program, "origin");
    u_grade_last = gl_GetUniformLocation(grade_program, "last_");
    gl_UseProgram(grade_program);
    gl_Uniform1i(gl_GetUniformLocation(grade_program, "image"), 0);
    gl_Uniform1i(gl_GetUniformLocation(grade_program, "bloom_tex"), 1);
    gl_Uniform1i(gl_GetUniformLocation(grade_program, "lut_tex"), 2);
    u_bloom_p0 = gl_GetUniformLocation(bloom_program, "p0");
    u_bloom_p1 = gl_GetUniformLocation(bloom_program, "p1");
    gl_UseProgram(bloom_program);
    gl_Uniform1i(gl_GetUniformLocation(bloom_program, "source"), 0);
    gl_Uniform1i(gl_GetUniformLocation(bloom_program, "level"), 1);
    gl_UseProgram(0);
    static const unsigned char black[4] = { 0, 0, 0, 255 };
    post_black = lookup_texture(GL_RGBA8, GL_RGBA, black, 1, 1, GL_NEAREST);
    post_lut = lookup_texture(GL_RGBA8, GL_RGBA, black, 1, 1, GL_LINEAR);
    state = 1;
    return 1;
}

/* One bloom pass into `out` (w x h): mode, the source's texel size, the push values. */
static void bloom_pass(view_target* out, GLuint source, GLuint level, int mode, float tw, float th, float scale,
    const float p1[4])
{
    gl_BindFramebuffer(GL_FRAMEBUFFER, out->fbo);
    gl_Viewport(0, 0, out->w, out->h);
    gl_ActiveTexture(GL_TEXTURE1);
    gl_BindTexture(GL_TEXTURE_2D, level ? level : post_black);
    gl_ActiveTexture(GL_TEXTURE0);
    gl_BindTexture(GL_TEXTURE_2D, source);
    gl_Uniform4f(u_bloom_p0, tw, th, (float)mode, scale);
    gl_Uniform4fv(u_bloom_p1, 1, p1);
    gl_DrawArrays(GL_TRIANGLES, 0, 3);
}

/* The Gaussian bloom of the lit image `source` (w x h): the result's texture (half size), or 0. */
static GLuint gl_bloom(const pc_post_frame* pf, GLuint source, int w, int h)
{
    /* Levels: about 5 at size 1, more for a wider glow; each at least 2 x 2. */
    int levels = 5 + (int)lrintf(log2f(pf->bloom_size > 0.05f ? pf->bloom_size : 0.05f) * 1.5f);
    levels = levels < 2 ? 2 : levels > BLOOM_LEVELS ? BLOOM_LEVELS : levels;
    int lw[BLOOM_LEVELS], lh[BLOOM_LEVELS];
    for (int i = 0; i < levels; i++) {
        lw[i] = w >> (i + 1), lh[i] = h >> (i + 1);
        if (lw[i] < 2 || lh[i] < 2) {
            levels = i;
            break;
        }
        if (!ensure_view_target(&bloom_down[i], lw[i], lh[i]) || !ensure_view_target(&bloom_up[i], lw[i], lh[i]))
            return 0;
    }
    if (levels < 1)
        return 0;
    gl_UseProgram(bloom_program);
    float p1[4] = { pf->exposure_scale, pf->bloom_threshold, pf->bloom_knee, 1.0f };
    bloom_pass(&bloom_down[0], source, 0, 0, 1.0f / w, 1.0f / h, 1, p1);
    for (int i = 1; i < levels; i++)
        bloom_pass(&bloom_down[i], bloom_down[i - 1].texture, 0, 1, 1.0f / lw[i - 1], 1.0f / lh[i - 1], 1, p1);
    /* Up: each level its own downsample plus a tent of the one below; the top divided by the levels. */
    GLuint below = bloom_down[levels - 1].texture;
    for (int i = levels - 2; i >= 0; i--) {
        bloom_pass(&bloom_up[i], below, bloom_down[i].texture, 2, 1.0f / lw[i + 1], 1.0f / lh[i + 1],
            i == 0 ? 1.0f / levels : 1.0f, p1);
        below = bloom_up[i].texture;
    }
    return levels > 1 ? bloom_up[0].texture : bloom_down[0].texture;
}

/* Auto exposure: meter the lit image into a small image, read back a few frames later. */
static void gl_meter(GLuint source)
{
    if (!ensure_view_target(&meter_target, PC_POST_METER_W, PC_POST_METER_H))
        return;
    if (!meter_buffers[0])
        gl_GenBuffers(3, meter_buffers);
    size_t bytes = PC_POST_METER_W * PC_POST_METER_H * 4 * sizeof(float);
    int slot = meter_next;
    if (meter_pending[slot]) { /* written three frames ago */
        float texels[PC_POST_METER_W * PC_POST_METER_H * 4];
        gl_BindBuffer(GL_PIXEL_PACK_BUFFER, meter_buffers[slot]);
        gl_GetBufferSubData(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)bytes, texels);
        gl_BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        pc_post_meter_reduce(texels, PC_POST_METER_W, PC_POST_METER_H, 4);
    }
    gl_UseProgram(bloom_program);
    static const float p1[4] = { 1, 0, 0, 0 };
    bloom_pass(&meter_target, source, 0, 4, 1.0f / PC_POST_METER_W, 1.0f / PC_POST_METER_H, 1, p1);
    gl_BindBuffer(GL_PIXEL_PACK_BUFFER, meter_buffers[slot]);
    gl_BufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)bytes, NULL, GL_STREAM_READ);
    gl_ReadPixels(0, 0, PC_POST_METER_W, PC_POST_METER_H, GL_RGBA, GL_FLOAT, 0);
    gl_BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    meter_pending[slot] = 1;
    meter_next = (slot + 1) % 3;
}

/* The LUT strip (post_profile.cpp), uploaded when it changes. */
static void gl_lut_update(void)
{
    int size;
    unsigned serial;
    const unsigned char* strip = pc_post_lut(&size, &serial);
    if (serial == post_lut_serial)
        return;
    post_lut_serial = serial;
    gl_BindTexture(GL_TEXTURE_2D, post_lut);
    gl_PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (strip)
        gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size * size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, strip);
    gl_PixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

/* The grade pass: bloom and meter from `source` (the lit image, w x h), then the grade into whatever
 * stage_begin bound (origin: the output's corner; last: the window). */
static void gl_grade(const pc_post_frame* pf, GLuint source, int w, int h, int view_w, int view_h, int origin_x,
    int origin_y, int last)
{
    GLuint bloom = 0;
    GLint fbo = 0, viewport[4];
    gl_GetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    gl_GetIntegerv(GL_VIEWPORT, viewport);
    gl_ActiveTexture(GL_TEXTURE0);
    gl_BindTexture(GL_TEXTURE_2D, source);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    if (pf->bloom)
        bloom = gl_bloom(pf, source, w, h);
    if (pf->meter)
        gl_meter(source);
    pc_post_active_bloom = bloom ? PC_BLOOM_GAUSSIAN : 0;
    if (pf->lut)
        gl_lut_update();
    gl_BindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
    gl_Viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    static pc_post_gpu params;
    static const float identity[4] = { 1, 1, 0, 0 };
    pc_post_gpu_pack(&params, w, h, view_w, view_h, identity);
    if (!bloom)
        params.v[0][1] = 0;
    gl_UseProgram(grade_program);
    gl_Uniform4fv(u_grade_pg, PC_POST_GPU_VEC4S, &params.v[0][0]);
    gl_Uniform2i(u_grade_origin, origin_x, origin_y);
    gl_Uniform1i(u_grade_last, last);
    gl_ActiveTexture(GL_TEXTURE2);
    gl_BindTexture(GL_TEXTURE_2D, pf->lut ? post_lut : post_black);
    gl_ActiveTexture(GL_TEXTURE1);
    gl_BindTexture(GL_TEXTURE_2D, bloom ? bloom : post_black);
    gl_ActiveTexture(GL_TEXTURE0);
    gl_BindTexture(GL_TEXTURE_2D, source);
    gl_DrawArrays(GL_TRIANGLES, 0, 3);
}
