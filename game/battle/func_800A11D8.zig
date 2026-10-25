//! Bone matrix and armature lookup for a battle model (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");

/// orig: func_800A11D8 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Copies bone `bone` of model `index` to `out_matrix` and its armature's first word to `out_armature`.
pub fn func_800A11D8(index: c_int, bone: c_int, out_matrix: ?*v.MATRIX, out_armature: [*c]c_uint) callconv(.c) void {
    var model: ?*v.D_800F4538_t = psx.at(v.g.D_800F4538(), index).*;
    if (model == null) {
        const alt: ?*v.D_800F45E0_t = psx.at(v.g.D_800F45E0(), index).*;
        if (alt == null) return;
        model = @ptrCast(alt);
    }
    const m = model.?;
    out_matrix.?.* = psx.at(&m.bones, bone).*;
    const shape = psx.get(m.unk0.unk68).?;
    out_armature[0] = psx.conv(c_uint, psx.at(&shape.armatures, bone).unk0);
}
