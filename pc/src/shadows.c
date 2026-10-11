/* Point-light shadows: cube maps of the captured room geometry.
 *
 * Each shadow-casting light renders the room's world-space triangles into a
 * cube of R32F distances (distance to the light / its radius), 512 x 512 per
 * face, with standard GL cube-face orientations so texture(cube, direction)
 * looks them up directly. Maps are cached and redrawn only when the room or
 * the light changes. Up to PC_SHADOW_SLOTS lights cast shadows at once. */
#define _POSIX_C_SOURCE 200809L
#include "gl_api.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PC_SHADOW_SLOTS 4
#define SHADOW_SIZE 512

extern const float* pc_scene_positions(int* count);
extern int pc_scene_serial(void);
extern GLuint pc_gl_program(const char* vertex, const char* fragment);
extern const float* pc_hw_casters(int* count, int* serial);

static GLuint program, vertex_array, vertex_buffer, fbo, depth_buffer;
static GLuint caster_array, caster_buffer; /* characters and props, per frame */
static int cached_casters[PC_SHADOW_SLOTS], uploaded_casters = -1, caster_vertices;
static GLint u_face, u_light;
static GLuint cubes[PC_SHADOW_SLOTS];
static float cached[PC_SHADOW_SLOTS][4]; /* position, radius of the drawn map */
static int cached_serial[PC_SHADOW_SLOTS], uploaded_serial = -1, vertex_count, ready;

static const char* vertex_source =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_world;\n"
    "uniform mat4 face;\n" /* projection * face view * translate(-light) */
    "uniform vec4 light;\n"
    "out vec3 v_world;\n"
    "void main() { v_world = a_world; gl_Position = face * vec4(a_world, 1.0); }\n";
static const char* fragment_source =
    "#version 330 core\n"
    "in vec3 v_world;\n"
    "uniform vec4 light;\n" /* xyz, radius */
    "out vec4 distance;\n"
    "void main() { distance = vec4(length(v_world - light.xyz) / light.w); }\n";

static void init(void)
{
    ready = -1;
    program = pc_gl_program(vertex_source, fragment_source);
    if (!program)
        return;
    u_face = gl_GetUniformLocation(program, "face");
    u_light = gl_GetUniformLocation(program, "light");
    gl_GenVertexArrays(1, &vertex_array);
    gl_GenBuffers(1, &vertex_buffer);
    gl_BindVertexArray(vertex_array);
    gl_BindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    gl_EnableVertexAttribArray(0);
    gl_VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    gl_GenVertexArrays(1, &caster_array);
    gl_GenBuffers(1, &caster_buffer);
    gl_BindVertexArray(caster_array);
    gl_BindBuffer(GL_ARRAY_BUFFER, caster_buffer);
    gl_EnableVertexAttribArray(0);
    gl_VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    gl_BindVertexArray(0);
    gl_GenFramebuffers(1, &fbo);
    gl_GenRenderbuffers(1, &depth_buffer);
    gl_BindRenderbuffer(GL_RENDERBUFFER, depth_buffer);
    gl_RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, SHADOW_SIZE, SHADOW_SIZE);
    gl_GenTextures(PC_SHADOW_SLOTS, cubes);
    for (int slot = 0; slot < PC_SHADOW_SLOTS; slot++) {
        gl_BindTexture(GL_TEXTURE_CUBE_MAP, cubes[slot]);
        for (int face = 0; face < 6; face++)
            gl_TexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_R32F, SHADOW_SIZE, SHADOW_SIZE, 0,
                GL_RED, GL_FLOAT, NULL);
        gl_TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl_TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl_TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl_TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl_TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        cached_serial[slot] = -1;
    }
    ready = 1;
}

/* Column-major 4x4: projection(90 deg, near 1, far radius) * lookAt(face). */
static void face_matrix(int face, const float light[4], float out[16])
{
    static const float target[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    static const float up[6][3] = { { 0, -1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, -1, 0 }, { 0, -1, 0 } };
    const float* f = target[face];
    const float* u = up[face];
    float s[3] = { f[1] * u[2] - f[2] * u[1], f[2] * u[0] - f[0] * u[2], f[0] * u[1] - f[1] * u[0] };
    float v[3] = { s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0] };
    /* view rows: s, v, -f; translation -dot(row, light) */
    float view[3][4];
    for (int k = 0; k < 3; k++)
        view[0][k] = s[k], view[1][k] = v[k], view[2][k] = -f[k];
    for (int r = 0; r < 3; r++)
        view[r][3] = -(view[r][0] * light[0] + view[r][1] * light[1] + view[r][2] * light[2]);
    float n = 1, far = light[3] * 1.05f;
    float p22 = -(far + n) / (far - n), p23 = -2 * far * n / (far - n);
    memset(out, 0, 16 * sizeof(float));
    for (int c = 0; c < 4; c++) {
        out[c * 4 + 0] = view[0][c]; /* x scale 1 (90 degrees) */
        out[c * 4 + 1] = view[1][c];
        out[c * 4 + 2] = p22 * view[2][c] + (c == 3 ? p23 : 0);
        out[c * 4 + 3] = -view[2][c];
    }
}

