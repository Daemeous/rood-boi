// Material channel maps (pc/docs/Materials.md): hand-made 8-bit maps over a
// texture, one value per texel, kept per texture by its content key (the
// fixture catalog's key: the same in every room and wherever the texture
// lands in VRAM) in a folder of channel images:
//
//   <materials>/<key>/<channel>.png     emissive, roughness, height, metal
//
// A map is the size of the texture's UV box (the exported reference's
// size), texel for texel (nearest, no filtering). Two layers: the shipped
// one (pc/assets/materials, committed, single-channel PNGs only) and the
// user's (materials/ in the user lighting directory), which wins per channel
// file. The loader is generic: it finds whatever channel files a texture
// has; consumers opt into individual channels. Files changed on disk are
// reloaded (poll(): mtime and size).
#ifndef ROOD_MATERIALS_H
#define ROOD_MATERIALS_H
#include <stdint.h>
#include <string>
#include <vector>

namespace materials {

struct Surface {
    float roughness = 1.0f; /* 1: fully rough */
    float specular = 0.0f;  /* fraction of dielectric F0 (0.04) */
    float metal = 0.0f;
    /* Relief (pc/docs/Materials.md "Height and normals"): estimate a height
     * from the texture's own colours when it has no height.png (still only
     * where the global auto_height switch is on), and a multiplier of the
     * global relief (bump_strength) for this surface. */
    bool auto_height = false;
    float bump = 1.0f;
};
/* Catalog presets and layered entries: absent or unmatched means Surface{}
 * and must never alter the old shading path. A map overrides roughness only.
 * clut is optional but required for keys that are not unique by themselves. */
struct Entry {
    std::string id, name, where, review, preset, key, clut;
    int w = 0, h = 0, zone = -1, room = -1;
    Surface value;
    /* Which fields the entry sets itself (1 roughness, 2 specular, 4
     * metal, 8 auto_height, 16 bump); the rest come from its preset. */
    unsigned explicit_fields = 0;
    bool user = false; /* from the user layer (materials.json) */
};
std::vector<Entry> catalog_entries();
Surface match(const std::string& key, int w, int h, const std::string& clut, int zone, int room);
/* The entry that applies (the last matching one), or nullptr: valid until
 * the catalog is next read. */
const Entry* find_match(const std::string& key, int w, int h, const std::string& clut, int zone, int room);
/* Whether any entry names a texture of this size (and so its key is worth
 * hashing), and whether any entry needs the palette's key. */
bool catalog_has_size(int w, int h);
/* Changes whenever either catalog file changed on disk (checks their stamps:
 * two stat calls). */
uint32_t catalog_generation();
/* The presets by name (shipped, then the user's), in name order. */
std::vector<std::pair<std::string, Surface>> catalog_presets();
/* The user layer (materials.json): its path, its entries as written, and
 * writing an entry into it (same id replaces) or removing one. The text of
 * one entry as the catalog writes it (Copy JSON for the catalog). */
std::string catalog_user_file();
/* The editor's unsaved edits (Details > Material): entries over both files
 * by id, live (they match and shade at once) until saved into the user
 * layer or reverted. `removed`: ids the user layer drops on save. */
void set_pending(const Entry& e);
void remove_pending(const std::string& id); /* drops the user layer's entry on save */
std::vector<Entry> pending_entries();
bool pending_dirty();
bool save_pending(std::string* error);
void revert_pending();
bool put_user_entry(const Entry& e, std::string* error);
bool remove_user_entry(const std::string& id, std::string* error);
std::string entry_json(const Entry& e);

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
 * the shipped layer): "emissive", "roughness", "height", "metal". */
bool known_channel(const std::string& name);
/* A channel's neutral value (an exported blank template): roughness 255
 * (rough), height 128 (flat, room to carve down or raise), else 0. */
uint8_t blank_value(const std::string& name);

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
void reload(); /* forget(), and read both catalog files again */

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
