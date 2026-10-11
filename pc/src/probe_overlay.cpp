// Probe GI (probe_gi.inc) in the editor: the settings panel, the probes drawn
// over the view, and the lights at their base intensities (light actors'
// animation off) that the probes are traced with.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "imgui.h"
#include "lights_internal.h"
#include "probe_gi.h"

extern "C" {
int pc_lights_world(float position[][4], float color[][4], float direction[][4], float extent_right[][4],
    float extent_up[][4], int shadows[], int max);
int pc_probe_snapshot(float (*position)[4], float (*color)[4], int max, const float eye[3]);
extern int pc_gi_bounces;

/* The renderers' lights with light actors' flicker and pulse off: the probes
 * trace these, and scale by current / base per light each frame. */
int pc_probe_lights_base(float position[][4], float color[][4], float direction[][4], float right[][4], float up[][4],
    int shadows[], int max)
{
    int saved = rood_lights::animate;
    rood_lights::animate = 0;
    int n = pc_lights_world(position, color, direction, right, up, shadows, max);
    rood_lights::animate = saved;
    return n;
}

void pc_probe_panel(void)
{
    if (pc_gi_method != PC_GI_PROBES)
        return;
    ImGui::SliderFloat("Probe spacing", &pc_probe_spacing, 32, 256, "%.0f units");
    ImGui::SliderInt("Rays per probe", &pc_probe_rays, 16, 256);
    ImGui::SliderFloat("Probe hysteresis", &pc_probe_hysteresis, 0.8f, 0.99f, "%.3f");
    ImGui::SliderInt("Probe budget", &pc_probe_budget, 256, 8192);
    ImGui::Combo("Show probes", &pc_probe_show, "Off\0Active\0Active and inactive\0");
    pc_probe_stats s;
    pc_probe_get_stats(&s);
    static const char* const backends[] = { "-", "ray queries", "compute BVH (Vulkan)", "compute BVH (OpenGL)" };
    if (s.probes)
        ImGui::TextDisabled("%d x %d x %d = %d probes (%d active), %s, %u updates%s", s.counts[0], s.counts[1],
            s.counts[2], s.probes, s.active, backends[s.backend < 0 || s.backend > 3 ? 0 : s.backend], s.converged,
            pc_gi_bounces >= 2 ? ", multi-bounce" : "");
    else
        ImGui::TextDisabled("No probes yet (needs a room and some light to bounce)");
}

/* Probes as small spheres: filled with their irradiance toward the camera,
 * scaled to the view; inactive ones (pc_probe_show 2) as red outlines. */
void pc_probe_draw(const float view[16], const float projection[16], const float rect[4], void* draw_list)
{
    if (!pc_probe_show || pc_gi_method != PC_GI_PROBES)
        return;
    pc_probe_stats s;
    pc_probe_get_stats(&s);
    if (!s.valid || s.probes <= 0)
        return;
    static float (*position)[4], (*color)[4];
    static int capacity;
    if (capacity < s.probes) {
        delete[] position;
        delete[] color;
        capacity = s.probes;
        position = new float[capacity][4];
        color = new float[capacity][4];
    }
    /* The eye: where the column-major view matrix maps to the origin, R e + t = 0
     * (R need not be orthonormal: solved with the inverse through cofactors). */
    float m[3][3], eye[3];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            m[r][c] = view[c * 4 + r];
    float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
              + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (fabsf(det) < 1e-12f)
        return;
    for (int k = 0; k < 3; k++) { /* Cramer's rule on R e = -t */
        float a[3][3];
        memcpy(a, m, sizeof a);
        for (int r = 0; r < 3; r++)
            a[r][k] = -view[12 + r];
        eye[k] = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
                     + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0])) / det;
    }
    int n = pc_probe_snapshot(position, color, capacity, eye);
    (void)draw_list;
    /* Over the view (the background list can sit under the view's window), clipped to it. */
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->PushClipRect(ImVec2(rect[0], rect[1]), ImVec2(rect[0] + rect[2], rect[1] + rect[3]), true);
    float radius_world = 0.12f * fminf(s.spacing[0], fminf(s.spacing[1], s.spacing[2]));
    for (int i = 0; i < n; i++) {
        bool active = position[i][3] > 0.5f;
        if (!active && pc_probe_show < 2)
            continue;
        float v[4], c[4];
        for (int r = 0; r < 4; r++)
            v[r] = view[r] * position[i][0] + view[4 + r] * position[i][1] + view[8 + r] * position[i][2] + view[12 + r];
        for (int r = 0; r < 4; r++)
            c[r] = projection[r] * v[0] + projection[4 + r] * v[1] + projection[8 + r] * v[2] + projection[12 + r] * v[3];
        if (c[3] <= 1e-3f)
            continue;
        float x = rect[0] + (c[0] / c[3] * 0.5f + 0.5f) * rect[2], y = rect[1] + (0.5f - c[1] / c[3] * 0.5f) * rect[3];
        /* Its radius on screen: project a point offset along the view's up row. */
        float up[3] = { view[1], view[5], view[9] }, ul = sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]), c2[4];
        float q[3] = { position[i][0] + up[0] / ul * radius_world, position[i][1] + up[1] / ul * radius_world,
            position[i][2] + up[2] / ul * radius_world };
        for (int r = 0; r < 4; r++)
            v[r] = view[r] * q[0] + view[4 + r] * q[1] + view[8 + r] * q[2] + view[12 + r];
        for (int r = 0; r < 4; r++)
            c2[r] = projection[r] * v[0] + projection[4 + r] * v[1] + projection[8 + r] * v[2] + projection[12 + r] * v[3];
        if (c2[3] <= 1e-3f)
            continue;
        float r = fabsf(c2[1] / c2[3] - c[1] / c[3]) * 0.5f * rect[3];
        r = r < 2.5f ? 2.5f : r;
        if (x < rect[0] - r || y < rect[1] - r || x > rect[0] + rect[2] + r || y > rect[1] + rect[3] + r)
            continue;
        if (!active) {
            draw->AddCircle(ImVec2(x, y), r, IM_COL32(230, 60, 60, 200), 16, 1.5f);
            continue;
        }
        /* Tone-map the irradiance for display (the field is linear, often > 1). */
        int rgb[3];
        for (int k = 0; k < 3; k++) {
            float t = color[i][k] / (1.0f + color[i][k]);
            rgb[k] = (int)(255.0f * sqrtf(t < 0 ? 0 : t));
        }
        draw->AddCircleFilled(ImVec2(x, y), r, IM_COL32(rgb[0], rgb[1], rgb[2], 235), 20);
        draw->AddCircle(ImVec2(x, y), r, IM_COL32(20, 20, 20, 160), 20, 1.0f);
        draw->AddCircleFilled(ImVec2(x - r * 0.35f, y - r * 0.35f), r * 0.2f, IM_COL32(255, 255, 255, 60), 8); /* a highlight */
    }
    draw->PopClipRect();
}
}
