//! The bone of a battle model whose armature carries a code (src/BATTLE/BATTLE.PRG/38C1C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_38C1C");

/// orig: func_800A152C (src/BATTLE/BATTLE.PRG/38C1C.c)
/// Returns the first bone whose armature field `field` selects matches the low byte of `wanted` (0 for 0xFF, -1 for an empty slot, -4 for no match).
pub fn findBone(actorId: c_int, wanted: c_int, field: c_int) callconv(.c) c_int {
    var model: ?*v.D_800F4538_t = psx.at(v.g.D_800F4538(), actorId).*;
    if (model == null) {
        model = @ptrCast(psx.at(v.g.D_800F45E0(), actorId).*);
        if (model == null) return -1;
    }

    const nBones: c_int = @as(c_int, model.?.unk0.nBones);
    const var_v1 = psx.get(model.?.unk0.unk68);

    if (wanted == 0xFF) return 0;

    const code: c_int = wanted & 0xFF;
    var i: c_int = 0;
    while (i < nBones) : (i +%= 1) {
        const armature = psx.at(&var_v1.?.armatures, i);
        switch (field) {
            0 => {
                if (@as(c_int, armature.unk6) == code) return i;
            },
            1 => {
                const new_var: c_int = @intFromBool((@as(c_int, armature.unk7) >> 4) == code +% 1);
                if (new_var != 0) return i;
            },
            2 => {
                if ((@as(c_int, armature.unk7) & 0xF) == code +% 1) return i;
            },
            3 => {
                if (@as(c_int, armature.unk8) == code) return i;
            },
            else => {},
        }
    }

    return -4;
}
