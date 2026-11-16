/* BATTLE/4B16C.s vs_render_drawModelGouraudPolygons and vs_render_drawModelGouraudPolygonsWithOverrides: the per-vertex-coloured character models. No packets are built: the Zig
 * (game/battle/pc/build_actor_packets.zig, build_blended_actor_packets.zig) draws them through the native list
 * (actor_native.c pc_object_native_take). */
void vs_render_drawModelGouraudPolygons(void* projected, void* actor, void* unused, void* model);
void vs_render_drawModelGouraudPolygonsWithOverrides(void* projected, void* object, void* color, void* model);
