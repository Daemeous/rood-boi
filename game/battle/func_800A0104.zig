//! Sets a battle object's material and rotation matrix (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_800A0104 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Stores a material index in object `index`, builds its rotation matrix from the material's angles, and clears its state; -1 if the object is absent or its actor is 2 or more.
pub fn func_800A0104(index: c_int, material: c_int) callconv(.c) c_int {
    const obj: *v.D_800F45E0_t = psx.at(v.g.D_800F45E0(), index).* orelse return -1;
    if (psx.getBits(&obj.unk6C[8], "actorId") >= 2) return -1;

    obj.unk6C[8].unk2 = psx.conv(u8, material);
    const m: c_int = material *% 4;
    const scratch: *v.D_1F80035C_t = @ptrFromInt(0x1F80035C);
    const table = v.g.D_800E9030();
    scratch.unk84.vx = psx.conv(i16, @as(c_int, psx.at(table, m).*) *% 1024);
    scratch.unk84.vy = psx.conv(i16, @as(c_int, psx.at(table, m +% 1).*) *% 1024);
    scratch.unk84.vz = psx.conv(i16, @as(c_int, psx.at(table, m +% 2).*) *% 1024);
    _ = c.RotMatrixYXZ_gte(@ptrCast(&scratch.unk84), @ptrCast(&obj.unk6C[4]));
    obj.unk18 = 0;
    psx.byteAt(c_int, obj, 0x24).* = 0;
    obj.unk28 = 0;
    return 0;
}