/* Render (if needed) the cube for `slot` from `light` (xyz, radius). */
static void render(int slot, const float light[4])
{
    int serial = pc_scene_serial(), casters, caster_serial;
    const float* caster_positions = pc_hw_casters(&casters, &caster_serial);
    if (cached_serial[slot] == serial && cached_casters[slot] == caster_serial
        && !memcmp(cached[slot], light, sizeof cached[slot]))
        return;
    int count;
    const float* positions = pc_scene_positions(&count);
    if (!positions || !count)
        return;
    if (uploaded_casters != caster_serial) {
        gl_BindVertexArray(caster_array);
        gl_BindBuffer(GL_ARRAY_BUFFER, caster_buffer);
        gl_BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)casters * 9 * sizeof(float), caster_positions, GL_STREAM_DRAW);
        uploaded_casters = caster_serial;
        caster_vertices = casters * 3;
    }
    gl_BindVertexArray(vertex_array);
    if (uploaded_serial != serial) {
        gl_BindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
        gl_BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * 9 * sizeof(float), positions, GL_STATIC_DRAW);
        uploaded_serial = serial;
        vertex_count = count * 3;
    }
    gl_BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl_FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_buffer);
    gl_Viewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);
    gl_UseProgram(program);
    gl_Uniform4f(u_light, light[0], light[1], light[2], light[3]);
    gl_Enable(GL_DEPTH_TEST);
    gl_DepthFunc(GL_LESS);
    gl_DepthMask(GL_TRUE);
    gl_Disable(GL_BLEND);
    gl_Disable(GL_SCISSOR_TEST);
    for (int face = 0; face < 6; face++) {
        gl_FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
            cubes[slot], 0);
        static const GLenum one[1] = { GL_COLOR_ATTACHMENT0 };
        gl_DrawBuffers(1, one);
        static const float clear[4] = { 2, 2, 2, 2 }, far_depth = 1.0f;
        gl_ClearBufferfv(GL_COLOR, 0, clear); /* nothing there: beyond the radius */
        gl_ClearBufferfv(GL_DEPTH, 0, &far_depth);
        float matrix[16];
        face_matrix(face, light, matrix);
        gl_UniformMatrix4fv(u_face, 1, GL_FALSE, matrix);
        gl_BindVertexArray(vertex_array);
        gl_DrawArrays(GL_TRIANGLES, 0, vertex_count);
        if (caster_vertices) {
            gl_BindVertexArray(caster_array);
            gl_DrawArrays(GL_TRIANGLES, 0, caster_vertices);
        }
    }
    gl_Disable(GL_DEPTH_TEST);
    gl_BindVertexArray(0);
    gl_BindFramebuffer(GL_FRAMEBUFFER, 0);
    memcpy(cached[slot], light, sizeof cached[slot]);
    cached_serial[slot] = serial;
    cached_casters[slot] = caster_serial;
}

/* Assign shadow slots to the lights that want them (world xyz + radius) and
 * bring their cubes up to date. slot_out[i] = slot or -1. Returns how many. */
int pc_shadows_update(int count, const float world[][4], const int* wants, int* slot_out)
{
    if (!ready)
        init();
    int used = 0;
    for (int i = 0; i < count; i++) {
        slot_out[i] = -1;
        if (ready > 0 && wants[i] && used < PC_SHADOW_SLOTS) {
            render(used, world[i]);
            slot_out[i] = used++;
        }
    }
    return used;
}
/* Bind slot cubes to texture units first_unit .. first_unit + 3. */
void pc_shadows_bind(int first_unit)
{
    for (int slot = 0; slot < PC_SHADOW_SLOTS; slot++) {
        gl_ActiveTexture(GL_TEXTURE0 + first_unit + slot);
        gl_BindTexture(GL_TEXTURE_CUBE_MAP, ready > 0 ? cubes[slot] : 0);
    }
    gl_ActiveTexture(GL_TEXTURE0);
}
