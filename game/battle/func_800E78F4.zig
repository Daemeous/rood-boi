//! Tests whether the actor's current target is nearer than the limit in D_800F58BC
//! (src/BATTLE/BATTLE.PRG/7EE98.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_7EE98");
const c = @import("c_7EE98");

/// orig: func_800E78F4 (src/BATTLE/BATTLE.PRG/7EE98.c)
/// Returns 1 when the actor passes the check in func_800DBCB4 and the 13-bit-shifted field of its target is below D_800F58BC's limit, else 0.
pub fn func_800E78F4(arg0: ?*v.func_800E78F4_t) callconv(.c) c_int {
    // arg0->unk470 dereferences arg0 first: a null arg0 traps here, as the C crashes.
    const self = arg0.?;
    var r: c_int = 0;
    var a: c_int = undefined;
    var b: c_int = undefined;

    if (self.unk470 != 0 and c.func_800DBCB4(psx.cptr(c.func_800E78F4_t, self), @ptrCast(psx.get(self.unk430))) != 0) {
        // (u_short) truncates the shifted word to 16 bits.
        a = @as(c_int, psx.conv(u16, psx.get(self.unk430).?.unk4 >> 13));
        b = v.g.D_800F58BC().*.?.unk18;
        r = @intFromBool(a < b);
    }
    return r;
}
