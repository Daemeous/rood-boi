#include "window_pattern.h"
#include "light_list.h"
#include <cassert>
#include <cstdio>
#include <cstddef>

int main()
{
    static_assert(sizeof(pc_light_gpu) == 1152, "std430 Light stride");
    static_assert(offsetof(pc_light_gpu, pattern_cdf) == 112, "std430 CDF offset");
    const char* mapping = "0123456789,0,0,0,0,1,0,1,0,0,1,0,1;"
                          "0123456789,1,0,1,0,1,1,1,1,0,1,0,1";
    auto fs = rood_window::faces(mapping);
    assert(fs.size() == 2);
    for (auto& f : fs) f.width = f.height = 8;
    auto qualified = rood_window::faces("0123456789@abcdef0123#1234,0,0,0,0,1,0,1,0,0,1,0,1");
    assert(qualified.size() == 1 && qualified[0].key == "0123456789@abcdef0123" && qualified[0].polygon == 1234);
    assert(rood_window::faces("0123456789#oops,0,0,0,0,1,0,1,0,0,1,0,1").empty());
    float u, v;
    assert(rood_window::uv(fs[0], .2f, .3f, u, v));
    assert(std::abs(u - .2f) < 1e-6f && std::abs(v - .3f) < 1e-6f);
    assert(!rood_window::uv(fs[0], 1, 1, u, v));
    assert(rood_window::faces("bad;0123456789,0,0").empty());
    auto degenerate = fs[0];
    for (auto& vertex : degenerate.v) vertex[0] = 0;
    assert(!rood_window::uv(degenerate, 0, .2f, u, v));
    float cdf[256];
    auto white = [](const rood_window::Face&, float, float, float rgb[3]) { rgb[0] = rgb[1] = rgb[2] = 1; };
    assert(std::abs(rood_window::distribution(fs, 8, cdf, white) - 1.0f) < 1e-6f);
    for (int i = 0; i < 64; ++i) assert(std::abs(cdf[i] - (i + 1) / 64.f) < 1e-6f);
    // Synthetic leaded pane: two completely black vertical columns. Zero
    // probability there, uniform physical cell extent on the bright cells.
    auto bars = [](const rood_window::Face&, float x, float, float rgb[3]) {
        int cell = int(x * 8);
        rgb[0] = rgb[1] = rgb[2] = cell == 2 || cell == 5 ? 0 : 1;
    };
    assert(std::abs(rood_window::distribution(fs, 8, cdf, bars) - .75f) < 1e-6f);
    float previous = 0;
    for (int i = 0; i < 64; ++i) {
        float mass = cdf[i] - previous;
        assert(std::abs(mass - ((i % 8 == 2 || i % 8 == 5) ? 0 : 1.f / 48)) < 1e-6f);
        previous = cdf[i];
    }
    assert(cdf[63] == 1);
    // Narrowing the transmissive area reduces total energy; the normalized
    // PDF must not be mistaken for a unit-power emitter.
    auto dim = [](const rood_window::Face&, float, float, float rgb[3]) { rgb[0] = rgb[1] = rgb[2] = .25f; };
    float dim_cdf[256];
    assert(std::abs(rood_window::distribution(fs, 8, dim_cdf, dim) - .25f) < 1e-6f);
    for (int i = 0; i < 64; ++i) assert(std::abs(dim_cdf[i] - (i + 1) / 64.f) < 1e-6f);
    // Same inverse CDF as the shader: no source points on the opaque bars;
    // samples spread across the finite emitting area, rather than point lights.
    int counts[64] = {};
    float minx = 1, maxx = 0;
    for (int s = 0; s < 4800; ++s) {
        float r = (s + .5f) / 4800, prev = 0;
        for (int i = 0; i < 64; ++i) {
            if (r < cdf[i] && cdf[i] > prev) {
                ++counts[i];
                float x = (i % 8 + (r - prev) / (cdf[i] - prev)) / 8;
                minx = std::min(minx, x); maxx = std::max(maxx, x);
                break;
            }
            prev = cdf[i];
        }
    }
    for (int i = 0; i < 64; ++i) assert(counts[i] == ((i % 8 == 2 || i % 8 == 5) ? 0 : 100));
    assert(minx < .01 && maxx > .99);
    auto thin_faces = fs;
    for (auto& f : thin_faces) { f.width = 31; f.height = 1; }
    // Every translation of a one-texel mullion, including positions between
    // the old 16x16 sample lattice, removes exactly its physical area.
    for (int bar = 0; bar < 31; ++bar) {
        auto thin = [bar](const rood_window::Face&, float x, float, float rgb[3]) {
            rgb[0] = rgb[1] = rgb[2] = int(x * 31) == bar ? 0 : 1;
        };
        float mean = rood_window::distribution(thin_faces, 8, dim_cdf, thin);
        assert(std::abs(mean - 30.0f / 31) < 1e-5f);
    }
    auto black = [](const rood_window::Face&, float, float, float rgb[3]) { rgb[0] = rgb[1] = rgb[2] = 0; };
    assert(!rood_window::distribution(fs, 8, cdf, black));
    assert(!rood_window::distribution({}, 8, cdf, white));
    // Flipped UVs retain their orientation.
    for (auto& f : fs) for (auto& vertex : f.v) vertex[2] = 1 - vertex[2];
    assert(rood_window::uv(fs[0], .2f, .3f, u, v) && std::abs(u - .8f) < 1e-6f);
    puts("PASS window source mapping, dark cells, normalized power, finite-area sampling, std430 layout");
}
