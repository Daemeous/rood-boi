// Material channel maps (pc/docs/Materials.md): hand-made 8-bit maps over a
// texture, one value per texel, kept per texture by its content key (the
// fixture catalog's key: the same in every room and wherever the texture
// lands in VRAM) in a folder of channel images:
//
//   <materials>/<key>/<channel>.png     emissive now; height, roughness later
//
// A map is the size of the texture's UV box (the exported reference's
// size), texel for texel (nearest, no filtering). Two layers: the shipped
// one (pc/assets/materials, committed, single-channel PNGs only) and the
// user's (materials/ in the user lighting directory), which wins per channel
// file. The loader is generic: it finds whatever channel files a texture
// has; emissive.cpp is the only consumer so far. Files changed on disk are
// reloaded (poll(): mtime and size).
#ifndef ROOD_MATERIALS_H
#define ROOD_MATERIALS_H
#include <stdint.h>
#include <string>
#include <vector>

namespace materials {

struct Channel {
    int w = 0, h = 0;
    std::vector<uint8_t> values; /* row-major, w * h: 0..255 */
    std::string path;
    bool shipped = false; /* from the shipped layer (else the user's) */
};

/* Where the layers and the exports live (relative to the repository root,
 * the game's working directory):
 *   user     $ROOD_MATERIALS, else materials/ in the user lighting
 *            directory ($ROOD_LIGHTS or pc/user/lighting)
 *   shipped  $ROOD_MATERIALS_SHIPPED, else pc/assets/materials
 *   exports  $ROOD_MATERIAL_EXPORTS, else pc/exports/materials (gitignored:
 *            the exported references are the game's art, never committed) */
std::string user_dir();
std::string shipped_dir();
std::string export_dir();

/* The channels the loader knows how to use (and the publish guard allows in
 * the shipped layer): "emissive". */
bool known_channel(const std::string& name);

/* A texture's channel map (the user's over the shipped one), or nullptr. */
const Channel* channel(const std::string& key, const std::string& name);
/* The channel files a texture has: name, and whether the user's layer has it. */
struct Present {
    std::string name, path;
    bool user;
};
std::vector<Present> channels(const std::string& key);
/* Changes whenever a channel file looked at so far changed on disk (or
 * appeared, or went); poll() checks (cheap: a stat per file). */
uint32_t generation();
void poll();
void forget(); /* reload everything on the next look */

/* Writes a channel into the user layer (a grayscale PNG). */
bool write_channel(const std::string& key, const std::string& name, int w, int h, const uint8_t* values,
    std::string* error);
/* Reads a painted PNG (grayscale, or its alpha when that is used), w x h,
 * into the user layer's channel. */
bool import_channel(const std::string& key, const std::string& name, const std::string& source, int w, int h,
    std::string* error);
/* The texture's reference (rgba, w x h) into `dir`/<key>/reference.png
 * (default: export_dir()), and an empty template for the channel in the
 * user layer when it has none. Refuses any directory inside pc/assets or
 * the shipped layer. `written`: the files written. */
bool export_reference(const std::string& key, int w, int h, const uint8_t* rgba, const std::string& dir,
    const std::string& channel_name, std::vector<std::string>* written, std::string* error);

/* PNG files: 8-bit grayscale or RGBA out; anything SDL reads in (as
 * grayscale: the alpha when it is used, else the luminance). */
bool read_png_gray(const std::string& path, int* w, int* h, std::vector<uint8_t>& out, std::string* error);
bool write_png(const std::string& path, int w, int h, int channels, const uint8_t* data, std::string* error);

} // namespace materials
#endif
