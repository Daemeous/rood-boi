//! Turns a relative facing (wrapped to half a turn) and sets the unk5C0 field around a call to func_800AA984 (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800A9EB4 (src/BATTLE/BATTLE.PRG/40564.c)
/// Calls func_800AA984 with the angle relative to the actor's facing, wrapped to +-half a turn, and unk5C0 set to arg2 unless arg2 is -1.
pub fn func_800A9EB4(arg0: c_int, arg1: c_short, arg2: c_int) callconv(.c) void {
    var a1: c_short = arg1;
    // D_800F4538[arg0]: the C does not bound the index.
    const temp = psx.at(v.g.D_800F4538(), arg0).*.?;

    a1 = psx.conv(c_short, @as(c_int, a1) -% @as(c_int, temp.unk0.facing));

    if (@as(c_int, a1) > 4096 / 2) {
        a1 = psx.conv(c_short, @as(c_int, a1) -% 4096);
    }

    if (@as(c_int, a1) < -4096 / 2) {
        a1 = psx.conv(c_short, @as(c_int, a1) +% 4096);
    }

    if (arg2 == -1) {
        c.func_800AA984(arg0, a1, -1);
        return;
    }

    const saved: u16 = temp.unk5C0;
    temp.unk5C0 = psx.conv(u16, arg2);

    c.func_800AA984(arg0, a1, -1);

    temp.unk5C0 = saved;
}
