/* Modern button prompts (button_prompts.c; README "2D / UI").
 *
 * The game shows PS1 buttons as pictographs in its text (the font's
 * triangle, square, circle and cross) and as 16 x 16 sprites on its field
 * UI page (those four and L1, R1, L2, R2). button_prompts auto (the
 * default) draws each one as the key or gamepad button bound to that PS1
 * button (boot_controller.c bindings) on the device used last, from Kenney's
 * Input Prompts icon fonts (CC0, pc/third_party/fonts/KenneyInputPrompts),
 * inside the original's box: the game still decides what, where and when.
 * playstation / xbox / nintendo / keyboard force one family; original keeps
 * the PS1's glyphs (nothing goes through here then). */
#ifndef ROOD_BUTTON_PROMPTS_H
#define ROOD_BUTTON_PROMPTS_H
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Modes (setting button_prompts); families are the forced modes' values. */
enum {
    PC_PROMPTS_AUTO = 0,
    PC_PROMPTS_PLAYSTATION = 1,
    PC_PROMPTS_XBOX = 2,
    PC_PROMPTS_NINTENDO = 3,
    PC_PROMPTS_KEYBOARD = 4,
    PC_PROMPTS_ORIGINAL = 5,
    PC_PROMPTS_MODES = 6
};
extern int pc_button_prompts; /* PC_PROMPTS_* */
extern const char* const pc_button_prompts_names[PC_PROMPTS_MODES];

/* How the icons look (setting button_prompts_style): solid (a filled cap,
 * the symbol knocked out), outline (ring and symbol, no fill), and both in
 * the family's colours (PlayStation's teal triangle, red circle, blue cross
 * and pink square; Xbox's A green, B red, X blue, Y yellow; the other
 * families stay neutral). No backing behind any of them. */
enum {
    PC_PROMPTS_STYLE_SOLID = 0,
    PC_PROMPTS_STYLE_OUTLINE = 1,
    PC_PROMPTS_STYLE_COLOR_SOLID = 2,
    PC_PROMPTS_STYLE_COLOR_OUTLINE = 3,
    PC_PROMPTS_STYLES = 4
};
extern int pc_button_prompts_style; /* PC_PROMPTS_STYLE_* */
extern const char* const pc_button_prompts_style_names[PC_PROMPTS_STYLES];
/* Sprite icons' size (setting button_prompts_scale, 0.4-1): each fits in a
 * circle this fraction of its 16 x 16 cell across, centred on the
 * original's, so neighbours (the face-button diamond) never touch. */
extern float pc_button_prompts_scale;
/* The L2 shortcut cross's D-pad (setting button_prompts_dpad): cross (the
 * family's whole D-pad in the style: solid, or outline arms), dim (the same,
 * the arms with no usable action beside them dimmed) or lit (outline
 * styles: the arms with a usable action filled, the others hollow; solid
 * styles: those bright, the others' insides dim). */
enum { PC_PROMPTS_DPAD_CROSS = 0, PC_PROMPTS_DPAD_DIM = 1, PC_PROMPTS_DPAD_LIT = 2, PC_PROMPTS_DPADS = 3 };
extern int pc_button_prompts_dpad;
extern const char* const pc_button_prompts_dpad_names[PC_PROMPTS_DPADS];
/* The cross's arm `arm` (0 up, 1 left, 2 right, 3 down: BATTLE.PRG
 * func_800C9FE8's label order, ids 4..7) has a usable action this frame
 * (weapon_slots.c, as the game asks for each label). */
void pc_button_prompts_cross_action(int arm, int usable);
/* Tests (RPC button_prompts cross_actions): those arms (bits up, left, right,
 * down) whatever the game reports; -1: the game's again. */
void pc_button_prompts_cross_override(int arms);

/* The device used last (boot_window.c keyboard, boot_gamepad.c gamepads):
 * family PC_PROMPTS_PLAYSTATION .. PC_PROMPTS_KEYBOARD; variant 3/4/5 for
 * PlayStation generations, 360 for the Xbox 360 (Start / Back glyphs). */
void pc_button_prompts_device(int family, int variant);
/* The family drawn now (0: the original glyphs). */
int pc_button_prompts_family(void);
/* Whether any prompt is drawn from the icon fonts now. */
int pc_button_prompts_active(void);
/* Bumped when what a prompt shows may change (mode, device, bindings). */
unsigned pc_button_prompts_generation(void);

/* 2D region [u0, u1] x [v0, v1] of page `tpage` / CLUT `clut` is (part of) a
 * button pictograph or sprite the icons replace now; *key identifies what it
 * shows (for the 2D atlas's cache). */
int pc_button_prompts_classify(const uint16_t* vram, unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1,
    uint64_t* key);
