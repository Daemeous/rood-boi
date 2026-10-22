//! Looks up a pair in the D_800F5910 table and reports its partner entry, or copies arg0 when no entry matches (src/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_771A8");

/// orig: func_800DF9A8 (src/BATTLE/BATTLE.PRG/771A8.c)
/// Finds arg0's entry in the D_800F5910 pairs and writes the partner entry to arg1; with no match, copies arg0 to arg1 as a 32-bit word.
pub fn func_800DF9A8(arg0: ?*v.func_800DFA54_t, arg1: ?*v.func_800DFA54_t) callconv(.c) void {
    const table = v.g.D_800F5910().*.?;
    const src = arg0.?;

    var i: c_int = 0;
    while (i < 8) : (i += 1) {
        const entry = psx.at(table, i * 2);
        if (@as(c_int, psx.getBits(entry, "unk0_0")) == @as(c_int, src.unk0) and
            @as(c_int, psx.getBits(entry, "unk0_5")) == @as(c_int, src.unk2))
        {
            // Odd entries pair with the even entry below them, even with the one above.
            if ((i & 1) != 0) {
                i -= 1;
            } else {
                i += 1;
            }
            const partner = psx.at(table, i * 2 + 1);
            const dst = arg1.?;
            dst.unk0 = psx.conv(u8, psx.getBits(partner, "unk0_0"));
            dst.unk2 = psx.conv(u8, psx.getBits(partner, "unk0_5"));
            dst.unk1 = psx.conv(u8, i >> 1);
            return;
        }
    }

    // No match: the C copies the whole 4 bytes at arg0 (the struct is 3 bytes).
    const dst32: *align(1) u32 = @ptrCast(arg1.?);
    dst32.* = @as(*align(1) const u32, @ptrCast(src)).*;
}
