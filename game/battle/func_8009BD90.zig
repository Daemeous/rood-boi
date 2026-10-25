//! Finds another battle object in the same group and model that already has data to share (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_8009BD90 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Points the object's dataAddr at a matching other object's slot and returns 1, or returns 0 if none matches.
pub fn func_8009BD90(arg0: ?*v.vs_battle_objectData) callconv(.c) c_int {
    const obj = arg0.?;
    // D_800F4538[index] is indexed by a signed char: negative indices reach before the array, as in C.
    const self_ = psx.at(v.g.D_800F4538(), obj.index).*.?;
    const t1: c_int = @as(c_int, self_.unk6E6);

    var var_a2: c_int = @as(c_int, @intFromBool(obj.index >= 2)) * 2;
    while (var_a2 < 17) : (var_a2 +%= 1) {
        if (var_a2 != @as(c_int, obj.index)) {
            // var_a2 is 0..16 here, so the index is in range.
            const temp_v1 = v.g.D_800F4538()[@intCast(var_a2)];
            if (temp_v1 != null and @as(c_int, temp_v1.?.unk6E6) == t1) {
                const t = temp_v1.?;
                if ((obj.modelId == 0 and psx.get(t.unk5D4) != null) or
                    (obj.modelId != 0 and @as(c_int, t.unk5B1) == @as(c_int, obj.modelId)))
                {
                    obj.dataAddr = psx.conv(u32, var_a2);
                    return 1;
                }
            }
        }
    }
    return 0;
}
