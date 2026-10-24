/* Runtime window source distributions. No game texels are serialized.
 * Kept renderer-independent so mapping/energy/sampling have synthetic tests. */
#ifndef ROOD_WINDOW_PATTERN_H
#define ROOD_WINDOW_PATTERN_H
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>
namespace rood_window {
struct Face { std::string key; unsigned polygon = 0; int width = 1, height = 1; float v[3][4]; }; // key optionally includes @palette
inline std::vector<Face> faces(const std::string& text)
{
    std::vector<Face> out;
    std::istringstream all(text);
    std::string record;
    while (std::getline(all, record, ';')) {
        std::replace(record.begin(), record.end(), ',', ' ');
        std::istringstream in(record);
        Face f{};
        bool ok = bool(in >> f.key);
        auto hash = f.key.find('#');
        if (hash != std::string::npos) {
            char* end = nullptr;
            const char* start = f.key.c_str() + hash + 1;
            unsigned long polygon = std::strtoul(start, &end, 10);
            ok = ok && end != start && *end == 0 && polygon <= 0xffffffffUL;
            f.polygon = (unsigned)polygon;
            f.key.resize(hash);
        }
        ok = ok && (f.key.size() == 10 || (f.key.size() == 21 && f.key[10] == '@'));
        for (auto& vertex : f.v)
            for (float& x : vertex)
                ok = bool(in >> x) && std::isfinite(x) && ok;
        if (ok) out.push_back(f);
    }
    return out;
}
inline bool uv(const Face& f, float x, float y, float& u, float& v)
{
    float ax = f.v[1][0] - f.v[0][0], ay = f.v[1][1] - f.v[0][1];
    float bx = f.v[2][0] - f.v[0][0], by = f.v[2][1] - f.v[0][1];
    float det = ax * by - ay * bx;
    if (std::abs(det) < 1e-10f) return false;
    x -= f.v[0][0]; y -= f.v[0][1];
    float a = (x * by - y * bx) / det, b = (ax * y - ay * x) / det;
    if (a < -1e-5f || b < -1e-5f || a + b > 1.00001f) return false;
    u = f.v[0][2] + a * (f.v[1][2] - f.v[0][2]) + b * (f.v[2][2] - f.v[0][2]);
    v = f.v[0][3] + a * (f.v[1][3] - f.v[0][3]) + b * (f.v[2][3] - f.v[0][3]);
    return true;
}
struct Vertex { float c[4]; };
struct Polygon { Vertex v[16]; int n = 0; };
inline Polygon clip(const Polygon& p, int axis, float edge, bool above)
{
    Polygon out;
    if (!p.n) return out;
    Vertex a = p.v[p.n - 1];
    for (int i = 0; i < p.n; ++i) {
        Vertex b = p.v[i];
        float da = above ? a.c[axis] - edge : edge - a.c[axis];
        float db = above ? b.c[axis] - edge : edge - b.c[axis];
        if ((da >= 0) != (db >= 0)) {
            float t = da / (da - db);
            Vertex q;
            for (int k = 0; k < 4; ++k) q.c[k] = a.c[k] + t * (b.c[k] - a.c[k]);
            if (out.n < 16) out.v[out.n++] = q;
        }
        if (db >= 0 && out.n < 16) out.v[out.n++] = b;
        a = b;
    }
    return out;
}
inline float area(const Polygon& p)
{
    float sum = 0;
    for (int i = 0; i < p.n; ++i) {
        const auto& a = p.v[i]; const auto& b = p.v[(i + 1) % p.n];
        sum += a.c[0] * b.c[1] - a.c[1] * b.c[0];
    }
    return std::abs(sum) * .5f;
}
/* Conservative integration: clip source triangles against grid cells AND
 * every intersected source texel. Thin translated bars cannot fall between
 * a fixed sampling lattice. The GPU cells contain exact area averages of
 * nearest-filtered texels (piecewise constant); finer grids preserve more
 * spatial detail. Normalize only the PDF, returning original mean radiance. */
template<class Texel> float distribution(const std::vector<Face>& fs, int side, float cdf[256], Texel texel)
{
    std::fill(cdf, cdf + 256, 0.0f);
    side = std::max(1, std::min(16, side));
    for (const Face& f : fs) {
        Polygon tri; tri.n = 3;
        for (int i = 0; i < 3; ++i) for (int k = 0; k < 4; ++k) tri.v[i].c[k] = f.v[i][k];
        int width = std::max(1, f.width), height = std::max(1, f.height);
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
            Polygon cell = clip(clip(clip(clip(tri, 0, float(x) / side, true),
                0, float(x + 1) / side, false), 1, float(y) / side, true), 1, float(y + 1) / side, false);
            if (area(cell) <= 0) continue;
            float u0 = 1, v0 = 1, u1 = 0, v1 = 0;
            for (int k = 0; k < cell.n; ++k) {
                u0 = std::min(u0, cell.v[k].c[2]); u1 = std::max(u1, cell.v[k].c[2]);
                v0 = std::min(v0, cell.v[k].c[3]); v1 = std::max(v1, cell.v[k].c[3]);
            }
            int ix0 = std::clamp((int)std::floor(u0 * width), 0, width - 1);
            int iy0 = std::clamp((int)std::floor(v0 * height), 0, height - 1);
            int ix1 = std::clamp((int)std::floor(u1 * width), 0, width - 1);
            int iy1 = std::clamp((int)std::floor(v1 * height), 0, height - 1);
            for (int ty = iy0; ty <= iy1; ++ty) for (int tx = ix0; tx <= ix1; ++tx) {
                Polygon tex = clip(clip(clip(clip(cell, 2, float(tx) / width, true),
                    2, float(tx + 1) / width, false), 3, float(ty) / height, true), 3, float(ty + 1) / height, false);
                float share = area(tex);
                if (share <= 0) continue;
                float rgb[3] = {};
                texel(f, (tx + .5f) / width, (ty + .5f) / height, rgb);
                float w = rgb[0] * .299f + rgb[1] * .587f + rgb[2] * .114f;
                if (std::isfinite(w)) cdf[y * side + x] += share * std::max(0.0f, w);
            }
        }
    }
    float sum = 0;
    for (int i = 0; i < side * side; ++i) { sum += cdf[i]; cdf[i] = sum; }
    if (sum <= 0) return 0;
    for (int i = 0; i < side * side; ++i) cdf[i] /= sum;
    return sum;
}
} // namespace rood_window
#endif
