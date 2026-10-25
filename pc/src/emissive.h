/* Emissive texels: parts of textures that glow (pc/docs/Emissive.md).
 *
 * A catalog (pc/assets/emissive/catalog.json, curated and committed, plus
 * the user's overrides in the user lighting directory, emissive.json) names
 * textures by content (the fixture catalog's key: a hash of the texel
 * indices under a polygon's UV box) and, for each, a parametric mask in
 * texture space (rectangles, polygons, a CLUT index set, colour and
 * luminance ranges), a tint, a strength and whether it is a light.
 *
 * At run time every texture the native room and character lists draw is a
 * "use" (page, CLUT, UV box). A use whose content key is in the catalog is
 * marked: the mask's shapes are written into a VRAM-sized map of slots that
 * the draw shaders read (emissive.glsl). A marked texel that passes its
 * slot's filters is drawn at least as bright as its own colour times the
 * tint and strength (unlit, never darkened by the scene), and the G-buffer
 * carries that emission (shade alpha 0.25, rgb = emission / 2) so the
 * present pass keeps it above the lit result and lets it feed bloom. Light
 * entries also make their room triangles game glows (glows.c, ptgi.c). */
#ifndef ROOD_EMISSIVE_H
#define ROOD_EMISSIVE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Strength of every emissive surface, 0..2 (settings key `emissive`, RPC
 * graphics emissive, Graphics > Glowing surfaces). 0: off, the picture is
 * exactly the one without emissive texels. */
extern float pc_emissive;

#define PC_EMISSIVE_MAP_W 4096
/* 512 rows of cells (slots), 4 rows of slot parameters, 512 rows of the
 * cells' amounts (a painted map's value, else 255). */
#define PC_EMISSIVE_MAP_H 1028
#define PC_EMISSIVE_AMOUNT_ROW 516
/* The shade target's alpha marking an emissive texel (its rgb: emission / 2). */
#define PC_EMISSIVE_SHADE_ALPHA 0.25f

/* Material response (materials.h, pc/docs/Materials.md): the Vulkan slot
 * map image has six more planes of 512 rows below the emissive rows, from
 * PC_MATERIAL_ROW: each cell's roughness, specular and metal (x 255), the
 * relief's slopes along u and v (0: none, else 128 + slope x 63.5) and its
 * cavity (x 255); no material: 255, 0, 0, 0, 0, 0. The draw shaders write
 * them into the G-buffer's material target where pc_material_active(); the
 * present pass lights them (scaled.frag). OpenGL draws no material response. */
#define PC_MATERIAL_ROW PC_EMISSIVE_MAP_H
#define PC_MATERIAL_PLANES 6
#define PC_MATERIAL_MAP_H (512 * PC_MATERIAL_PLANES)
/* The switch (lighting quality key `specular`, 0: the specular lobe off
 * entirely, the picture exactly the one without materials) and the
 * tunables (settings, Lighting panel, RPC lighting): global specular
 * strength (0..4), roughness bias (-1..1, added before the lobe), and the
 * traced reflections' bounces (0: none, the lights' highlights only). */
extern int pc_specular;
extern float pc_specular_strength;
extern float pc_roughness_bias;
extern int pc_reflection_bounces;
/* The relief (height.png, or the estimate from a texture's colours where an
 * entry asks for it): its depth in texels per height unit (0..4; 0 flat),
 * the estimate's switch and strength (0..2; the planes are rebuilt when they
 * change), and the cavities' share of ambient light lost (0..1). The
 * environment: the Fresnel reflection of the room's own light where no
 * traced reflection is bound (0..1), and how far the game's baked light on
 * a material obeys its energy split (0..1; 0: baked light untouched). */
extern float pc_bump_strength;
extern int pc_auto_height;
extern float pc_auto_height_strength;
extern float pc_bump_occlusion;
extern float pc_env_specular;
extern float pc_baked_energy;
/* The editor's preview of the height estimate on every entry in view
 * (material_editor.cpp; not a setting). */
