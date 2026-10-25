//! Chooses the battle slot (D_800F45E0) that best matches a given entry (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");

/// orig: func_8009F794 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Returns the index of the live slot with the same tag whose field 0x1E is the largest value not above arg0's, or -1.
pub fn func_8009F794(arg0: ?*v.D_800F45E0_t) callconv(.c) c_int {
    var best: c_int = -0x7530;
    var result: c_int = -1;
    const t1: c_int = psx.byteAt(c_int, arg0.?, 0x5C).*;

    var i: usize = 0;
    while (i < 16) : (i += 1) {
        const temp = v.g.D_800F45E0()[i];
        if (temp != null and psx.getBits(temp.?, "unk8_0") == 0 and temp.?.unk1A == 0 and
            psx.getBits(&temp.?.unk6C[8], "unk0_3") == 0 and temp.?.unk12 == 0xFF and
            t1 == psx.byteAt(c_int, temp.?, 0x5C).*)
        {
            const temp2: c_int = temp.?.unk1E;
            if (@as(c_int, arg0.?.unk1E) >= temp2 and best < temp2) {
                best = temp2;
                result = @intCast(i);
            }
        }
    }
    return result;
}
