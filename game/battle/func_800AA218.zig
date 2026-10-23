//! Refreshes a battle object's tile position from a field record (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800AA218 (src/BATTLE/BATTLE.PRG/40564.c)
/// Runs the slot's pre-update call, then copies the record's tile X, Z and unk5D into the slot.
pub fn func_800AA218(arg0: c_int, arg1: ?*v.func_8006EBF8_t_fields, arg2: c_int) callconv(.c) void {
    const temp_s0 = psx.at(v.g.D_800F4538(), arg0).*;

    if (temp_s0) |t| {
        c.func_800AA290(arg0, @ptrCast(arg1), arg2, psx.conv(c_int, t.unk5C2));
        const fields = arg1.?;
        t.unk0.currentTileX = psx.getBits(fields, "unk0_0");
        t.unk0.unk5D = psx.getBits(fields, "unk0_8");
        t.unk0.currentTileZ = psx.getBits(fields, "unk0_16");
    }
}
