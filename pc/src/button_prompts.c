/* Modern button prompts (button_prompts.h; README "2D / UI").
 *
 * Where the game shows a PS1 button (found by page, CLUT and texel box, as
 * ui_text.c finds glyphs):
 *
 *   text     the font page (832, 0), 4-bit, CLUT row 222, 12 x 12 cells:
 *            glyphs 0xAE-0xB1 of either table (triangle, square, circle,
 *            cross) in menus, the information box and the quick manual
 *   sprite   the field UI page (768, 0), CLUT (944, 223), 16 x 16 cells at
 *            (0, 128): triangle, square, circle, cross, then L1, R1, L2, R2
 *            below (vs_mainmenu_renderButton, the quick manual's page keys,
 *            MENUF's PRESS BUTTON prompt, the name entry screen, the L2
 *            shortcut cross's face-button diamond)
 *   dpad     the same page and CLUT, 30 x 30 at (184, 160): the L2 shortcut
 *            cross's D-pad (BATTLE.PRG func_800C9FE8), drawn as the
 *            family's D-pad when the four directions are bound to one, its
 *            arms dimmed or filled by which have a usable action beside
 *            them (button_prompts_dpad; weapon_slots.c reports them)
 *
 * the region goes through the 2D atlas (ui_upscale.c) and its texels are
 * drawn again from an icon: the PS1 button's binding on the device used
 * last (or the forced family) names a glyph of Kenney's Input Prompts fonts
 * (pc/third_party/fonts/KenneyInputPrompts, with each font's name map), made
 * into a signed distance field by ui_text.c and fitted, centred on the
 * original: text in its ink box (one pixel larger, inside the cell), sprites
 * in a circle button_prompts_scale of the cell across (the shortcut cross's
 * face-button diamond overlaps its cells: circles that do not meet keep any
 * shape apart), the shortcut cross's D-pad in its arms' box. Text icons take
 * the original glyph's CLUT ramp (coverage -> index), so their colour,
 * brightness and translucency are the text's; sprite icons are the glyph
 * alone (button_prompts_style: solid or outline glyphs, white or the
 * family's colours), no backing. Placement and timing are the game's: only
 * the texels inside the region change. A key or element without an icon
 * keeps the original. */
#define _POSIX_C_SOURCE 200809L
#include "button_prompts.h"
#include "boot_controller.h"
#include "ui_text.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int pc_button_prompts = PC_PROMPTS_AUTO;
const char* const pc_button_prompts_names[PC_PROMPTS_MODES] = { "auto", "playstation", "xbox", "nintendo",
    "keyboard", "original" };
int pc_button_prompts_style = PC_PROMPTS_STYLE_COLOR_OUTLINE;
const char* const pc_button_prompts_style_names[PC_PROMPTS_STYLES] = { "solid", "outline", "color_solid",
    "color_outline" };
float pc_button_prompts_scale = 0.7f;
int pc_button_prompts_dpad = PC_PROMPTS_DPAD_DIM;
const char* const pc_button_prompts_dpad_names[PC_PROMPTS_DPADS] = { "cross", "dim", "lit" };
/* Bit k: the cross's arm k (up, left, right, down) has a usable action;
 * the RPC's stand-in (tests; -1: none). */
static unsigned game_actions = 15;
static int actions_override = -1;
#define cross_actions (actions_override >= 0 ? (unsigned)actions_override : game_actions)
void pc_button_prompts_cross_action(int arm, int usable)
{
    if (arm >= 0 && arm < 4)
        game_actions = usable ? game_actions | 1u << arm : game_actions & ~(1u << arm);
}
void pc_button_prompts_cross_override(int arms) { actions_override = arms >= 0 && arms < 16 ? arms : -1; }
static int outline_style(void)
{
    return pc_button_prompts_style == PC_PROMPTS_STYLE_OUTLINE || pc_button_prompts_style == PC_PROMPTS_STYLE_COLOR_OUTLINE;
}
static int colour_style(void)
{
    return pc_button_prompts_style == PC_PROMPTS_STYLE_COLOR_SOLID
        || pc_button_prompts_style == PC_PROMPTS_STYLE_COLOR_OUTLINE;
}
static float scale_now(void)
{
    float k = pc_button_prompts_scale;
    return k != k ? 0.7f : k < 0.4f ? 0.4f : k > 1.0f ? 1.0f : k;
}

static int device = PC_PROMPTS_KEYBOARD, device_variant;
static long regions, failed;
/* What each prompt on screen was drawn as since pc_button_prompts_shown_reset
 * (RPC button_prompts "shown"; tests): family << 24 | codepoint per PS1
 * button and context (0 text, 1 sprite), 0: not drawn. */
static unsigned shown[PC_PAD_BUTTONS][2], shown_dpad;
static int shown_arms = -1; /* the D-pad's lit arms (bits up, left, right, down; -1: the whole D-pad) */
void pc_button_prompts_shown_reset(void)
{
    memset(shown, 0, sizeof shown);
    shown_dpad = 0;
    shown_arms = -1;
}

static void init_once(void)
{
    static int done;
    if (done)
        return;
    done = 1;
    /* ROOD_BUTTON_PROMPTS: the mode before any settings file (tests: original). */
    const char* env = getenv("ROOD_BUTTON_PROMPTS");
    for (int k = 0; env && k < PC_PROMPTS_MODES; k++)
        if (!strcmp(env, pc_button_prompts_names[k]))
            pc_button_prompts = k;
}

void pc_button_prompts_device(int family, int variant)
{
    if (family >= PC_PROMPTS_PLAYSTATION && family <= PC_PROMPTS_KEYBOARD)
        device = family, device_variant = variant;
}

