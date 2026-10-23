//! Calls the per-slot update for every actor whose byte at `arg0` is not 0xFF
//! when `arg0` is the current value of D_800E8F2C (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800AB2AC (src/BATTLE/BATTLE.PRG/40564.c)
/// For each of the 17 actor slots, runs both `func_800AB098` passes when the slot is set and its byte at `arg0` is not 0xFF.
pub fn func_800AB2AC(arg0: c_int) callconv(.c) void {
    if (arg0 == @as(c_int, v.g.D_800E8F2C().*)) {
        const table = v.g.D_800F4538();
        var i: c_int = 0;
        while (i < 17) : (i += 1) {
            if (table[@intCast(i)]) |temp| {
                // unk183E is [2]u8: arg0 is not checked against 2 by the C.
                if (psx.at(&temp.unk183E, arg0).* != 0xFF) {
                    c.func_800AB098(@ptrCast(temp), 0, 0);
                    c.func_800AB098(@ptrCast(temp), 1, 0);
                }
            }
        }
    }
}
