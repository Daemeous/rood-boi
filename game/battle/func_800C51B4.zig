//! Applies or clears the groups of the battle entry block (src/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800C51B4 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Applies or clears the group of each entry of the battle entry block, as `mode` selects.
pub fn func_800C51B4(mode: c_int) callconv(.c) void {
    const entries = psx.get(v.g.D_800EB9B8().*.?.unk40);
    if (v.g.D_800EB9B8().*.?.unk31 != 0) {
        if (mode != 3) {
            var i: c_int = 0;
            while (i < @as(c_int, v.g.D_800EB9B8().*.?.unk2C)) : (i +%= 1) {
                const entry = psx.at(entries.?, i);
                const group: c_int = entry.unk9;
                if ((group >> 4) == 0) {
                    var data: ?*anyopaque = null;
                    if (mode == 1) {
                        _ = c.func_800C513C(group, psx.conv(i8, entry.unkA));
                    } else {
                        _ = c.func_800C5164(group, 0, psx.conv(i8, entry.unkA));
                        data = @ptrCast(&v.g.D_800EB9B8().*.?.unk34);
                    }
                    _ = c.func_800C518C(group, data);
                }
            }
        }
    } else {
        const i: c_int = v.g.D_800EB9B8().*.?.unk2D;
        if (i != 255) {
            if ((mode & 1) != 0) {
                const group: c_int = v.g.D_800EB9B8().*.?.unk2F;
                if ((group >> 4) == 0) {
                    _ = c.func_800C513C(group, psx.conv(i8, v.g.D_800EB9B8().*.?.unk30));
                    _ = c.func_800C518C(group, null);
                }
            }
            if ((mode & 2) != 0) {
                const entry = psx.at(entries.?, i);
                v.g.D_800EB9B8().*.?.unk2F = entry.unk9;
                v.g.D_800EB9B8().*.?.unk30 = entry.unkA;
                const group: c_int = v.g.D_800EB9B8().*.?.unk2F;
                if ((group >> 4) == 0) {
                    _ = c.func_800C5164(group, 0, psx.conv(i8, v.g.D_800EB9B8().*.?.unk30));
                    _ = c.func_800C518C(group, @ptrCast(&v.g.D_800EB9B8().*.?.unk34));
                }
            }
        }
    }
}
