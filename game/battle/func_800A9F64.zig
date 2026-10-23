//! Runs func_800AAA88 for battle slot arg0 with its unk5C0 set to arg2 for the call (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800A9F64 (src/BATTLE/BATTLE.PRG/40564.c)
/// Calls func_800AAA88 for slot arg0 with its unk5C0 temporarily set to arg2; arg2 == -1 calls it unchanged.
pub fn func_800A9F64(arg0: c_int, arg1: ?*v.SVECTOR, arg2: c_int) callconv(.c) void {
    if (arg2 == -1) {
        c.func_800AAA88(arg0, @ptrCast(arg1), -1);
        return;
    }

    // The C indexes D_800F4538 without a bound check and dereferences the slot unconditionally here.
    const slot = psx.at(v.g.D_800F4538(), arg0).*.?;
    const saved = slot.unk5C0;
    slot.unk5C0 = psx.conv(u16, arg2);

    c.func_800AAA88(arg0, @ptrCast(arg1), -1);

    slot.unk5C0 = saved;
}
