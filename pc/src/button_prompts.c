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
 *            MENUF's PRESS BUTTON prompt, the name entry screen)
 *
 * the region goes through the 2D atlas (ui_upscale.c) and its texels are
 * drawn again from an icon: the PS1 button's binding on the device used
 * last (or the forced family) names a glyph of Kenney's Input Prompts fonts
 * (pc/third_party/fonts/KenneyInputPrompts, with each font's name map), made
 * into a signed distance field by ui_text.c and fitted, centred, into the
 * original's ink box (text: one pixel larger, inside the cell). Text icons
 * take the original glyph's CLUT ramp (coverage -> index), so their colour,
 * brightness and translucency are the text's; sprite icons are a dark body
 * with the family's symbol colours and a dark rim, the symbol being the
 * glyph's holes. Placement, timing and size are the game's: only the texels
 * inside the box change. A key or element without an icon keeps the
 * original. */
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

static int device = PC_PROMPTS_KEYBOARD, device_variant;
static long regions, failed;

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
    static int last_family = -1, last_variant;
    int family = pc_button_prompts_family(), variant = variant_now();
    if (family != last_family || variant != last_variant || pc_bind_generation != last_bind) {
        last_family = family, last_variant = variant, last_bind = pc_bind_generation;
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
    static const char* const ps[] = { [SDL_GAMEPAD_BUTTON_SOUTH] = "playstation_button_cross",
        [SDL_GAMEPAD_BUTTON_EAST] = "playstation_button_circle", [SDL_GAMEPAD_BUTTON_WEST] = "playstation_button_square",
        [SDL_GAMEPAD_BUTTON_NORTH] = "playstation_button_triangle",
        [SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = "playstation_trigger_l1_alternative",
        [SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = "playstation_trigger_r1_alternative",
        [SDL_GAMEPAD_BUTTON_LEFT_STICK] = "playstation_button_l3", [SDL_GAMEPAD_BUTTON_RIGHT_STICK] = "playstation_button_r3",
        [SDL_GAMEPAD_BUTTON_DPAD_UP] = "playstation_dpad_up", [SDL_GAMEPAD_BUTTON_DPAD_DOWN] = "playstation_dpad_down",
        [SDL_GAMEPAD_BUTTON_DPAD_LEFT] = "playstation_dpad_left",
        [SDL_GAMEPAD_BUTTON_DPAD_RIGHT] = "playstation_dpad_right" };
    static const char* const xbox[] = { [SDL_GAMEPAD_BUTTON_SOUTH] = "xbox_button_a",
        [SDL_GAMEPAD_BUTTON_EAST] = "xbox_button_b", [SDL_GAMEPAD_BUTTON_WEST] = "xbox_button_x",
        [SDL_GAMEPAD_BUTTON_NORTH] = "xbox_button_y", [SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = "xbox_lb",
        [SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = "xbox_rb", [SDL_GAMEPAD_BUTTON_LEFT_STICK] = "xbox_ls",
        [SDL_GAMEPAD_BUTTON_RIGHT_STICK] = "xbox_rs", [SDL_GAMEPAD_BUTTON_GUIDE] = "xbox_guide",
        [SDL_GAMEPAD_BUTTON_DPAD_UP] = "xbox_dpad_up", [SDL_GAMEPAD_BUTTON_DPAD_DOWN] = "xbox_dpad_down",
        [SDL_GAMEPAD_BUTTON_DPAD_LEFT] = "xbox_dpad_left", [SDL_GAMEPAD_BUTTON_DPAD_RIGHT] = "xbox_dpad_right" };
    /* Nintendo's labels sit the other way round: A is east, B south. */
    static const char* const nintendo[] = { [SDL_GAMEPAD_BUTTON_SOUTH] = "switch_button_b",
        [SDL_GAMEPAD_BUTTON_EAST] = "switch_button_a", [SDL_GAMEPAD_BUTTON_WEST] = "switch_button_y",
        [SDL_GAMEPAD_BUTTON_NORTH] = "switch_button_x", [SDL_GAMEPAD_BUTTON_LEFT_SHOULDER] = "switch_button_l",
        [SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = "switch_button_r", [SDL_GAMEPAD_BUTTON_BACK] = "switch_button_minus",
        [SDL_GAMEPAD_BUTTON_START] = "switch_button_plus", [SDL_GAMEPAD_BUTTON_GUIDE] = "switch_button_home",
        [SDL_GAMEPAD_BUTTON_LEFT_STICK] = "switch_stick_l_press",
        [SDL_GAMEPAD_BUTTON_RIGHT_STICK] = "switch_stick_r_press", [SDL_GAMEPAD_BUTTON_DPAD_UP] = "switch_dpad_up",
        [SDL_GAMEPAD_BUTTON_DPAD_DOWN] = "switch_dpad_down", [SDL_GAMEPAD_BUTTON_DPAD_LEFT] = "switch_dpad_left",
        [SDL_GAMEPAD_BUTTON_DPAD_RIGHT] = "switch_dpad_right" };
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

/* ---- the game's button pictographs and sprites ---- */

enum { TEXT, SPRITE };
typedef struct {
    int context, button;   /* TEXT / SPRITE; the PS1 button (pad bit index) */
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
    const char* name = glyph_for(family, variant, r->button);
    *codepoint = name ? codepoint_of(family, name) : 0;
    if (!*codepoint)
        return NULL;
    char path[160];
    snprintf(path, sizeof path, KENNEY "%s.ttf", font_names[family]);
    int missing;
    return pc_ui_icon_glyph(path, *codepoint, &missing);
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
    if (r.context == SPRITE && (box[2] - box[0] < 10 || box[3] - box[1] < 10))
        return 0;
    unsigned codepoint;
    if (!icon_for(&r, family, variant_now(), &codepoint))
        return 0;
    *key = (uint64_t)family << 48 ^ (uint64_t)codepoint << 16 ^ (uint64_t)r.context << 8 ^ (uint64_t)r.button;
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

/* A sprite icon's symbol colour: the family's face colours, else light. */
static rgb symbol_colour(int family, int element)
{
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
    return hex(0xe8e8e0);
}

#define MAX_GRID (16 * 8)
/* kind: 1 inside the icon, 2 not; the uncovered pixels the border reaches
 * without crossing the icon become 0 (outside), the rest stay 2 (its holes:
 * the symbol). */
static void find_holes(unsigned char* kind, int w, int h)
{
    static int stack[MAX_GRID * 4 * MAX_GRID];
    int top = 0;
    for (int i = 0; i < w; i++)
        for (int k = 0; k < 2; k++) {
            int e = k ? (h - 1) * w + i : i;
            if (kind[e] == 2)
                kind[e] = 0, stack[top++] = e;
        }
    for (int i = 0; i < h; i++)
        for (int k = 0; k < 2; k++) {
            int e = i * w + (k ? w - 1 : 0);
            if (kind[e] == 2)
                kind[e] = 0, stack[top++] = e;
        }
    while (top) {
        int q = stack[--top], x = q % w, y = q / w;
        int next[4] = { x > 0 ? q - 1 : -1, x < w - 1 ? q + 1 : -1, y > 0 ? q - w : -1, y < h - 1 ? q + w : -1 };
        for (int k = 0; k < 4; k++)
            if (next[k] >= 0 && kind[next[k]] == 2)
                kind[next[k]] = 0, stack[top++] = next[k];
    }
}
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
    /* The target box: the original's ink (text: a pixel more each side). */
    float t0 = (float)box[0], t1 = (float)box[1], t2 = (float)box[2], t3 = (float)box[3];
    if (r.context == TEXT) {
        t0 = fmaxf(t0 - 1, 0), t1 = fmaxf(t1 - 1, 0), t2 = fminf(t2 + 1, (float)r.cell), t3 = fminf(t3 + 1, (float)r.cell);
    }
    float g[4];
    pc_ui_icon_box(icon, g);
    float gw = g[2] - g[0], gh = g[3] - g[1];
    if (gw <= 0 || gh <= 0) {
        ++failed;
        return 0;
    }
    float s = fminf((t2 - t0) / gw, (t3 - t1) / gh); /* original pixels per field pixel */
    float cx_t = (t0 + t2) * 0.5f, cy_t = (t1 + t3) * 0.5f, cx_f = (g[0] + g[2]) * 0.5f, cy_f = (g[1] + g[3]) * 0.5f;
    /* Coverage over the whole cell at the drawn resolution (1x: one per texel). */
    int res = scale < 2 ? 1 : factor, n = r.cell * res;
    static float cover[MAX_GRID * MAX_GRID];
    static unsigned char kind[MAX_GRID * MAX_GRID]; /* 0 outside, 1 inside, 2 hole */
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            float ox = (x + 0.5f) / res, oy = (y + 0.5f) / res;
            float d = pc_ui_icon_distance(icon, cx_f + (ox - cx_t) / s, cy_f - (oy - cy_t) / s) * s;
            float c = fminf(fmaxf(0.5f + d * res, 0.0f), 1.0f);
            cover[y * n + x] = c;
            kind[y * n + x] = c >= 0.5f ? 1 : 2;
        }
    find_holes(kind, n, n);
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
    int element = family == PC_PROMPTS_KEYBOARD ? -1 : pc_bind_pad[r.button];
    rgb body = hex(0x404048), rim = hex(0x141418), symbol = symbol_colour(family, element);
    if (family == PC_PROMPTS_KEYBOARD)
        body = hex(0x48484c);
    int w = (u1 - u0 + 1) * factor, rows = (v1 - v0 + 1) * factor, rep = factor / res;
    for (int y = 0; y < rows; y++) {
        int gy = ((v0 - r.cell_v) * factor + y) / rep;
        for (int x = 0; x < w; x++) {
            int gx = ((u0 - r.cell_u) * factor + x) / rep;
            uint32_t value = 0;
            if (gx < n && gy < n) {
                int p = gy * n + gx;
                float c = cover[p];
                if (r.context == TEXT) {
                    int index = c > 0.02f ? 1 + (int)lrintf(c * (hi - 1)) : 0;
                    value = index ? lut[index] : 0;
                } else if (kind[p] == 2) {
                    value = ps1(mix(symbol, body, c), stp);
                } else if (kind[p] == 0) {
                    value = c > 0.02f ? ps1(rim, stp) : 0;
                } else {
                    /* Inside: blend towards whatever it borders. */
                    int hole = 0, outside = 0;
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++) {
                            int qx = gx + dx, qy = gy + dy;
                            if (qx < 0 || qy < 0 || qx >= n || qy >= n)
                                continue;
                            hole |= kind[qy * n + qx] == 2, outside |= kind[qy * n + qx] == 0;
                        }
                    value = ps1(hole ? mix(symbol, body, c) : outside ? mix(rim, body, c) : body, stp);
                }
            }
            out[y * w + x] = value;
        }
    }
    ++regions;
    return 1;
}

/* ---- icons for native (ImGui) menus: prompt_glyphs.cpp ---- */

int pc_button_prompts_icon_rgba(int button, int size, uint32_t* rgba, int max_w, int* w_out, int* h_out)
{
    int family = pc_button_prompts_family(), variant = variant_now();
    if (!family)
        family = PC_PROMPTS_PLAYSTATION, variant = 0; /* original: the PlayStation's own buttons */
    if (button < 0 || button >= PC_PAD_BUTTONS || size < 4 || size > MAX_GRID)
        return 0;
    const char* name = glyph_for(family, variant, button);
    unsigned codepoint = name ? codepoint_of(family, name) : 0;
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
    if (w > max_w || w > MAX_GRID * 4)
        return 0;
    static float cover[MAX_GRID * 4 * MAX_GRID];
    static unsigned char kind[MAX_GRID * 4 * MAX_GRID];
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float fx = g[0] + (x + 0.5f - 1) / s, fy = g[3] - (y + 0.5f - 1) / s;
            float c = fminf(fmaxf(0.5f + pc_ui_icon_distance(icon, fx, fy) * s, 0.0f), 1.0f);
            cover[y * w + x] = c;
            kind[y * w + x] = c >= 0.5f ? 1 : 2;
        }
    find_holes(kind, w, h);
    int element = family == PC_PROMPTS_KEYBOARD ? -1 : pc_bind_pad[button];
    rgb body = hex(family == PC_PROMPTS_KEYBOARD ? 0x48484c : 0x404048), symbol = symbol_colour(family, element);
    for (int i = 0; i < w * h; i++) {
        float c = cover[i], alpha = kind[i] == 0 ? c : 1.0f;
        rgb col = kind[i] == 2 || kind[i] == 1 ? mix(symbol, body, c) : body;
        if (kind[i] == 1) { /* inside: towards the symbol only next to a hole */
            int x = i % w, y = i / w, hole = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if (x + dx >= 0 && y + dy >= 0 && x + dx < w && y + dy < h && kind[(y + dy) * w + x + dx] == 2)
                        hole = 1;
            col = hole ? mix(symbol, body, c) : body;
        }
        unsigned r = (unsigned)lrintf(col.r * 255), gg = (unsigned)lrintf(col.g * 255),
                 b = (unsigned)lrintf(col.b * 255), a = (unsigned)lrintf(alpha * 255);
        rgba[(i / w) * max_w + i % w] = r | gg << 8 | b << 16 | a << 24; /* ImGui's RGBA32 */
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
    return pc_binding_setting(key, value);
}

void pc_button_prompts_settings_write(FILE* f)
{
    init_once();
    fprintf(f, "button_prompts %s\n", pc_button_prompts_names[pc_button_prompts]);
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
    if (used < size)
        used += snprintf(out + used, (size_t)(size - used), "}}");
    return used;
}
