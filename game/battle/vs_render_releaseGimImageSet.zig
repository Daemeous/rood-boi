//! Frees the gim_t block at vs_render_gimLayers and its three sub-images (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_render_releaseGimImageSet (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Releases the gim_t block at vs_render_gimLayers once its image header is marked done, and clears the pointer.
pub fn vs_render_releaseGimImageSet() callconv(.c) void {
    const base: ?*v.gim_t = v.g.vs_render_gimLayers().*;
    if (base) |p| {
        if (psx.getBits(p, "unk0_6") == 3) {
            _ = c.vs_render_retireTexturePages(@as(c_uint, p.unk2), @as(c_uint, psx.getBits(p, "unk0_9")), 6);
            var i: c_int = 0;
            while (i < 3) : (i +%= 1) {
                // var_s0 is p + i; the loop body does not move it, so p is the base.
                const cur = psx.at(p, i);
                if (psx.getBits(cur, "unk0_3") == 3) {
                    // vs_render_gimLayers is re-read here, as the C reads the global each time.
                    const g = v.g.vs_render_gimLayers().*.?;
                    _ = c.vs_render_retireTexturePages(
                        psx.conv(c_uint, @as(c_int, g.unk2) +% @as(c_int, psx.getBits(g, "unk0_9"))),
                        1,
                        6,
                    );
                }
            }
            c.vs_main_freeHeapR(@ptrCast(v.g.vs_render_gimLayers().*));
            v.g.vs_render_gimLayers().* = null;
        }
    }
}
