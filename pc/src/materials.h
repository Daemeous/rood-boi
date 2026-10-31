// Material channel maps (pc/docs/Materials.md): hand-made 8-bit maps over a
// texture, one value per texel, kept per texture by its content key (the
// fixture catalog's key: the same in every room and wherever the texture
// lands in VRAM) in a folder of channel images:
//
//   <materials>/<key>/<channel>.png     emissive, roughness, height, metal
//
// A map is the size of the texture's UV box (the exported reference's
// size), texel for texel (nearest, no filtering). Three layers: the baked
// one (pc/build/materials: derived from the textures by
// pc/tools/material_bake.py, local only, never committed; used while
// pc_materials_baked is on), the shipped one (pc/assets/materials,
// committed, single-channel PNGs only) over it and the user's (materials/ in
// the user lighting directory) over both, per channel file. The loader is generic: it finds whatever channel files a texture
// has; consumers opt into individual channels. Files changed on disk are
// reloaded (poll(): mtime and size).
#ifndef ROOD_MATERIALS_H
#define ROOD_MATERIALS_H
#include <stdint.h>
#include <map>
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
 * clut is optional but required for keys that are not unique by themselves.
 * A palette entry has no key (and w, h 0 unless it names a size): it matches
 * every texture drawn with palette `clut`, where no entry names the texture
 * itself (pc/docs/Materials.md, Palette entries). */
struct Entry {
    std::string id, name, where, review, preset, key, clut;
    int w = 0, h = 0, zone = -1, room = -1;
    /* scope {"rooms": [[zone, room], ...]}: the rooms when it names more
     * than one (zone/room are then the first); empty for one room or none. */
    std::vector<std::pair<int, int>> rooms;
    Surface value;
    /* Which fields the entry sets itself (1 roughness, 2 specular, 4
     * metal, 8 auto_height, 16 bump); the rest come from its preset. */
    unsigned explicit_fields = 0;
    /* The instance's parent when it is not the bare preset named by `preset`
     * (an instance id: pc/docs/plans/2026-10-30-master-materials-plan.md);
     * empty: the parent is the preset (or none). */
    std::string parent;
    bool user = false; /* from the user layer (materials.json) */
    bool preview = false; /* a temporary preview (set_preview): never saved */
    /* Whether the entry applies in this room (unscoped: everywhere). */
    bool in_scope(int z, int r) const
    {
        if (!rooms.empty()) {
            for (const auto& p : rooms)
                if (p.first == z && p.second == r) return true;
            return false;
        }
        return zone < 0 || (zone == z && room == r);
    }
    /* Scope it to one room (zone < 0: everywhere). */
    void set_room(int z, int r) { zone = z, room = z < 0 ? -1 : r, rooms.clear(); }
};
std::vector<Entry> catalog_entries();

/* ---- Master materials and instances (plan 2026-10-30-master-materials-plan.md) ----
 * A master is a shader exposing named parameters (this part: the one
 * standard surface, the existing planes path; part 4 generates masters from
 * graphs: one more file under masters_dir()). An instance is a master, an
 * optional parent instance, the parameter values it sets itself, and an
 * optional assignment (a texture key or a palette, scoped as entries are).
 * The catalog files are read as instances: every preset is a root instance
 * (id = its name), every entry an instance whose parent is its preset and
 * whose own values are its explicit fields (nothing is rewritten on read).
 * The Entry list above is derived from the assigned instances (resolved
 * values, explicit_fields = own values), so everything that shades reads
 * exactly what it did. */
