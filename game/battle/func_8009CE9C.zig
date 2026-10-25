//! Battle: stores the selection byte D_800E8F2C and clears the flagged entries of the battle actors (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009CE9C (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Handles the actors whose slot bytes match index, then stores index - 1 in D_800E8F2C; index must be 1..3.
pub fn func_8009CE9C(index: c_uint) callconv(.c) c_int {
    if (index -% 1 >= 3) return -1;

    var i: usize = 0;
    while (i < 17) : (i += 1) {
        const temp = v.g.D_800F4538()[i];
        if (temp) |actor| {
            // Byte offsets: D_800E8F2C + 0x183E and index + 0x183D; index is 1..3 here.
            const selOff: isize = @as(isize, v.g.D_800E8F2C().*) + 0x183E;
            const idxOff: isize = @as(isize, @intCast(index)) + 0x183D;
            if (psx.byteAt(u8, actor, selOff).* != 0xFF and psx.byteAt(u8, actor, idxOff).* == 0xFF) {
                if (actor.unk180C[0].unk1 >= 2) {
                    _ = c.func_800AB098(@ptrCast(actor), 0, 0);
                }
                if (actor.unk180C[1].unk1 >= 2) {
                    _ = c.func_800AB098(@ptrCast(actor), 1, 0);
                }
            }
        }
    }
    v.g.D_800E8F2C().* = psx.conv(u8, index -% 1);
    return 0;
}
