//! Frees the gim_t block at D_800EB9BC and its three sub-images (src/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800C86AC (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Releases the gim_t block at D_800EB9BC once its image header is marked done, and clears the pointer.
pub fn func_800C86AC() callconv(.c) void {
    const base: ?*v.gim_t = v.g.D_800EB9BC().*;
    if (base) |p| {
        if (psx.getBits(p, "unk0_6") == 3) {
            _ = c.func_8007E0A8(@as(c_uint, p.unk2), @as(c_uint, psx.getBits(p, "unk0_9")), 6);
            var i: c_int = 0;
            while (i < 3) : (i +%= 1) {
                // var_s0 is p + i; the loop body does not move it, so p is the base.
                const cur = psx.at(p, i);
                if (psx.getBits(cur, "unk0_3") == 3) {
                    // D_800EB9BC is re-read here, as the C reads the global each time.
                    const g = v.g.D_800EB9BC().*.?;
                    _ = c.func_8007E0A8(
                        psx.conv(c_uint, @as(c_int, g.unk2) +% @as(c_int, psx.getBits(g, "unk0_9"))),
                        1,
                        6,
                    );
                }
            }
            c.vs_main_freeHeapR(@ptrCast(v.g.D_800EB9BC().*));
            v.g.D_800EB9BC().* = null;
        }
    }
}