struct MasterParam {
    std::string name, label, tip, group;
    bool is_bool = false;
    /* A colour (type "color", part 4's promoted colour knobs): three values 0..1 kept as `<name>.r`,
     * `<name>.g`, `<name>.b` wherever values are (an instance's own, the resolved map); written as
     * [r, g, b] in the files. Set and reset as one. */
    bool is_color = false;
    float def = 0, lo = 0, hi = 1;
    float color[3] = { 1, 1, 1 }; /* a colour's default */
};
/* A colour parameter's component keys (".r", ".g", ".b" after the name). */
extern const char* const COLOR_PARTS[3];
struct MasterSlot {
    std::string name, label, channel; /* channel: the map file ("" the game texel) */
};
struct Master {
    std::string name, shader, path; /* path: "" for the built-in description */
    std::vector<MasterParam> params;
    std::vector<MasterSlot> slots;
    const MasterParam* param(const std::string& name) const;
};
std::string masters_dir(); /* $ROOD_MATERIAL_MASTERS, else masters/ beside the shipped catalog file */
std::string graphs_dir(); /* the user's graph masters (part 4): $ROOD_MATERIAL_GRAPHS, else <user maps layer>/masters */
void masters_changed(); /* a master file was written or removed (part 4's editor): read the folders again at once */
const Master* master(const std::string& name); /* nullptr: unknown */
std::vector<std::string> master_names(); /* standard_surface first */

struct Instance {
    std::string id, name, where, review;
    std::string master = "standard_surface";
    std::string parent; /* an instance id, or none */
    std::map<std::string, float> own; /* the values it sets itself (bools 0 / 1) */
    /* The assignment (as Entry's match and scope); none: an unassigned
     * instance (a preset, or a user-made one with a parent). */
    bool assigned = false;
    std::string key, clut;
    int w = 0, h = 0, zone = -1, room = -1;
    std::vector<std::pair<int, int>> rooms;
    bool root = false; /* read from a file's `presets` (written back there) */
    bool user = false, preview = false, pending = false;
    bool in_scope(int z, int r) const;
    void set_room(int z, int r) { zone = z, room = z < 0 ? -1 : r, rooms.clear(); }
};
/* The instances of both files, the unsaved edits and the previews, in file
 * order (shipped, then the user's; the later file wins per id). */
std::vector<Instance> instances();
const Instance* find_instance(const std::string& id); /* valid until the catalog is next read */
/* Every parameter of the instance's master resolved (own, else the parent's,
 * ..., else the master's default); `chain`: the ids walked, the instance
 * first. False with `error` when the id, the master or a parent is unknown
 * or the parents loop. */
bool resolve(const std::string& id, std::map<std::string, float>& out, std::vector<std::string>* chain,
    std::string* error);
/* Resolve an instance that is not (yet) in the catalog (its parents are). */
bool resolve_instance(const Instance& i, std::map<std::string, float>& out, std::vector<std::string>* chain,
    std::string* error);
/* Which ancestor sets `name` for the instance ("" the master's default, the id itself for an own value). */
std::string value_source(const std::string& id, const std::string& name);
Surface surface_of(const std::map<std::string, float>& values);
/* The Entry an assigned instance derives (value = resolved; explicit_fields =
 * its own standard values); false when it does not resolve. */
bool entry_of(const Instance& i, Entry& out, std::string* error);
Instance instance_of(const Entry& e); /* the reverse (parent = e.parent, else e.preset) */
/* The unsaved layer, by instance: as set_pending / remove_pending below. */
void set_pending_instance(const Instance& i);
std::vector<Instance> pending_instances();
/* Writes an instance into the user layer (a root goes into `presets`, the
 * rest into `entries`; the file keeps its layout and its format unless the
 * instance needs format 2: a parent that is not a preset, another master, or
 * no assignment). */
bool put_user_instance(const Instance& i, std::string* error);
std::string instance_json(const Instance& i); /* one entry as the catalog writes it */
/* The user layer rewritten as every instance it holds would be written now
 * (its migration made explicit), into `path` (never the user layer itself
 * unless `path` is it); counts of presets and entries written. */
bool write_migrated(const std::string& path, int* presets, int* entries, std::string* error);
/* Problems the catalog has (unknown parents, loops, id clashes between a
 * preset and an entry, invalid entries dropped on read): one line each. */
std::vector<std::string> catalog_problems();
Surface match(const std::string& key, int w, int h, const std::string& clut, int zone, int room);
/* The entry that applies (the last matching one), or nullptr: valid until
 * the catalog is next read. */
