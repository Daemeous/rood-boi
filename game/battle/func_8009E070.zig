//! Battle actor slot setup for a chosen action kind (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009E070 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Sets up actor slot `slot` for action kind `kind` at `position`, then starts its animation.
pub fn func_8009E070(slot: c_int, position: ?*v.SVECTOR, kind: c_int) callconv(.c) void {
    // The slot pointer is loaded before the switch; it is only dereferenced in the cases that use it.
    const temp: ?*v.D_800F4538_t = psx.at(v.g.D_800F4538(), slot).*;

    var var_s1: c_int = undefined;
    switch (kind) {
        1, 2, 3, 4 => var_s1 = kind +% 0x5F,
        5 => var_s1 = 0x1E,
        6 => {
            const t = temp.?;
            var_s1 = 0x1C;
            if ((@as(c_int, psx.getBits(t, "unk5AC_3")) << 3) != 0) {
                var_s1 = 1;
            }
            if (psx.getBits(&t.unk0, "unkA_0") == 3 or psx.getBits(&t.unk0, "unkA_3") == 2) {
                return;
            }
        },
        7 => var_s1 = 0x1D,
        else => return,
    }

    // The call happens before the store, as in the C: a null slot faults at the store.
    const result = c.func_8009E180(@ptrCast(temp), @ptrCast(position));
    const t = temp.?;
    t.unk0.unk3E = psx.conv(i16, result);
    t.unk0.unk18 = 0xA;
    t.unk1846 = 0;
    psx.setBits(t, "unk5AC_20", 1);
    _ = c.func_800A0204(slot, var_s1, 0, 6);
    t.unk6E0 = 0;
}
