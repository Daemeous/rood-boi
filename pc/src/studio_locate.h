// The model studio's texel locator, the maths (studio.cpp holds the state and draws; no Dear ImGui here, so the maths can be tried
// alone): where a texel of a texture page lands on a posed model, and which texel is under a pixel of the picture.
//
// A model's textures are boxes of a few texture pages; a painted page (Aseprite: pc/tools/aseprite/, the sprite of one page and
// palette: `m<model>_p<page>_c<clut>`) is 256 x 256 texels, a texel (x, y) the page's pixel there. A triangle's corners carry the
// page's texture coordinates (u, v in texels, 0..255), so the texel's square [x, x + 1] x [y, y + 1] lies over each triangle that
// samples it, and the part of the triangle inside the square is the texel's footprint on the model (shared or mirrored coordinates:
// several triangles, several footprints).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace studio_locate {

struct Tri {
    float p[3][3];     // world positions (the pose)
    uint8_t uv[3][2];  // texture coordinates of the corners
    uint16_t page;     // the texture page (tpage & 31: the page's column and row, as the painted sprite's label names it)
    uint16_t clut;
    int polygon, quad; // the model's polygon it came from (a quad: its two triangles, 1 and 2)
    int mesh;          // which of the drawn models (an index of the capture)
};

// The view a picture was drawn with: column-major view and projection (clip w > 0 in front) and its window rectangle x, y, w, h.
struct View {
    float view[16], projection[16], rect[4];
    bool valid = false;
};

struct Hit {
    int tri;                  // index into the triangles
    float centre[3];          // the world point of the texel's centre (or the nearest point of the triangle to it)
    std::vector<float> ring;  // the footprint polygon, world xyz per corner (3 to 7 corners)
    bool on_screen = false;
    float screen[2] = { 0, 0 }; // the centre's window pixel
    float depth = 0;            // its depth (clip w)
    bool visible = false;       // nearest at its pixel (nothing in front, the triangle faces the eye)
    float area = 0;             // the footprint's area in texels (0..1)
};

// Every triangle that samples texel (x, y) of page `page` with palette `clut` (-1: any palette).
std::vector<Hit> locate(const std::vector<Tri>& tris, int page, int clut, int x, int y);

// The window pixel of a world point; false when it is behind the eye.
bool project(const View& v, const float world[3], float out[2], float* depth);

// The visible flag and the screen position of each hit (view valid), `tolerance` in world units: a hit is hidden when something is
// nearer at its pixel by more than that.
void shade(const std::vector<Tri>& tris, const View& v, std::vector<Hit>& hits, float tolerance);

struct Pick {
    bool hit = false;
    int tri = -1;
    int page = 0, clut = 0, x = 0, y = 0; // the texel
    float u = 0, v = 0;                   // the exact texture coordinates there
    float world[3] = { 0, 0, 0 };
    float depth = 0;
};
// The texel under window pixel (px, py): the nearest triangle there (one that faces the eye first, a back face only where nothing
// else is), its texture coordinates interpolated as the GPU does for the screen's perspective (the barycentre divided by w).
Pick pick(const std::vector<Tri>& tris, const View& v, float px, float py);

// The label of a page: "_p5_c0012" (the part after the model's key in a sprite's title).
std::string page_label(int page, int clut);
// Reads a sprite's title (`m<10 hex>_p<page>_c<4 hex>`, the model part optional) into page, clut and the model's key (0 when none).
bool parse_label(const std::string& label, int* page, int* clut, uint64_t* model);

} // namespace studio_locate