int pc_button_prompts_family(void)
{
    init_once();
    if (pc_button_prompts == PC_PROMPTS_ORIGINAL)
        return 0;
    return pc_button_prompts == PC_PROMPTS_AUTO ? device : pc_button_prompts;
}
static int variant_now(void) { return pc_button_prompts == PC_PROMPTS_AUTO ? device_variant : 0; }
int pc_button_prompts_active(void) { return pc_button_prompts_family() != 0; }

unsigned pc_button_prompts_generation(void)
{
    static unsigned generation = 1, last_bind;
    static int last_family = -1, last_variant, last_style = -1, last_dpad = -1, last_device = -1;
    static unsigned last_actions = 15;
    static float last_scale;
    int family = pc_button_prompts_family(), variant = variant_now();
    /* The shortcut cross's usable arms change its D-pad (modes dim, lit). */
    unsigned actions = pc_button_prompts_dpad == PC_PROMPTS_DPAD_CROSS ? last_actions : cross_actions;
    if (family != last_family || variant != last_variant || pc_bind_generation != last_bind
        || pc_button_prompts_style != last_style || scale_now() != last_scale || pc_button_prompts_dpad != last_dpad
        || actions != last_actions || (!family && device != last_device)) { /* (original: Travel's icon) */
        last_family = family, last_variant = variant, last_bind = pc_bind_generation, last_device = device;
        last_style = pc_button_prompts_style, last_scale = scale_now(), last_dpad = pc_button_prompts_dpad;
        last_actions = actions;
        ++generation;
    }
    return generation;
}

/* ---- icon fonts ---- */

#define KENNEY "pc/third_party/fonts/KenneyInputPrompts/kenney_input_"
static const char* const font_names[5] = { NULL, "playstation_series", "xbox_series", "nintendo_switch",
    "keyboard_mouse" };
typedef struct {
    char name[48];
    unsigned codepoint;
} map_entry;
static map_entry* maps[5];
static int map_sizes[5], map_loaded[5];

static unsigned codepoint_of(int family, const char* name)
{
    if (!map_loaded[family]) {
        map_loaded[family] = 1;
        char path[160];
        snprintf(path, sizeof path, KENNEY "%s_map.txt", font_names[family]);
        FILE* f = fopen(path, "r");
        if (!f) {
            fprintf(stderr, "[ui] button_prompts: cannot read %s: the original glyphs stay\n", path);
            return 0;
        }
        char line[160];
        int capacity = 0;
        while (fgets(line, sizeof line, f)) {
            map_entry e;
            if (sscanf(line, " %47[^:]: U+%x", e.name, &e.codepoint) != 2)
                continue;
            if (map_sizes[family] == capacity) {
                capacity = capacity ? capacity * 2 : 128;
                map_entry* grown = realloc(maps[family], (size_t)capacity * sizeof *grown);
                if (!grown)
                    break;
                maps[family] = grown;
            }
            maps[family][map_sizes[family]++] = e;
        }
        fclose(f);
    }
    for (int i = 0; i < map_sizes[family]; i++)
        if (!strcmp(maps[family][i].name, name))
            return maps[family][i].codepoint;
    return 0;
}