const Entry* find_match(const std::string& key, int w, int h, const std::string& clut, int zone, int room);
/* The same with the palette's variants (emissive::palette_variants: an
 * animated palette is one palette through its colours; the colours now
 * first): a texture's own entry with any of them, else a palette entry of
 * the first variant that has one. One variant: find_match above. */
const Entry* find_match(const std::string& key, int w, int h, const std::vector<std::string>& cluts, int zone,
    int room);
/* Whether any entry names a texture of this size (and so its key is worth
 * hashing), and whether any entry needs the palette's key. */
bool catalog_has_size(int w, int h);
/* Whether any entry matches by palette alone (no key): then every size counts. */
bool catalog_has_palette_entries();
/* Whether matching a texture of this key needs its palette's key (an entry
 * names the key with a palette, or a palette entry exists). Like
 * catalog_has_size, as of the catalog's last read (no stat calls). */
bool catalog_needs_clut(const std::string& key);
/* Changes whenever the catalog does: either file changed on disk (checks
 * their stamps: two stat calls), an unsaved edit, a preview. */
uint32_t catalog_generation();
/* Changes when more than values changed (an instance added, removed,
 * re-parented, re-assigned or given another master; the files read again): a
 * value edit (set_pending_instance with the same structure) bumps
 * catalog_generation only. The node graphs' slots follow this one. */
uint32_t catalog_structure_generation();
/* How many times a catalog file was read (both files on a load count two):
 * a value edit reads none (RPC material_instance status). */
unsigned catalog_file_reads();
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
/* Temporary preview entries (the Material review window): over both files
 * and the unsaved edits, live at once, never saved (empty: none). */
void set_preview(const std::vector<Entry>& entries);
std::vector<Entry> preview_entries();
bool remove_user_entry(const std::string& id, std::string* error);
std::string entry_json(const Entry& e);

struct Channel {
    int w = 0, h = 0;
    std::vector<uint8_t> values; /* row-major, w * h: 0..255 */
    std::string path;
    bool shipped = false; /* from the shipped layer */
    bool baked = false;   /* from the baked layer (neither: the user's) */
};

/* Where the layers and the exports live (relative to the repository root,
 * the game's working directory):
 *   user     $ROOD_MATERIALS, else materials/ in the user lighting
 *            directory ($ROOD_LIGHTS or pc/user/lighting)
 *   shipped  $ROOD_MATERIALS_SHIPPED, else pc/assets/materials
 *   baked    $ROOD_MATERIALS_BAKED, else pc/build/materials (gitignored:
 *            maps derived from the game's art by material_bake.py)
 *   exports  $ROOD_MATERIAL_EXPORTS, else pc/exports/materials (gitignored:
 *            the exported references are the game's art, never committed) */
std::string user_dir();
std::string shipped_dir();
std::string baked_dir();
std::string export_dir();

/* The channels the loader knows how to use (and the publish guard allows in
 * the shipped layer): "emissive", "roughness", "height", "metal". */
bool known_channel(const std::string& name);
/* A channel's neutral value (an exported blank template): roughness 255
 * (rough), height 128 (flat, room to carve down or raise), else 0. */
uint8_t blank_value(const std::string& name);

/* A texture's channel map (the user's over the shipped one over the baked
 * one), or nullptr. */
const Channel* channel(const std::string& key, const std::string& name);
/* The channel files a texture has: name, and which layer the one in use is in. */
struct Present {
    std::string name, path;
    bool user;
    bool baked = false;
};
std::vector<Present> channels(const std::string& key);
/* Changes whenever a channel file looked at so far changed on disk (or
 * appeared, or went; poll() checks: cheap, a stat per file) and at once when
 * pc_materials_baked is switched. */
uint32_t generation();
void poll(); /* (a slice of the files per call, at most every 50 ms) */
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

/* Whether the baked layer is used (the materials_baked setting; 0: the maps
 * as without it, the colour estimate where an entry asks for one). */
extern int pc_materials_baked;
#endif
