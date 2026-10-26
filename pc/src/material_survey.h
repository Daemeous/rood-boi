// Which textures a view shows, and how much of it each covers: the material
// survey (pc/docs/Materials.md, "Surveying a room"). The textured triangles
// the native lists draw (room, characters, props; the same ones the texture
// picker and the material matching see) are rasterised on the CPU into a
// coarse grid of the tools' view (the game camera, or the free camera while
// it is on) with a depth test and the game's back-face rule, and each grid
// cell is attributed to the texture (content key, size, palette) its
// nearest triangle samples. Boxes of the same texture (the same key and
// palette, anywhere in VRAM) are merged. Editor and RPC only.
#ifndef ROOD_MATERIAL_SURVEY_H
#define ROOD_MATERIAL_SURVEY_H
#include <stdint.h>
#include <array>
#include <string>
#include <vector>

namespace material_survey {

struct Texture {
    std::string key, clut_key; /* content key (fixture_lib.py ikey), palette key ("" for 15-bit pages) */
    int w = 0, h = 0;
    unsigned tpage = 0, clut = 0; /* where it was found (the largest box's use) */
    uint8_t box[4] = { 0, 0, 0, 0 };
    std::vector<std::array<uint8_t, 4>> boxes; /* every box drawing it (u0, v0, u1, v1) */
    int cells = 0; /* grid cells it covers */
    std::string entry; /* the material entry that applies here ("": none) */
};
struct Result {
    bool valid = false;
    int grid_w = 0, grid_h = 0;
    int textured = 0, untextured = 0; /* covered cells */
    int zone = -1, room = -1;
    float eye[3] = { 0, 0, 0 }, forward[3] = { 0, 0, 1 }; /* the view's camera (world) */
    std::vector<Texture> textures; /* most cells first */
    std::vector<int> map; /* per cell (row-major): index into textures, -1 nothing, -2 untextured */
};
/* Survey the tools' view on a grid `grid_w` cells wide (the height follows
 * the view's aspect). `cull`: the facing that is drawn (1 the game's rule,
 * -1 the opposite, 0 both faces; for checking the rule against picks). */
bool survey(Result& out, int grid_w = 160, int cull = 1);

} // namespace material_survey
#endif