/* The glyph name for a gamepad element in a family. */
static const char* pad_glyph(int family, int variant, int element)
{
    /* D-pad directions use the _outline glyphs: the filled ones are one plain
     * cross for every direction (one-colour font); the outline set fills the
     * pressed arm. */
    static const char* const ps[] = { [SDL_GAMEPAD_BUTTON_SOUTH] = "playstation_button_cross",
        [SDL_GAMEPAD_BUTTON_EAST] = "playstation_button_circle", [SDL_GAMEPAD_BUTTON_WEST] = "playstation_button_square",
        [SDL_GAMEPAD_BUTTON_NORTH] = "playstation_button_triangle",
        [SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = "playstation_trigger_l1_alternative",
        [SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = "playstation_trigger_r1_alternative",
        [SDL_GAMEPAD_BUTTON_LEFT_STICK] = "playstation_button_l3", [SDL_GAMEPAD_BUTTON_RIGHT_STICK] = "playstation_button_r3",
        [SDL_GAMEPAD_BUTTON_DPAD_UP] = "playstation_dpad_up_outline", [SDL_GAMEPAD_BUTTON_DPAD_DOWN] = "playstation_dpad_down_outline",
        [SDL_GAMEPAD_BUTTON_DPAD_LEFT] = "playstation_dpad_left_outline",
        [SDL_GAMEPAD_BUTTON_DPAD_RIGHT] = "playstation_dpad_right_outline" };
    static const char* const xbox[] = { [SDL_GAMEPAD_BUTTON_SOUTH] = "xbox_button_a",
        [SDL_GAMEPAD_BUTTON_EAST] = "xbox_button_b", [SDL_GAMEPAD_BUTTON_WEST] = "xbox_button_x",
        [SDL_GAMEPAD_BUTTON_NORTH] = "xbox_button_y", [SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = "xbox_lb",
        [SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = "xbox_rb", [SDL_GAMEPAD_BUTTON_LEFT_STICK] = "xbox_ls",
        [SDL_GAMEPAD_BUTTON_RIGHT_STICK] = "xbox_rs", [SDL_GAMEPAD_BUTTON_GUIDE] = "xbox_guide",
        [SDL_GAMEPAD_BUTTON_DPAD_UP] = "xbox_dpad_up_outline", [SDL_GAMEPAD_BUTTON_DPAD_DOWN] = "xbox_dpad_down_outline",
        [SDL_GAMEPAD_BUTTON_DPAD_LEFT] = "xbox_dpad_left_outline", [SDL_GAMEPAD_BUTTON_DPAD_RIGHT] = "xbox_dpad_right_outline" };
    /* Nintendo's labels sit the other way round: A is east, B south. */
    static const char* const nintendo[] = { [SDL_GAMEPAD_BUTTON_SOUTH] = "switch_button_b",
        [SDL_GAMEPAD_BUTTON_EAST] = "switch_button_a", [SDL_GAMEPAD_BUTTON_WEST] = "switch_button_y",
        [SDL_GAMEPAD_BUTTON_NORTH] = "switch_button_x", [SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = "switch_button_l",
        [SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = "switch_button_r", [SDL_GAMEPAD_BUTTON_BACK] = "switch_button_minus",
        [SDL_GAMEPAD_BUTTON_START] = "switch_button_plus", [SDL_GAMEPAD_BUTTON_GUIDE] = "switch_button_home",
        [SDL_GAMEPAD_BUTTON_LEFT_STICK] = "switch_stick_l_press",
        [SDL_GAMEPAD_BUTTON_RIGHT_STICK] = "switch_stick_r_press", [SDL_GAMEPAD_BUTTON_DPAD_UP] = "switch_dpad_up_outline",
        [SDL_GAMEPAD_BUTTON_DPAD_DOWN] = "switch_dpad_down_outline", [SDL_GAMEPAD_BUTTON_DPAD_LEFT] = "switch_dpad_left_outline",
        [SDL_GAMEPAD_BUTTON_DPAD_RIGHT] = "switch_dpad_right_outline" };
    int left = element == PC_PAD_AXIS(SDL_GAMEPAD_AXIS_LEFT_TRIGGER),
        right = element == PC_PAD_AXIS(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    switch (family) {
    case PC_PROMPTS_PLAYSTATION:
        if (left || right)
            return left ? "playstation_trigger_l2_alternative" : "playstation_trigger_r2_alternative";
        if (element == SDL_GAMEPAD_BUTTON_START)
            return variant >= 4 ? (variant == 4 ? "playstation4_button_options" : "playstation5_button_options")
                                : "playstation3_button_start";
        if (element == SDL_GAMEPAD_BUTTON_BACK)
            return variant == 4 ? "playstation4_button_share"
                 : variant == 5 ? "playstation5_button_create"
                                : "playstation3_button_select";
        return element >= 0 && element < (int)(sizeof ps / sizeof *ps) ? ps[element] : NULL;
    case PC_PROMPTS_XBOX:
        if (left || right)
            return left ? "xbox_lt" : "xbox_rt";
        if (element == SDL_GAMEPAD_BUTTON_START)
            return variant == 360 ? "xbox_button_start" : "xbox_button_menu";
        if (element == SDL_GAMEPAD_BUTTON_BACK)
            return variant == 360 ? "xbox_button_back" : "xbox_button_view";
        return element >= 0 && element < (int)(sizeof xbox / sizeof *xbox) ? xbox[element] : NULL;
    case PC_PROMPTS_NINTENDO:
        if (left || right)
            return left ? "switch_button_zl" : "switch_button_zr";
        return element >= 0 && element < (int)(sizeof nintendo / sizeof *nintendo) ? nintendo[element] : NULL;
    }
    return NULL;
}

/* The glyph name for a key (keycaps). */
static const char* key_glyph(SDL_Keycode key)
{
    static const struct {
        SDL_Keycode key;
        const char* glyph;
    } special[] = {
        { SDLK_RETURN, "return" }, { SDLK_KP_ENTER, "numpad_enter" }, { SDLK_KP_PLUS, "numpad_plus" },
        { SDLK_BACKSPACE, "backspace_icon" }, { SDLK_UP, "arrow_up" }, { SDLK_DOWN, "arrow_down" },
        { SDLK_LEFT, "arrow_left" }, { SDLK_RIGHT, "arrow_right" }, { SDLK_LSHIFT, "shift_icon" },
        { SDLK_RSHIFT, "shift_icon" }, { SDLK_LCTRL, "ctrl" }, { SDLK_RCTRL, "ctrl" }, { SDLK_LALT, "alt" },
        { SDLK_RALT, "alt" }, { SDLK_CAPSLOCK, "capslock_icon" }, { SDLK_PAGEUP, "page_up" },
        { SDLK_PAGEDOWN, "page_down" }, { SDLK_SLASH, "slash_forward" }, { SDLK_BACKSLASH, "slash_back" },
        { SDLK_LEFTBRACKET, "bracket_open" }, { SDLK_RIGHTBRACKET, "bracket_close" }, { SDLK_GRAVE, "tilde" },
    };
    static char name[48];
    for (size_t i = 0; i < sizeof special / sizeof *special; i++)
        if (special[i].key == key) {
            snprintf(name, sizeof name, "keyboard_%s", special[i].glyph);
            return name;
        }
    const char* base = pc_key_name(key); /* a-z, 0-9, tab, space, escape, f1, minus, ... */
    if (!base)
        return NULL;
    snprintf(name, sizeof name, "keyboard_%s", base);
    return name;
}

/* What PS1 button `button` shows in `family`: its glyph name (NULL: none). */
static const char* glyph_for(int family, int variant, int button)
{
    if (family == PC_PROMPTS_KEYBOARD)
        return pc_bind_keys[button][0] ? key_glyph(pc_bind_keys[button][0]) : NULL;
    return pc_bind_pad[button] == PC_PAD_NONE ? NULL : pad_glyph(family, variant, pc_bind_pad[button]);
}

/* The codepoint of `name` in the style drawn now: the outline styles take
 * the glyph's _outline variant where the font has one (D-pad directions are
 * _outline glyphs already: the pressed arm filled). */
static unsigned styled_codepoint(int family, const char* name)
{
    if (!name)
        return 0;
    if (outline_style() && !strstr(name, "_outline")) {
        char outline[64];
        snprintf(outline, sizeof outline, "%s_outline", name);
        unsigned cp = codepoint_of(family, outline);
        if (cp)
            return cp;
    }
    return codepoint_of(family, name);
}

/* The whole D-pad (the shortcut cross's four arms, one 30 x 30 sprite): the
 * family's D-pad glyphs' base name when the four directions are bound to one
 * (the arrow keys on the keyboard), else NULL (the original art). The font
 * has <base> (solid), <base>_none (outline arms) and <base>_<dir>_outline
 * (outline arms, that one filled). */
static const char* dpad_base(int family)
{
    static const int directions[4] = { 12, 13, 14, 15 }; /* up, right, down, left */
    if (family == PC_PROMPTS_KEYBOARD) {
        static const SDL_Keycode arrows[4] = { SDLK_UP, SDLK_RIGHT, SDLK_DOWN, SDLK_LEFT };
        for (int k = 0; k < 4; k++)
            if (pc_bind_keys[directions[k]][0] != arrows[k])
                return NULL;
        return "keyboard_arrows";
    }
    static const int pads[4] = { SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
        SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT };
    for (int k = 0; k < 4; k++)
        if (pc_bind_pad[directions[k]] != pads[k])
            return NULL;
    static const char* const names[4] = { NULL, "playstation_dpad", "xbox_dpad", "switch_dpad" };
    return family > 0 && family < 4 ? names[family] : NULL;
}
/* The D-pad's shape in the style drawn now: <base> or <base>_none. */
static const char* dpad_glyph(int family)
{
    static char name[48];
    const char* base = dpad_base(family);
    if (!base)
        return NULL;
    snprintf(name, sizeof name, "%s%s", base, outline_style() ? "_none" : "");
    return name;
}
static const char* const arm_names[4] = { "up", "left", "right", "down" };

/* ---- the game's button pictographs and sprites ---- */

enum { TEXT, SPRITE, DPAD };
typedef struct {
    int context, button;   /* TEXT / SPRITE / DPAD; the PS1 button (pad bit index; DPAD -1) */
    int cell_u, cell_v, cell; /* the cell in page texels and its size */
} prompt_region;

static int page_index(const uint16_t* vram, int page_x, int u, int v)
{
    return (vram[(v & 511) * 1024 + ((page_x + (u >> 2)) & 1023)] >> ((u & 3) * 4)) & 15;
}

/* PS1 pad bits of the font's 0xAE-0xB1 and of the sprites in reading order. */
static const int text_buttons[4] = { 4, 7, 5, 6 };                /* triangle, square, circle, cross */
static const int sprite_buttons[8] = { 4, 7, 5, 6, 2, 3, 0, 1 }; /* ..., L1, R1, L2, R2 */

static int identify(unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1, prompt_region* r)
{
    if ((tpage >> 7 & 3) != 0 || (tpage >> 4 & 1) != 0)
        return 0; /* 4-bit pages at y 0 */
    int page = (int)(tpage & 15), cx = (int)(clut & 63) * 16, cy = (int)(clut >> 6 & 511);
    if (page == 13 && cy == 222 && cx >= 832 && cx < 960) {
        int col = u0 / 12, row = v0 / 12;
        if (u1 / 12 != col || v1 / 12 != row || col >= 21 || row >= 18)
            return 0;
        int glyph = (row * 21 + col) % 189;
        if (glyph < 0xAE || glyph > 0xB1)
            return 0;
        *r = (prompt_region){ TEXT, text_buttons[glyph - 0xAE], col * 12, row * 12, 12 };
        return 1;
    }
    if (page == 12 && cy == 223 && cx == 944 && u1 < 64 && v0 >= 128 && v1 < 160) {
        if (u1 / 16 != u0 / 16 || v1 / 16 != v0 / 16)
            return 0;
        *r = (prompt_region){ SPRITE, sprite_buttons[(v0 - 128) / 16 * 4 + u0 / 16], u0 & ~15, v0 & ~15, 16 };
        return 1;
    }
    /* BATTLE.PRG func_800C9FE8 (the L2 shortcut cross): the D-pad's four
     * arms, 30 x 30 at (184, 160) of the same page and CLUT. */
    if (page == 12 && cy == 223 && cx == 944 && u0 >= 184 && v0 >= 160 && u1 < 214 && v1 < 190) {
        *r = (prompt_region){ DPAD, -1, 184, 160, 30 };
        return 1;
    }
    return 0;
}

/* The original's ink box in its cell (x0, y0, x1, y1, exclusive ends). */
static int ink(const uint16_t* vram, int page_x, const prompt_region* r, int threshold, int box[4])
{
    box[0] = box[1] = r->cell, box[2] = box[3] = 0;
    for (int y = 0; y < r->cell; y++)
        for (int x = 0; x < r->cell; x++)
            if (page_index(vram, page_x, r->cell_u + x, r->cell_v + y) >= threshold) {
                box[0] = x < box[0] ? x : box[0], box[1] = y < box[1] ? y : box[1];
                box[2] = x + 1 > box[2] ? x + 1 : box[2], box[3] = y + 1 > box[3] ? y + 1 : box[3];
            }
    return box[2] > box[0];
}

static const pc_ui_icon* icon_for(const prompt_region* r, int family, int variant, unsigned* codepoint)
{
    *codepoint = r->context == DPAD ? styled_codepoint(family, dpad_glyph(family))
                                    : styled_codepoint(family, glyph_for(family, variant, r->button));
    if (!*codepoint)
        return NULL;
    char path[160];
    snprintf(path, sizeof path, KENNEY "%s.ttf", font_names[family]);
    int missing;
    return pc_ui_icon_glyph(path, *codepoint, &missing);
}

/* button_prompts_dpad dim / lit: which of the cross's arms have a usable
 * action (*arms); for lit also their glyphs (<base>_<dir>_outline: the
 * outline arms with that one filled; their union fills every lit arm).
 * Returns how many glyphs (dim: 0), -1 in mode cross or when the font lacks
 * one (the whole D-pad then). */
static int dpad_lit(int family, const pc_ui_icon* lit[4], unsigned* arms)
{
    const char* base = dpad_base(family);
    *arms = 0;
    if (pc_button_prompts_dpad == PC_PROMPTS_DPAD_CROSS || !base)
        return -1;
    if (pc_button_prompts_dpad == PC_PROMPTS_DPAD_DIM) {
        *arms = cross_actions & 15;
        return 0;
    }
    char path[160];
    snprintf(path, sizeof path, KENNEY "%s.ttf", font_names[family]);
    int n = 0;
    for (int k = 0; k < 4; k++) {
        if (!(cross_actions >> k & 1))
            continue;
        char name[64];
        snprintf(name, sizeof name, "%s_%s_outline", base, arm_names[k]);
        unsigned cp = codepoint_of(family, name);
        int missing;
        if (!cp || !(lit[n] = pc_ui_icon_glyph(path, cp, &missing)))
            return -1;
        ++n, *arms |= 1u << k;
    }
    return n;
}

int pc_button_prompts_classify(const uint16_t* vram, unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1,
    uint64_t* key)
{
    prompt_region r;
    int family = pc_button_prompts_family();
    if (!family || !vram || !identify(tpage, clut, u0, v0, u1, v1, &r))
        return 0;
    /* An empty cell (the page holds something else now) stays as it is. */
    int box[4];
    if (!ink(vram, (int)(tpage & 15) * 64, &r, r.context == TEXT ? 4 : 1, box))
        return 0;
    if (r.context != TEXT && (box[2] - box[0] < 10 || box[3] - box[1] < 10))
        return 0;
    unsigned codepoint;
    if (!icon_for(&r, family, variant_now(), &codepoint))
        return 0;
    /* The D-pad: its mode and which arms are lit are in the key too. */
    unsigned lit_key = 0;
    if (r.context == DPAD) {
        const pc_ui_icon* lit[4];
        unsigned arms;
        int n = dpad_lit(family, lit, &arms);
        lit_key = n >= 0 ? (unsigned)pc_button_prompts_dpad << 8 | arms << 4 : 0;
        shown_dpad = (unsigned)family << 24 | codepoint;
        shown_arms = n >= 0 ? (int)arms : -1;
    } else
        shown[r.button][r.context] = (unsigned)family << 24 | codepoint;
    /* The style and size are in the key too (the same glyph drawn another way). */
    *key = (uint64_t)family << 48 ^ (uint64_t)pc_button_prompts_style << 44
         ^ (uint64_t)lrintf(scale_now() * 100) << 36 ^ (uint64_t)codepoint << 16 ^ (uint64_t)r.context << 8
         ^ (uint64_t)(r.button & 0xff) ^ (uint64_t)lit_key << 52;
    return 1;
}

/* ---- drawing ---- */

typedef struct {
    float r, g, b;
} rgb;
static uint16_t ps1(rgb c, uint16_t stp)
{
    int r = (int)lrintf(fminf(fmaxf(c.r, 0), 1) * 31), g = (int)lrintf(fminf(fmaxf(c.g, 0), 1) * 31),
        b = (int)lrintf(fminf(fmaxf(c.b, 0), 1) * 31);
    uint16_t v = (uint16_t)(r | g << 5 | b << 10 | stp);
    return v ? v : 1 << 10; /* 0 is the transparent colour key */
}
static rgb mix(rgb a, rgb b, float t)
{
    return (rgb){ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
}
static rgb hex(unsigned v) { return (rgb){ (v >> 16 & 255) / 255.0f, (v >> 8 & 255) / 255.0f, (v & 255) / 255.0f }; }

/* An icon's colour: in the colour styles the family's face colours (the
 * other buttons and families neutral), else neutral. */
static rgb ink_colour(int family, int element)
{
    rgb neutral = hex(0xeeeee8);
    if (!colour_style())
        return neutral;
    if (family == PC_PROMPTS_PLAYSTATION)
        switch (element) {
        case SDL_GAMEPAD_BUTTON_NORTH:
            return hex(0x40d8a8);
        case SDL_GAMEPAD_BUTTON_EAST:
            return hex(0xf06070);
        case SDL_GAMEPAD_BUTTON_SOUTH:
            return hex(0x80b0f0);
        case SDL_GAMEPAD_BUTTON_WEST:
            return hex(0xe098d8);
        }
    if (family == PC_PROMPTS_XBOX)
        switch (element) {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            return hex(0x70c840);
        case SDL_GAMEPAD_BUTTON_EAST:
            return hex(0xf04840);
        case SDL_GAMEPAD_BUTTON_WEST:
            return hex(0x4890f0);
        case SDL_GAMEPAD_BUTTON_NORTH:
            return hex(0xf8c830);
        }
    return neutral;
}

/* The farthest inked point of an icon from its box's centre (field pixels):
 * sprite icons are fitted by it, into a circle, so that any shape (round
 * buttons, square key caps, wide shoulder tabs) keeps clear of neighbours
 * whose circles do not meet. */
static float icon_radius(const pc_ui_icon* icon)
{
    static struct {
        const pc_ui_icon* icon;
        float radius;
    } cache[64];
    static int used;
    for (int i = 0; i < used; i++)
        if (cache[i].icon == icon)
            return cache[i].radius;
    float g[4];
    pc_ui_icon_box(icon, g);
    float cx = (g[0] + g[2]) * 0.5f, cy = (g[1] + g[3]) * 0.5f, step = fmaxf(g[2] - g[0], g[3] - g[1]) / 128;
    float radius = 0;
    for (float y = g[1]; y <= g[3]; y += step)
        for (float x = g[0]; x <= g[2]; x += step)
            if (pc_ui_icon_distance(icon, x, y) > 0) {
                float r = hypotf(x - cx, y - cy);
                radius = r > radius ? r : radius;
            }
    radius += step * 0.5f;
    if (used == 64)
        used = 0;
    cache[used].icon = icon, cache[used].radius = radius, ++used;
    return radius;
}

#define MAX_GRID (32 * 8)
int pc_button_prompts_render(const uint16_t* vram, unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1,
    int factor, int scale, uint32_t* out)
{
    prompt_region r;
    int family = pc_button_prompts_family(), variant = variant_now();
    unsigned codepoint;
    const pc_ui_icon* icon;
    int page_x = (int)(tpage & 15) * 64, box[4];
    if (!family || !identify(tpage, clut, u0, v0, u1, v1, &r) || !(icon = icon_for(&r, family, variant, &codepoint))
        || !ink(vram, page_x, &r, r.context == TEXT ? 4 : 1, box) || factor < 1 || factor > 8) {
        ++failed;
        return 0;
    }
    float g[4];
    pc_ui_icon_box(icon, g);
    float gw = g[2] - g[0], gh = g[3] - g[1];
    if (gw <= 0 || gh <= 0) {
        ++failed;
        return 0;
    }
    float cx_f = (g[0] + g[2]) * 0.5f, cy_f = (g[1] + g[3]) * 0.5f;
    /* The original's ink box: its centre is the icon's. */
    float t0 = (float)box[0], t1 = (float)box[1], t2 = (float)box[2], t3 = (float)box[3];
    float cx_t = (t0 + t2) * 0.5f, cy_t = (t1 + t3) * 0.5f;
    float s; /* original pixels per field pixel */
    if (r.context == TEXT) {
        /* Text: the ink box and a pixel more each side, inside the cell. */
        t0 = fmaxf(t0 - 1, 0), t1 = fmaxf(t1 - 1, 0), t2 = fminf(t2 + 1, (float)r.cell), t3 = fminf(t3 + 1, (float)r.cell);
        s = fminf((t2 - t0) / gw, (t3 - t1) / gh);
        cx_t = (t0 + t2) * 0.5f, cy_t = (t1 + t3) * 0.5f;
    } else if (r.context == SPRITE) {
        /* Sprites: inside a circle button_prompts_scale of the cell across,
         * kept inside the cell. */
        float radius = r.cell * scale_now() * 0.5f;
        s = radius / icon_radius(icon);
        cx_t = fminf(fmaxf(cx_t, radius), r.cell - radius), cy_t = fminf(fmaxf(cy_t, radius), r.cell - radius);
    } else {
        /* The D-pad: its arms' box, a little smaller with the scale. */
        float k = fminf(1.0f, scale_now() + 0.1f);
        s = fminf((t2 - t0) * k / gw, (t3 - t1) * k / gh);
    }
    /* The D-pad in modes dim and lit. lit: the lit arms' glyphs (same font,
     * same origin: they line up with the shape); outline styles draw their
     * union (lit arms filled, the others hollow), solid styles the whole
     * shape, bright where the union is (lit arms and every arm's rim), dim
     * inside the other arms. dim: the shape, each arm bright or dim by the
     * arm it lies in (the crosses: quarters split along the diagonals
     * through the shape's centre; the arrow keys: the top key, then the
     * bottom row in thirds; the splits fall in gaps or at inner corners). */
    const pc_ui_icon* lit[4];
    unsigned arms = 0;
    int lit_count = r.context == DPAD ? dpad_lit(family, lit, &arms) : -1;
    int dim_mode = lit_count == 0 && pc_button_prompts_dpad == PC_PROMPTS_DPAD_DIM;
    int lit_union = lit_count > 0 && outline_style(), lit_tint = !dim_mode && lit_count >= 0 && !outline_style();
    float third = gw / 6;
    /* Distances over the whole cell at the atlas's resolution (PS1 pixels;
     * at 1x outlines a little bolder so their strokes stay whole), drawn
     * anti-aliased (distance texels, ui_text.h). */
    int res = factor, n = r.cell * res;
    float bias = scale < 2 && r.context != TEXT && outline_style() ? 0.25f : 0.0f;
    static float cover[MAX_GRID * MAX_GRID], bright[MAX_GRID * MAX_GRID];
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            float ox = (x + 0.5f) / res, oy = (y + 0.5f) / res;
            float fx = cx_f + (ox - cx_t) / s, fy = cy_f - (oy - cy_t) / s;
            float d = pc_ui_icon_distance(icon, fx, fy), dl = -1e4f;
            for (int k = 0; k < lit_count; k++)
                dl = fmaxf(dl, pc_ui_icon_distance(lit[k], fx, fy));
            if (lit_union)
                d = dl;
            cover[y * n + x] = d * s + bias; /* (a distance) */
            float b = 1.0f;
            if (lit_tint)
                b = fminf(fmaxf(0.5f + dl * s * res, 0.0f), 1.0f);
            else if (dim_mode) {
                float dx = fx - cx_f, dy = fy - cy_f;
                int arm = family == PC_PROMPTS_KEYBOARD ? (dy > 0 ? 0 : dx < -third ? 1 : dx > third ? 2 : 3)
                        : fabsf(dx) > fabsf(dy)       ? (dx < 0 ? 1 : 2)
                                                      : (dy > 0 ? 0 : 3);
                b = (float)(arms >> arm & 1);
            }
            bright[y * n + x] = b;
        }
    const uint16_t* lut = vram + (clut >> 6 & 511) * 1024 + (clut & 63) * 16;
    /* Text: the original glyph's ramp, index 1 up to the highest it uses. */
    int hi = 1, stp_votes = 0, opaque = 0;
    for (int y = 0; y < r.cell; y++)
        for (int x = 0; x < r.cell; x++) {
            int i = page_index(vram, page_x, r.cell_u + x, r.cell_v + y);
            hi = i > hi ? i : hi;
            if (i && lut[i])
                ++opaque, stp_votes += lut[i] >> 15;
        }
    uint16_t stp = opaque && stp_votes * 2 > opaque ? 0x8000 : 0;
    int element = family == PC_PROMPTS_KEYBOARD || r.context == DPAD ? -1 : pc_bind_pad[r.button];
    /* Sprites: the icon alone, no backing, in its colour (dim arms dimmer). */
    rgb colour = ink_colour(family, element), edge = hex(0x101014), dim = mix(colour, edge, 0.62f);
    int w = (u1 - u0 + 1) * factor, rows = (v1 - v0 + 1) * factor;
    for (int y = 0; y < rows; y++) {
        int gy = (v0 - r.cell_v) * factor + y;
        for (int x = 0; x < w; x++) {
            int gx = (u0 - r.cell_u) * factor + x;
            float d = gx < n && gy < n ? cover[gy * n + gx] : PC_UI_DIST_FAR;
            out[y * w + x] = r.context == TEXT
                               ? pc_ui_dist_texel(d, PC_UI_DIST_RAMP, 1, hi, 0)
                               : pc_ui_dist_direct(d, ps1(mix(dim, colour, gx < n && gy < n ? bright[gy * n + gx] : 1.0f),
                                     stp));
        }
    }
    ++regions;
    return 1;
}

int pc_button_prompts_mask(const uint16_t* vram, unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1,
    int factor, int scale, unsigned char* mask)
{
    static uint32_t texels[MAX_GRID * MAX_GRID];
    int w = (u1 - u0 + 1) * factor, h = (v1 - v0 + 1) * factor;
    long regions_before = regions, failed_before = failed;
    if (w <= 0 || h <= 0 || w > MAX_GRID || h > MAX_GRID
        || !pc_button_prompts_render(vram, tpage, clut, u0, v0, u1, v1, factor, scale, texels)) {
        regions = regions_before, failed = failed_before;
        return 0;
    }
    regions = regions_before; /* not drawn: the counters are the game's */
    pc_ui_text_resolve(vram, clut, texels, w * h, factor, 0.0f); /* any coverage: what the pixels may show */
    for (int i = 0; i < w * h; i++)
        mask[i] = texels[i] != 0;
    return w * h;
}

/* ---- icons for native (ImGui) menus: prompt_glyphs.cpp ---- */

int pc_button_prompts_icon_rgba(int button, int size, uint32_t* rgba, int max_w, int* w_out, int* h_out)
{
    int family = pc_button_prompts_family(), variant = variant_now();
    if (!family) {
        /* original: the PlayStation's own buttons. The port's own actions
         * (Travel) are no PS1 button: their key when the keyboard was used
         * last (none bound: no icon). */
        family = button >= PC_PAD_BUTTONS && device == PC_PROMPTS_KEYBOARD ? PC_PROMPTS_KEYBOARD
                                                                            : PC_PROMPTS_PLAYSTATION;
        variant = 0;
    }
    if (button < 0 || button >= PC_BIND_SLOTS || size < 4 || size > 128)
        return 0;
    unsigned codepoint = styled_codepoint(family, glyph_for(family, variant, button));
    char path[160];
    snprintf(path, sizeof path, KENNEY "%s.ttf", font_names[family]);
    int missing;
    const pc_ui_icon* icon = codepoint ? pc_ui_icon_glyph(path, codepoint, &missing) : NULL;
    if (!icon)
        return 0;
    float g[4];
    pc_ui_icon_box(icon, g);
    float gw = g[2] - g[0], gh = g[3] - g[1];
    if (gw <= 0 || gh <= 0)
        return 0;
    /* Height `size` with a pixel of margin, width from the icon's aspect. */
    float s = (size - 2) / gh; /* image pixels per field pixel */
    int h = size, w = (int)ceilf(gw * s) + 2;
    if (w > max_w || w > 128 * 4)
        return 0;
    int element = family == PC_PROMPTS_KEYBOARD ? -1 : pc_bind_pad[button];
    rgb col = ink_colour(family, element);
    unsigned cr = (unsigned)lrintf(col.r * 255), cg = (unsigned)lrintf(col.g * 255), cb = (unsigned)lrintf(col.b * 255);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float fx = g[0] + (x + 0.5f - 1) / s, fy = g[3] - (y + 0.5f - 1) / s;
            float c = fminf(fmaxf(0.5f + pc_ui_icon_distance(icon, fx, fy) * s, 0.0f), 1.0f);
            unsigned a = (unsigned)lrintf(c * 255);
            rgba[y * max_w + x] = cr | cg << 8 | cb << 16 | a << 24; /* ImGui's RGBA32 (straight alpha) */
        }
    *w_out = w, *h_out = h;
    return 1;
}

/* ---- settings and reports ---- */

int pc_button_prompts_setting(const char* key, const char* value)
{
    init_once();
    if (!strcmp(key, "button_prompts")) {
        char name[32] = "";
        sscanf(value, "%31s", name);
        int found = 0;
        for (int k = 0; k < PC_PROMPTS_MODES; k++)
            if (!strcmp(name, pc_button_prompts_names[k]))
                pc_button_prompts = k, found = 1;
        if (!found)
            fprintf(stderr, "[settings] button_prompts: unknown value '%s' (auto, playstation, xbox, nintendo, "
                            "keyboard, original)\n",
                name);
        return 1;
    }
    if (!strcmp(key, "button_prompts_style")) {
        char name[32] = "";
        sscanf(value, "%31s", name);
        int found = 0;
        for (int k = 0; k < PC_PROMPTS_STYLES; k++)
            if (!strcmp(name, pc_button_prompts_style_names[k]))
                pc_button_prompts_style = k, found = 1;
        if (!found)
            fprintf(stderr, "[settings] button_prompts_style: unknown value '%s' (solid, outline, color_solid, "
                            "color_outline)\n",
                name);
        return 1;
    }
    if (!strcmp(key, "button_prompts_dpad")) {
        char name[32] = "";
        sscanf(value, "%31s", name);
        int found = 0;
        for (int k = 0; k < PC_PROMPTS_DPADS; k++)
            if (!strcmp(name, pc_button_prompts_dpad_names[k]))
                pc_button_prompts_dpad = k, found = 1;
        if (!found)
            fprintf(stderr, "[settings] button_prompts_dpad: unknown value '%s' (cross, dim, lit)\n", name);
        return 1;
    }
    if (!strcmp(key, "button_prompts_scale")) {
        float k;
        if (sscanf(value, "%f", &k) == 1 && k >= 0.4f && k <= 1.0f)
            pc_button_prompts_scale = k;
        else
            fprintf(stderr, "[settings] button_prompts_scale: '%s' is not 0.4-1\n", value);
        return 1;
    }
    return pc_binding_setting(key, value);
}

void pc_button_prompts_settings_write(FILE* f)
{
    init_once();
    fprintf(f, "button_prompts %s\n", pc_button_prompts_names[pc_button_prompts]);
    fprintf(f, "button_prompts_style %s\n", pc_button_prompts_style_names[pc_button_prompts_style]);
    fprintf(f, "button_prompts_scale %g\n", (double)scale_now());
    fprintf(f, "button_prompts_dpad %s\n", pc_button_prompts_dpad_names[pc_button_prompts_dpad]);
    pc_binding_settings_write(f);
}

int pc_button_prompts_json(char* out, int size)
{
    int family = pc_button_prompts_family(), variant = variant_now();
    int used = snprintf(out, (size_t)size,
        "{\"button_prompts\":\"%s\",\"family\":\"%s\",\"device\":\"%s\",\"variant\":%d,\"generation\":%u,"
        "\"regions\":%ld,\"failed\":%ld,\"glyphs\":{",
        pc_button_prompts_names[pc_button_prompts], family ? pc_button_prompts_names[family] : "original",
        pc_button_prompts_names[device], device_variant, pc_button_prompts_generation(), regions, failed);
    for (int b = 0; b < PC_PAD_BUTTONS && used < size; b++) {
        const char* name = family ? glyph_for(family, variant, b) : NULL;
        int ok = name && codepoint_of(family, name);
        used += snprintf(out + used, (size_t)(size - used), "%s\"%s\":%s%s%s", b ? "," : "", pc_pad_button_names[b],
            ok ? "\"" : "", ok ? name : "null", ok ? "\"" : "");
    }
    for (int b = 0; b < PC_PAD_BUTTONS && used < size; b++) {
        used += snprintf(out + used, (size_t)(size - used), "%s\"%s\":{\"keys\":[", b ? "," : "},\"bindings\":{",
            pc_pad_button_names[b]);
        for (int k = 0, any = 0; k < PC_BIND_KEYS && used < size; k++)
            if (pc_bind_keys[b][k] && pc_key_name(pc_bind_keys[b][k]))
                used += snprintf(out + used, (size_t)(size - used), "%s\"%s\"", any++ ? "," : "",
                    pc_key_name(pc_bind_keys[b][k]));
        if (used < size)
            used += snprintf(out + used, (size_t)(size - used), "],\"pad\":\"%s\"}", pc_pad_element_name(pc_bind_pad[b]));
    }
    /* "shown": {"text": {"circle": "xbox_button_b", ...}, "sprite": {...}}. */
    for (int c = 0; c < 2 && used < size; c++) {
        used += snprintf(out + used, (size_t)(size - used), "%s\"%s\":{", c ? "}," : "},\"shown\":{",
            c ? "sprite" : "text");
        for (int b = 0, any = 0; b < PC_PAD_BUTTONS && used < size; b++) {
            int fam = (int)(shown[b][c] >> 24);
            unsigned cp = shown[b][c] & 0xffffff;
            const char* name = NULL;
            for (int i = 0; fam > 0 && fam < 5 && i < map_sizes[fam]; i++)
                if (maps[fam][i].codepoint == cp)
                    name = maps[fam][i].name;
            if (name)
                used += snprintf(out + used, (size_t)(size - used), "%s\"%s\":\"%s\"", any++ ? "," : "",
                    pc_pad_button_names[b], name);
        }
    }
    /* "dpad": the shortcut cross's D-pad. */
    const char* dpad = NULL;
    for (int i = 0, fam = (int)(shown_dpad >> 24); fam > 0 && fam < 5 && i < map_sizes[fam]; i++)
        if (maps[fam][i].codepoint == (shown_dpad & 0xffffff))
            dpad = maps[fam][i].name;
    {
        /* "dpad_arms": the arms the D-pad was drawn lit (modes dim and lit),
         * null for the whole D-pad. */
        char arms[40] = "null";
        if (shown_arms >= 0) {
            int at = snprintf(arms, sizeof arms, "[");
            for (int k = 0, any = 0; k < 4; k++)
                if (shown_arms >> k & 1)
                    at += snprintf(arms + at, sizeof arms - (size_t)at, "%s\"%s\"", any++ ? "," : "", arm_names[k]);
            snprintf(arms + at, sizeof arms - (size_t)at, "]");
        }
        if (used < size)
            used += snprintf(out + used, (size_t)(size - used),
                "},\"dpad\":%s%s%s,\"dpad_arms\":%s},\"style\":\"%s\",\"scale\":%g,\"dpad_mode\":\"%s\","
                "\"cross_actions\":%u}",
                dpad ? "\"" : "", dpad ? dpad : "null", dpad ? "\"" : "", arms,
                pc_button_prompts_style_names[pc_button_prompts_style], (double)scale_now(),
                pc_button_prompts_dpad_names[pc_button_prompts_dpad], cross_actions);
    }
    return used;
}
