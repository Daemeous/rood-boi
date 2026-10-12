// Per-draw parameters of the native list shaders (render.c's uniforms; the
// C side is vk_render.inc's native_ubo, std140 offsets in comments).
layout(std140, binding = 0) uniform Draw {
    mat3 normal_matrix; // 0: room: inverse transpose of the model-view rotation
    vec4 projection; // 48: H * (3/4 in widescreen), H, OFX + draw x, OFY + draw y
    vec4 tint; // 64: dr, dg, db, fade (func_80098D6C)
    vec2 offset; // 80: OFX, OFY (screen space, for the PS1 area test)
    vec2 clip_z; // 88: near, far planes (view z)
    ivec4 rules; // 96: near bucket, far bucket, LOD bucket, instance bias
    ivec4 override_color; // 112: rgb, on
    ivec4 override_texture; // 128: tpage, clut, on
    ivec2 split; // 144: subdivision switch, instance subdivision level
    ivec2 instance_blend; // 152: on, tpage blend bits
    ivec2 forced; // 160: room translucent entries: decided by the game, UVs halved
    int objects; // 168: object rules (characters)
    int zsf3; // 172
    int translucent; // 176: 0 opaque polygons only, 1 semi-transparent ones only, 2 all (effects)
    int perspective; // 180
    int filtering; // 184
    uint window; // 188
    int mask_set; // 192
    float surface; // 196: G-buffer normal alpha: 1 room, 0.75 moving (objects)
    int depth_pass; // 200: objects, pass 2: depth of the visible fragments only
    int painter; // 204: objects drawn in PS1 order
    int semi_pass; // 208: translucent draws: 1 opaque texels only, 2 translucent texels only
    int surface_test; // 212: translucent draws: hidden behind surface_depth (1 exact, 2 room tolerance, 3 decal slack)
    int uv_count; // 216: entries of the animated materials' table (binding 5; 0 is none)
    // 224: the editor's cutaway (room lists, free camera): the focus in view
    // space with the rows' scales divided out (xyz), radius (w; 0: off)
    vec4 cut_focus;
    vec4 cut_scale; // 240: 1 / the view rows' scales (xyz); share of a cut surface's pixels kept (w; 1 none cut)
    vec4 instance_cut; // 256: render_instance.cut: share of the instance's pixels left out (x), ghost edges (y)
} u;
// Animated materials (render_list_uv_effects), entry 0 none: the UV offset
// the game's effect writes this frame (offset.xy), its window (base u, v,
// size u, v) and, between game frames, the part of the offset still to come
// with the wrap period (shift.xy, shift.zw).
struct UvEffect { ivec4 offset; ivec4 window; vec4 shift; };
layout(std430, binding = 5) readonly buffer UvEffects { UvEffect uv_effects[]; };
