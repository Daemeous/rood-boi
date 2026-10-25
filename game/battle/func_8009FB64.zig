//! Copies the VECTOR argument to D_800F4438 and sets every live actor's unk183C from D_800F2458 (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_8009FB64 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Stores the vector into D_800F4438, then copies D_800F2458's 1FE0 value into each non-null slot's unk183C.
pub fn func_8009FB64(arg0: ?*v.VECTOR) callconv(.c) void {
    v.g.D_800F4438().* = arg0.?.*;

    const table = v.g.D_800F4538();
    var i: usize = 0;
    while (i < 17) : (i += 1) {
        if (table[i]) |slot| {
            slot.unk183C = psx.conv(i16, v.g.D_800F2458().unk1FE0);
        }
    }
}
