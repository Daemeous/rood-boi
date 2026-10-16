// The menu model's studio light (menu_light.h): its fields for the theme
// parser (ui_theme.cpp), the editor's schema kind (menu_light_kind.cpp) and
// the tests. Menus' internals.
#pragma once
#include <cstddef>
#include <string>

#include "ui/menu_light.h"

namespace prefab {
struct Json;
}

namespace ui {

struct MenuLightField {
    enum Type { SCALAR, COLOUR, DIRECTION };
    const char* name; // flat, as the RPC and the schema name it: "key_intensity"
    const char* group; // the theme block's object ("key", "fill", "rim", "ambient"; "" the block itself)
    const char* key; // its key there ("intensity")
    Type type;
    size_t offset; // in pc_menu_light
    float min, max; // every component's range (values are clamped to it)
    int components() const;
    float* at(pc_menu_light& l) const;
    const float* at(const pc_menu_light& l) const;
};
const MenuLightField* menu_light_fields(int* count);
const MenuLightField* menu_light_field(const char* name);

// A direction's azimuth (degrees: 0 from the eye, positive to the right, 180 from behind) and elevation
// (degrees above the horizon), and back (a unit vector).
void menu_light_angles(const float dir[3], float* azimuth, float* elevation);
void menu_light_direction(float azimuth, float elevation, float dir[3]);
// Every field into its range (a direction without a length, or a value that is not finite: the default's).
void menu_light_clamp(pc_menu_light& l);
// The theme's "menu_model_light" block over `l` (missing keys keep their values; out-of-range values are
// clamped); false on a value of the wrong type, with `error`.
bool menu_light_parse(const prefab::Json& block, pc_menu_light& l, std::string* error);
// The block as the theme file holds it (`indent`: the key's own indentation).
std::string menu_light_block(const pc_menu_light& l, const char* indent);
// Theme text with its "menu_model_light" block replaced by (or, missing, given) `l`'s; the rest kept as it was.
bool menu_light_replace_block(const std::string& text, const pc_menu_light& l, std::string& out, std::string* error);
// Whether the live rig differs from the theme file's (unsaved edits).
bool menu_light_edited();

} // namespace ui
