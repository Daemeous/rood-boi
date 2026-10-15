// Picking math for the editor's view (editor_picking.h).
#include "editor_picking.h"

#include <math.h>

namespace rood_pick {
namespace {
/* The inverse of a column-major 4x4 matrix (cofactors); false when singular. */
bool invert(const double m[16], double out[16])
{
    double inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11]
        - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11]
        + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11]
        - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10]
        + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11]
        + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11]
        - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11]
        + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10]
        - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7]
        - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7]
        + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7]
        - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6]
        + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7]
        + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7]
        - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7]
        + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6]
        - m[8] * m[2] * m[5];
    double det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (fabs(det) < 1e-30)
        return false;
    for (int i = 0; i < 16; i++)
        out[i] = inv[i] / det;
    return true;
}
} // namespace

bool ray(const float view[16], const float projection[16], const float rect[4], float x, float y, float origin[3],
    float dir[3])
{
    if (rect[2] <= 0 || rect[3] <= 0)
        return false;
    double m[16], inv[16]; /* projection * view, in double: the game's far plane is 65536 */
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            double s = 0;
            for (int k = 0; k < 4; k++)
                s += (double)projection[k * 4 + r] * view[c * 4 + k];
            m[c * 4 + r] = s;
        }
    if (!invert(m, inv))
        return false;
    /* Normalized device coordinates as lights.cpp view_project maps them back. */
    double nx = (x - rect[0]) / rect[2] * 2 - 1, ny = 1 - (y - rect[1]) / rect[3] * 2;
    double ends[2][3];
    for (int e = 0; e < 2; e++) {
        double ndc[4] = { nx, ny, e ? 1.0 : -1.0, 1 }, w[4];
        for (int r = 0; r < 4; r++)
            w[r] = inv[r] * ndc[0] + inv[4 + r] * ndc[1] + inv[8 + r] * ndc[2] + inv[12 + r] * ndc[3];
        if (fabs(w[3]) < 1e-12)
            return false;
        for (int k = 0; k < 3; k++)
            ends[e][k] = w[k] / w[3];
    }
    double d[3] = { ends[1][0] - ends[0][0], ends[1][1] - ends[0][1], ends[1][2] - ends[0][2] };
    double length = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (length < 1e-9)
        return false;
    for (int k = 0; k < 3; k++) {
        origin[k] = (float)ends[0][k];
        dir[k] = (float)(d[k] / length);
    }
    return true;
}

float volume_hit(const float origin[3], const float dir[3], const float centre[3], const float size[3], float yaw,
    bool sphere)
{
    float half[3] = { size[0] * 0.5f, size[1] * 0.5f, size[2] * 0.5f };
    if (half[0] <= 0 || half[1] <= 0 || half[2] <= 0)
        return -1;
    float a = yaw * 3.14159265f / 180.0f, c = cosf(a), s = sinf(a);
    float p[3] = { origin[0] - centre[0], origin[1] - centre[1], origin[2] - centre[2] };
    /* Into the volume's frame (the inverse of atmosphere.cpp's corner()), its half size to 1. */
    float o[3] = { (c * p[0] - s * p[2]) / half[0], p[1] / half[1], (s * p[0] + c * p[2]) / half[2] };
    float d[3] = { (c * dir[0] - s * dir[2]) / half[0], dir[1] / half[1], (s * dir[0] + c * dir[2]) / half[2] };
    if (sphere) {
        float qa = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        float qb = 2 * (o[0] * d[0] + o[1] * d[1] + o[2] * d[2]);
        float qc = o[0] * o[0] + o[1] * o[1] + o[2] * o[2] - 1;
        if (qc <= 0 || qa <= 0) /* inside it */
            return -1;
        float disc = qb * qb - 4 * qa * qc;
        if (disc < 0)
            return -1;
        float t = (-qb - sqrtf(disc)) / (2 * qa);
        return t >= 0 ? t : -1;
    }
    float t0 = -1e30f, t1 = 1e30f;
    for (int k = 0; k < 3; k++) {
        if (fabsf(d[k]) < 1e-12f) {
            if (o[k] < -1 || o[k] > 1)
                return -1;
            continue;
        }
        float a0 = (-1 - o[k]) / d[k], a1 = (1 - o[k]) / d[k];
        if (a0 > a1) {
            float swap = a0;
            a0 = a1, a1 = swap;
        }
        t0 = fmaxf(t0, a0);
        t1 = fminf(t1, a1);
        if (t0 > t1)
            return -1;
    }
    return t0 >= 0 ? t0 : -1; /* behind it, or inside it */
}

bool in_rect(float x, float y, float x0, float y0, float x1, float y1)
{
    return x >= fminf(x0, x1) && x <= fmaxf(x0, x1) && y >= fminf(y0, y1) && y <= fmaxf(y0, y1);
}
} // namespace rood_pick