/* The region's texels at `factor` per texel (PS1 colours, 0 transparent, row
 * by row); `pixel` texels per drawn pixel (internal resolution 1: blocks of
 * the atlas's factor drawn as one). 0 when it cannot be drawn. */
int pc_button_prompts_render(const uint16_t* vram, unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1,
    int factor, int scale, uint32_t* out);

/* Tests (RPC button_prompts mask): which texels pc_button_prompts_render
 * draws (1) for that region now, row by row into `mask`; its size, 0 when
 * nothing is drawn from an icon there. */
int pc_button_prompts_mask(const uint16_t* vram, unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1,
    int factor, int scale, unsigned char* mask);

/* Settings lines (project_settings.cpp): 1 when `key` was ours
 * (button_prompts and the bind_key_* / bind_pad_* bindings). */
int pc_button_prompts_setting(const char* key, const char* value);
void pc_button_prompts_settings_write(FILE* f);
/* JSON object (RPC button_prompts): mode, family, device, generation, the
 * glyph each PS1 button shows now, bindings, counters. */
int pc_button_prompts_json(char* out, int size);
/* Forget which icons were drawn (the report's "shown"). */
void pc_button_prompts_shown_reset(void);

/* ---- Prompts for native (Dear ImGui) menus: prompt_glyphs.cpp ----
 *
 * The same icons, following the same device, mode and bindings, as RGBA
 * images in one Dear ImGui texture that either renderer backend (OpenGL or
 * Vulkan) uploads itself (an ImTextureData registered as a user texture), so
 * menus draw them with ImDrawList::AddImage on GL and Vulkan alike:
 *
 *     pc_prompt_image g = pc_prompt_glyph(PC_PROMPT_CIRCLE, ImGui::GetFontSize());
 *     if (g.texture)
 *         draw->AddImage(((ImTextureData*)g.texture)->GetTexRef(), pos,
 *             ImVec2(pos.x + g.w, pos.y + g.h), ImVec2(g.uv0[0], g.uv0[1]), ImVec2(g.uv1[0], g.uv1[1]));
 *
 * - pc_prompt_glyph(button, size_px): the icon of PS1 button `button`
 *   (PC_PROMPT_*, libetc pad bit index; PC_PROMPT_TRAVEL: the Travel action's
 *   key or pad element) for the device drawn now, `size_px`
 *   high (4..128; width from the icon's shape: key caps such as Enter are
 *   wider). texture is an ImTextureData* (use its GetTexRef(): valid at once,
 *   before the backend has uploaded it; ImTextureRef(ptr) would pick the
 *   legacy ImTextureID constructor and draw from the font atlas); texture_id is the backend's
 *   ImTextureID (GL texture name or VkDescriptorSet) once uploaded, else 0.
 *   texture NULL: no icon (an unbound button, or no ImGui context).
 *   In mode original the icons are the PlayStation family.
 *   Call it from the ImGui frame (between NewFrame and Render). An image
 *   stays valid until pc_prompt_generation() changes (the atlas is redrawn
 *   then).
 * - pc_prompt_device(): the family drawn now: PC_PROMPTS_PLAYSTATION,
 *   PC_PROMPTS_XBOX, PC_PROMPTS_NINTENDO or PC_PROMPTS_KEYBOARD, 0 when the
 *   mode is original (the game's own PS1 glyphs).
 * - pc_prompt_generation(): bumps whenever what a prompt shows may change
 *   (the device used last, the button_prompts setting, any binding). */
enum {
    PC_PROMPT_L2, PC_PROMPT_R2, PC_PROMPT_L1, PC_PROMPT_R1, PC_PROMPT_TRIANGLE, PC_PROMPT_CIRCLE, PC_PROMPT_CROSS,
    PC_PROMPT_SQUARE, PC_PROMPT_SELECT, PC_PROMPT_L3, PC_PROMPT_R3, PC_PROMPT_START, PC_PROMPT_UP, PC_PROMPT_RIGHT,
    PC_PROMPT_DOWN, PC_PROMPT_LEFT, PC_PROMPT_TRAVEL /* binding slot 16 (boot_controller.h PC_BIND_TRAVEL) */
};
typedef struct {
    void* texture;                 /* ImTextureData* (NULL: none) */
    unsigned long long texture_id; /* the backend's ImTextureID once uploaded, else 0 */
    float uv0[2], uv1[2];
    float w, h; /* pixels */
} pc_prompt_image;
pc_prompt_image pc_prompt_glyph(int button, float size_px);
int pc_prompt_device(void);
unsigned pc_prompt_generation(void);
/* The RGBA32 image behind pc_prompt_glyph (rows max_w apart); 0: none. */
int pc_button_prompts_icon_rgba(int button, int size, uint32_t* rgba, int max_w, int* w, int* h);

#ifdef __cplusplus
}
#endif
#endif
