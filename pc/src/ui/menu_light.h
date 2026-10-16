/* The modern menu's model view: its studio light as data (menu_light.cpp).
 *
 * The rig pc/shaders/vulkan/menu_studio.glsl lights the menu's model with
 * (gpu_hw.c pc_hw_present_menu_studio): a key, a fill and a rim light, a
 * sky/ground ambient, then exposure and saturation. Its values come from
 * the "menu_model_light" block of pc/assets/ui/theme.json (ui_theme.cpp,
 * live-reloaded); edits from the RPC (graphics menu_light) and the editor
 * (its schema kind "menu_model_light") change them live until the theme
 * file changes on disk or they are saved into it.
 *
 * Directions are in view space (x right, y down, z away from the eye),
 * pointing from the model toward the light; the shader normalises them.
 * Plain C for the presenters (gl_present.c, vk_present.c). */
#ifndef ROOD_UI_MENU_LIGHT_H
#define ROOD_UI_MENU_LIGHT_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct pc_menu_light {
    /* Each light: its direction, linear colour, intensity (scale on the
     * colour) and wrap (0: Lambert, max(n.l, 0); w: n.l * (1 - w) + w, so
     * the far side keeps w of it). */
    float key_dir[3], key_color[3], key_intensity, key_wrap;
    float fill_dir[3], fill_color[3], fill_intensity, fill_wrap;
    /* The rim: its share at grazing angles, (1 - n.v) ^ power, and lift
     * (added to the texture's colour so dark texels still catch it). */
    float rim_dir[3], rim_color[3], rim_intensity, rim_wrap, rim_power, rim_lift;
    /* Sky (normals up) to ground (normals down), times intensity. */
    float ambient_sky[3], ambient_ground[3], ambient_intensity;
    float exposure; /* a scale on the lit colour */
    float saturation; /* 0 grey, 1 as lit, above 1 more colourful */
} pc_menu_light;

/* The rig as shipped (theme.json's block holds the same values; the
 * presenters fall back to it without the menus' code). */
static inline void pc_ui_menu_model_light_defaults(pc_menu_light* l)
{
    static const pc_menu_light d = {
        { -0.45f, -0.5f, -0.74f }, { 1.0f, 0.97f, 0.92f }, 0.5f, 0.4f, /* key: warm, upper left in front */
        { 0.85f, -0.05f, -0.52f }, { 0.9f, 0.95f, 1.0f }, 0.15f, 0.0f, /* fill: cool, from the right */
        { 0.5f, -0.55f, 0.67f }, { 0.85f, 0.9f, 1.0f }, 0.45f, 0.2f, 2.0f, 0.1f, /* rim: behind, upper right */
        { 0.56f, 0.56f, 0.58f }, { 0.5f, 0.5f, 0.52f }, 1.0f, /* ambient: sky, ground */
        1.0f, 1.0f, /* exposure, saturation */
    };
    *l = d;
}

/* The shader's view of it: PC_MENU_LIGHT_VEC4S vec4s (menu_studio.glsl
 * MENU_STUDIO(i)): key direction + intensity, key colour + wrap, the same
 * for the fill and the rim, ground + rim power, sky + rim lift, (ambient
 * intensity, exposure, saturation, 0). */
#define PC_MENU_LIGHT_VEC4S 9
static inline void pc_menu_light_pack(const pc_menu_light* l, float out[PC_MENU_LIGHT_VEC4S][4])
{
    const float* dirs[3] = { l->key_dir, l->fill_dir, l->rim_dir };
    const float* colors[3] = { l->key_color, l->fill_color, l->rim_color };
    const float intensity[3] = { l->key_intensity, l->fill_intensity, l->rim_intensity };
    const float wrap[3] = { l->key_wrap, l->fill_wrap, l->rim_wrap };
    for (int i = 0; i < 3; i++)
        for (int c = 0; c < 4; c++) {
            out[i * 2][c] = c < 3 ? dirs[i][c] : intensity[i];
            out[i * 2 + 1][c] = c < 3 ? colors[i][c] : wrap[i];
        }
    for (int c = 0; c < 3; c++) {
        out[6][c] = l->ambient_ground[c];
        out[7][c] = l->ambient_sky[c];
    }
    out[6][3] = l->rim_power;
    out[7][3] = l->rim_lift;
    out[8][0] = l->ambient_intensity;
    out[8][1] = l->exposure;
    out[8][2] = l->saturation;
    out[8][3] = 0.0f;
}

/* The rig now (the theme file's, or live edits since it loaded); 1. */
int pc_ui_menu_model_light(pc_menu_light* out);
/* Live edits (clamped to the fields' ranges); until the theme file changes on disk. */
void pc_ui_menu_model_light_set(const pc_menu_light* in);
/* Write the rig now into the theme file's "menu_model_light" block (the
 * file's other keys and formatting kept; atomically). 0 on success, else
 * -1 with a message in `error`. */
int pc_ui_menu_model_light_save(char* error, int size);
/* RPC graphics {menu_light: ...} (dev_rpc.c hands over the value's text):
 * an object {field: value} sets those fields (names as the schema's:
 * key_intensity, key_color [r, g, b], key_direction [x, y, z],
 * key_azimuth / key_elevation in degrees, ...), "reset" the defaults,
 * "save" writes the theme file, anything else (true, {}) only reads.
 * Writes the rig now as a JSON object to `out`; 0, or -1 with a message. */
int pc_ui_menu_light_rpc(const char* value, char* out, int size);

#ifdef __cplusplus
}
#endif
#endif
