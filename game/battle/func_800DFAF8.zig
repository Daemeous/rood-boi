//! Battle unit 771A8 (src/BATTLE/BATTLE.PRG/771A8.c): finds the entry of the
//! D_800F5910 pair table whose two bitfield keys match the arguments and the
//! sp10 record.
const common = @import("common");
const psx = common.psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: func_800DFAF8 (src/BATTLE/BATTLE.PRG/771A8.c)
/// Returns the index i (0..7) of the matching D_800F5910 pair, or -1 when none matches.
pub fn func_800DFAF8(_: c_int, arg1: c_uint, arg2: c_int) callconv(.c) c_int {
    var sp10: [4]v.func_8008D2C0_t = undefined;
    _ = c.func_8008D2C0(psx.cptr(c.func_8008D2C0_t, &sp10[0]));

    const table = v.g.D_800F5910().*.?;
    var i: c_int = 0;
    while (i < 8) : (i += 1) {
        // i < 8: the indices i * 2 and i * 2 + 1 stay within the 16-entry table.
        const first: c_int = if (arg2 == 0) i * 2 + 1 else i * 2;
        const e = psx.at(table, first);
        // i >> 1 is 0..3: within sp10's four entries.
        const j: usize = @intCast(i >> 1);
        if (@as(c_int, psx.conv(u8, arg1)) == @as(c_int, psx.getBits(e, "unk0_0")) and
            @as(c_int, psx.conv(u8, arg1 >> 16)) == @as(c_int, psx.getBits(e, "unk0_5")))
        {
            const m = &sp10[j];
            const next = psx.at(table, i * 2 + 1);
            if (@as(c_int, psx.getBits(next, "unk0_0")) == @as(c_int, m.unk0) >> 7 and
                @as(c_int, psx.getBits(next, "unk0_5")) == @as(c_int, m.unk2) >> 7)
            {
                return i;
            }
        }
    }
    return -1;
}