extern int pc_auto_height_preview;
/* The planes (PC_MATERIAL_MAP_H x PC_EMISSIVE_MAP_W bytes); returns whether
 * they changed since the last call (upload them). Valid after
 * pc_emissive_sync. */
int pc_material_rows(const uint8_t** planes);
/* Whether any drawn texel has a material (and the switch is on). */
int pc_material_active(void);

/* render.c: an instance of `mesh` was added to a list (its polygons, the
 * instance's texture override: RENDER_OVERRIDE_TEXTURE in `override`). */
void pc_emissive_note_instance(uint32_t mesh, const void* polygons, int count, int override, unsigned tpage,
    unsigned clut);
/* render.c: the mesh is gone (its uses go with it). */
void pc_emissive_mesh_gone(uint32_t mesh);
/* Scopes (pc/docs/Emissive.md): room_native.c built a mesh of the current
 * room's geometry block at byte `offset` of the room data (polygon i lies at
 * offset + 8 + 32 i, quads 40 bytes each after the triangles);
 * actor_native.c built a mesh of the model whose content hash is `model`. */
void pc_emissive_room_mesh(uint32_t mesh, unsigned offset, unsigned tris, unsigned quads);
void pc_emissive_model_mesh(uint32_t mesh, uint32_t model);
/* gpu_hw.c: native VRAM changed in these 64 x 256 tiles (16 x 2). */
void pc_emissive_vram_dirty(uint32_t tiles);
/* Before a frame's first draw: matches new uses, rebuilds the map when
 * anything changed. Returns the rows to upload in [*row0, *row1) (empty:
 * nothing changed) and whether the shaders should look at it at all. */
int pc_emissive_sync(int* row0, int* row1);
/* The setting or the catalog changed since the last sync (sync now, even
 * between game frames). */
int pc_emissive_pending(void);
/* The CPU copy of the map (PC_EMISSIVE_MAP_W x PC_EMISSIVE_MAP_H bytes). */
const uint8_t* pc_emissive_map(void);
/* Whether any texel is marked (the draw shaders' switch). */
int pc_emissive_active(void);

/* The emission of a texel on the CPU, as emissive.glsl computes it (no
 * texture window): returns 1 when it glows, with its colour (display light)
 * and the entry's light weight (0: not a light). */
int pc_emissive_texel(unsigned u, unsigned v, unsigned tpage, unsigned clut, float rgb[3], float* light);
/* A live texture's normalized UV box, evaluated through the current emissive
 * map (including painted values), independent of its glow light weight.
 * Missing/non-emitting texels return 0 and clear rgb; coordinates clamp to 0..1. */
int pc_emissive_key_texel(const char* key, float u, float v, float rgb[3]);
/* Room glows from light entries (ptgi.c, glows.c): changes whenever the set
 * of room triangles that are lights changes (then glows.c regroups). */
uint32_t pc_emissive_light_serial(void);
/* Whether room triangle `index` of the captured scene (scene_capture.h) has
 * light texels; with rgb, their mean emission over the triangle's 15-point
 * grid times the light weight (zero where it does not glow this frame). */
int pc_emissive_room_light(int index, float rgb[3]);
/* ... and its entries' light shape: the glow's reach (0: the room's
 * glow_reach) and how far in front of the surface it sits (0: on it). */
int pc_emissive_room_light_shape(int index, float* reach, float* lift);

/* Light texels on object lists and other room blocks this present: glow
 * fragments (glow_overrides.h pc_glow_fragment, origin object). */
int pc_emissive_object_lights(void* fragments, int max);

/* RPC emissive (README): list, uses, pick, set, remove, save, reload,
 * export. Returns 0, or -1 with the message in out. */
int pc_emissive_rpc(const char* params, char* out, int size);
/* The editor's panel (dev overlay, Details > Emissive). */
void pc_emissive_panel(void);
/* The material editor (material_editor.cpp): Details > Material, RPC material. */
void pc_material_panel(void);
int pc_material_rpc(const char* params, char* out, int size);

#ifdef __cplusplus
}
#endif
#endif
