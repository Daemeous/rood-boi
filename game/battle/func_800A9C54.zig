//! Re-runs func_800AA290 for entity `arg0` and copies the tile from `arg1` into it
//! (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800A9C54 (src/BATTLE/BATTLE.PRG/40564.c)
/// Calls func_800AA290 for entity `arg0` (keeping its stored value 0x5C2 when `arg2` is -1), then stores the tile from `arg1` into the entity.
pub fn func_800A9C54(arg0: c_int, arg1: ?*v.func_8006EBF8_t_fields, arg2: c_int) callconv(.c) void {
    // The entity pointer is read before the call; the C dereferences it only where it uses it.
    const temp_s0 = psx.at(v.g.D_800F4538(), arg0).*;
    if (arg2 == -1) {
        c.func_800AA290(arg0, @ptrCast(arg1), -1, @as(c_int, temp_s0.?.unk5C2));
    } else {
        c.func_800AA290(arg0, @ptrCast(arg1), -1, arg2);
    }
    temp_s0.?.unk0.currentTileX = psx.getBits(arg1.?, "unk0_0");
    temp_s0.?.unk0.unk5D = psx.getBits(arg1.?, "unk0_8");
    temp_s0.?.unk0.currentTileZ = psx.getBits(arg1.?, "unk0_16");
}
